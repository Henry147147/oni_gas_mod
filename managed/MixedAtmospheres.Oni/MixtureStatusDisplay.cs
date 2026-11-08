using System;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Reflection;
using HarmonyLib;
using MixedAtmospheres.Core;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // Read-only UI for every successful mod load, including ordinary colonies
    // without a custom owner. All values come from the pinned presentation;
    // native scalar cells are never promoted to mixture inventories here.
    public sealed class MixtureStatusDisplay : MonoBehaviour
    {
        private static MixtureStatusDisplay? instance;
        private bool expanded = true;
        private bool showBoundary = true;
        // In a colony the panel can be hidden (its × button, the top-bar
        // button beside Vitals) and dragged by its title bar; the main menu
        // always shows it. The dragged position lives in KPlayerPrefs.
        private bool visible = true;
        private bool dragging, customPosition;
        private Vector2 dragOffset;
        private float panelX, panelY;
        private const string PositionX = "MixedAtmospheres.StatusPanel.X", PositionY = "MixedAtmospheres.StatusPanel.Y";
        private const float TitleHeight = 30;
        private Rect panelRect; // last drawn, screen pixels (validation reports)
        private float nextRefresh;
        private float nextDiagnostic;
        private int shortcutsAccepted;
        private string lastShortcut = "";
        private string lastShortcutEvent = "";
        private bool pendingOverlayCycle, pendingDiffusionOverlayRestore;
        private bool overlayObserved;
        private HashedString observedOverlay;
        private bool diagnosticWarning;
        private RuntimeMixturePresentationFrame? frame;
        private int sampleCell = -1;
        private string state = "Loaded", scope = "", measurement = "", composition = "", warning = "", failure = "";
        private GUIStyle? titleStyle, textStyle, hintStyle, buttonStyle;
        private static readonly Color Accent = new Color(.35f, .91f, .91f);
        // Shortcut keys (Shift or Ctrl plus the key) and panel defaults are
        // preferences (docs/settings.md); KeyCode.None disables a shortcut.
        private KeyCode overlayKey = KeyCode.F5, detailsKey = KeyCode.F8, findKey = KeyCode.F9, boundaryKey = KeyCode.F10, settingsKey = KeyCode.None;
        private const KeyCode DiffusionKey = KeyCode.F6; // development regions only
        private PanelClickBlocker? buttonBlocker;
        private bool buttonBlockerFailed;
        // OnGUI runs at least twice per Unity frame; its text changes only
        // with a refresh (five times a second), a preference, the expanded
        // state or the panel width. Build and measure it then, not per call.
        private bool layoutDirty = true, layoutExpanded;
        private float layoutWidth = -1, stateHeight, bodyHeight, diagnosticsHeight, contentHeight;
        private string stateText = "", bodyText = "", diagnosticsText = "";
        private readonly GUIContent measured = new GUIContent();
        // Bottom of ONI's notification list in screen pixels (top origin),
        // sampled with each refresh; the panel starts below it.
        private float notificationsBottom;
        private static readonly Vector3[] corners = new Vector3[4];
        private static readonly FieldInfo ActiveManagementScreen = AccessTools.Field(typeof(ManagementMenu), "activeScreen");

        // In the supported game, IsFullscreenUIActive() compares toggle entries
        // captured before those entries are initialized. Read the actual open
        // screen instead; changing ONI's own menu behavior is unnecessary.
        internal static bool IsFullscreenManagementScreen(Type? screenType) => screenType != null &&
            (typeof(ResearchScreen).IsAssignableFrom(screenType) || typeof(SkillsScreen).IsAssignableFrom(screenType) ||
             typeof(StarmapScreen).IsAssignableFrom(screenType) || typeof(ClusterMapScreen).IsAssignableFrom(screenType));
        private static bool FullscreenManagementActive()
        {
            var menu = ManagementMenu.Instance;
            if (menu == null) return false;
            var active = ActiveManagementScreen.GetValue(menu) as ManagementMenu.ScreenData;
            return active?.screen != null && IsFullscreenManagementScreen(active.screen.GetType());
        }

        internal static void Install()
        {
            if (instance != null) return;
            var owner = new GameObject("Mixed Atmospheres status");
            DontDestroyOnLoad(owner);
            instance = owner.AddComponent<MixtureStatusDisplay>();
            instance.ApplyPreferences(true);
            instance.LoadPosition();
            ModSettingsRuntime.PreferencesChanged += () => { if (instance != null) instance.ApplyPreferences(false); };
        }

        private void ApplyPreferences(bool initial)
        {
            var values = ModSettingsRuntime.Current;
            KeyCode Key(string name) => Enum.TryParse<KeyCode>(values.Choice(name), out var key) ? key : KeyCode.None;
            overlayKey = Key("keys.cycleOverlay"); detailsKey = Key("keys.toggleDetails"); findKey = Key("keys.findFluid");
            boundaryKey = Key("keys.toggleBoundary"); settingsKey = Key("keys.openSettings");
            // The settings window sets the panel's starting state; the shortcuts toggle it until the next change.
            var panelExpanded = values.Toggle("panel.expanded"); var panelBoundary = values.Toggle("panel.showBoundary");
            var panelVisible = values.Toggle("panel.visible");
            if (initial || panelExpanded != lastExpandedPreference) expanded = panelExpanded;
            if (initial || panelBoundary != lastBoundaryPreference) showBoundary = panelBoundary;
            if (initial || panelVisible != lastVisiblePreference) visible = panelVisible;
            lastExpandedPreference = panelExpanded; lastBoundaryPreference = panelBoundary; lastVisiblePreference = panelVisible;
            layoutDirty = true;
        }
        private bool lastExpandedPreference = true, lastBoundaryPreference = true, lastVisiblePreference = true;

        // The title bar's and shortcut's changes are remembered as preferences.
        private void SetExpanded(bool value)
        {
            expanded = value; layoutDirty = true;
            ModSettingsRuntime.SetPreference("panel.expanded", value, persist: true);
        }
        // The top-bar button: returns whether the panel is now shown.
        internal static bool ToggleVisible()
        {
            if (instance == null) return false;
            var next = !instance.visible;
            instance.visible = next; instance.layoutDirty = true;
            ModSettingsRuntime.SetPreference("panel.visible", next, persist: true);
            return next;
        }

        private void LoadPosition()
        {
            if (ModSettingsRuntime.Validation) return;
            try
            {
                if (!KPlayerPrefs.HasKey(PositionX) || !KPlayerPrefs.HasKey(PositionY)) return;
                panelX = KPlayerPrefs.GetFloat(PositionX); panelY = KPlayerPrefs.GetFloat(PositionY);
                customPosition = !float.IsNaN(panelX) && !float.IsNaN(panelY);
            }
            catch (Exception error) when (!(error is OutOfMemoryException)) { customPosition = false; }
        }
        private void EndDrag()
        {
            dragging = false;
            if (ModSettingsRuntime.Validation) return;
            try { KPlayerPrefs.SetFloat(PositionX, panelX); KPlayerPrefs.SetFloat(PositionY, panelY); KPlayerPrefs.Save(); }
            catch (Exception error) when (!(error is OutOfMemoryException))
            { Debug.LogWarning("[Mixed Atmospheres] Could not remember the status panel position: " + error.Message); }
        }
        private static float PanelScale => Mathf.Max(1, Screen.height / 900f);
        private float PanelWidth(float scale) => Mathf.Min(328, Screen.width / scale - 24);

        internal static void Remove()
        {
            if (instance != null) { instance.buttonBlocker?.Dispose(); Destroy(instance.gameObject); }
            instance = null;
        }

        private bool InColony => Game.Instance != null && Game.Instance.GameStarted() && Grid.WidthInCells > 0;

        private void Update()
        {
            // The title bar drags the panel while the left button stays down.
            if (dragging)
            {
                if (!Input.GetMouseButton(0)) EndDrag();
                else
                {
                    var scale = PanelScale;
                    var mouse = new Vector2(Input.mousePosition.x / scale, (Screen.height - Input.mousePosition.y) / scale);
                    panelX = Mathf.Clamp(mouse.x - dragOffset.x, 0, Mathf.Max(0, Screen.width / scale - PanelWidth(scale)));
                    panelY = Mathf.Clamp(mouse.y - dragOffset.y, 0, Mathf.Max(0, Screen.height / scale - TitleHeight));
                    customPosition = true;
                }
            }
            // The readings follow the cursor: a newly hovered cell refreshes at
            // once (at most twenty times a second), otherwise five times a second.
            var now = Time.unscaledTime;
            var moved = now >= lastRefresh + .05f && InColony && HoveredCell() != refreshedCell;
            if (now < nextRefresh && !moved) return;
            nextRefresh = now + .2f; lastRefresh = now;
            layoutDirty = true;
            notificationsBottom = InColony ? NotificationsBottom() : 0;
            try
            {
                Refresh();
                if (Time.unscaledTime >= nextDiagnostic)
                {
                    nextDiagnostic = Time.unscaledTime + 1;
                    RecordValidationStatus();
                }
            }
            catch (Exception error)
            {
                frame = null;
                state = "Readings unavailable";
                measurement = composition = warning = "";
                failure = error.Message;
                scope = "The status display could not read the completed presentation.";
            }
        }

        private int refreshedCell = -1;
        private float lastRefresh;
        private static int HoveredCell()
        {
            var camera = CameraController.Instance?.baseCamera;
            return camera != null ? Grid.PosToCell(camera.ScreenToWorldPoint(Input.mousePosition)) : -1;
        }

        private void Refresh()
        {
            refreshedCell = InColony ? HoveredCell() : -1;
            frame = InColony ? MixturePresentationRuntime.AcquireFrame() : null;
            failure = NativeSealedDomainValidation.Failed ? NativeSealedDomainValidation.FailureMessage : MixtureWorldRendering.LastError;
            measurement = composition = warning = "";
            sampleCell = -1;
            if (!InColony)
            {
                // The main menu panel is just the title bar and Settings
                // unless something is unusual.
                state = "";
                scope = NativeSealedDomainValidation.IsMode
                    ? "Development scene enabled. Start a disposable new colony to see the custom fluid region."
                    : ColonyOwner.Available ? ""
                    : "ONI owns colony physics. Enable the visual development scene to see simulated mixtures (README).";
                return;
            }
            if (ColonyOwner.Active)
            {
                // The colony owner (docs/colony-owner.md) holds every gas and liquid.
                // Normal play needs no status sentence: only a stop is named.
                var owner = ColonyOwner.Status;
                var paused = SpeedControlScreen.Instance != null && SpeedControlScreen.Instance.IsPaused;
                state = owner.State == 2 ? "Fluid simulation stopped" : "";
                if (owner.State == 2) failure = owner.Error;
                scope = "Speed: solver " + ColonyPacing.OwnerRate.ToString("0.00", CultureInfo.InvariantCulture) + "× · " +
                    (paused ? "game paused" : "game " + (ColonyPacing.GameRate > 0 ? ColonyPacing.GameRate.ToString("0.00", CultureInfo.InvariantCulture) + "×" : "…") +
                        " · clock " + Time.timeScale.ToString("0.00", CultureInfo.InvariantCulture) + "×") +
                    "\nFrame " + owner.CompletedFrames.ToString("N0", CultureInfo.InvariantCulture) + " · " +
                    (frame?.SuppliedCells.Count ?? 0).ToString("N0", CultureInfo.InvariantCulture) + " fluid cells" +
                    (MixtureWorldRendering.LastError.Length != 0 ? " · renderer error" :
                        MixtureWorldRendering.Installed && MixtureWorldRendering.SchedulingActive ? "" : " · renderer waiting");
                // Mixed pipes are the normal case; anything else is named.
                var pipes = ColonyPipeRuntime.StatusLine;
                if (pipes != "Pipes: mixed contents") scope += "\n" + pipes;
                if (ColonyPipeRuntime.Failure.Length != 0 && failure.Length == 0) failure = ColonyPipeRuntime.Failure;
                if (frame == null || frame.SuppliedCells.Count == 0) return;
            }
            else if (frame == null || !frame.HasWorld)
            {
                state = NativeSealedDomainValidation.Failed ? "Fluid simulation stopped" :
                    NativeSealedDomainValidation.IsMode ? "Preparing the fluid region" : "Loaded · native ONI physics";
                scope = "No custom world readings are published. Mixture overlays have no data in this colony.";
                return;
            }
            else
            {
                var paused = SpeedControlScreen.Instance != null && SpeedControlScreen.Instance.IsPaused;
                state = NativeSealedDomainValidation.Failed ? "Fluid simulation stopped" : frame.IsFixture ? "Visual fixture · native ONI physics" :
                    NativeSealedDomainValidation.Active ? "Custom fluid active" : "Mixture readings available";
                state += paused ? " · paused" : " · running";
                scope = frame.SuppliedCells.Count.ToString(CultureInfo.InvariantCulture) + " cells with readings · frame " +
                    frame.Revision.ToString(CultureInfo.InvariantCulture) + "\n" +
                    (NativeSealedDomainValidation.Active ? "Bounded development region; surrounding colony uses ONI physics." :
                     frame.IsFixture ? "Colours and readings are a display fixture." : "Only explicitly supplied cells show mixture data.") + "\n" +
                    GasDiffusionOptions.Description + "\nRenderer: " + (MixtureWorldRendering.LastError.Length != 0 ? "error" :
                        MixtureWorldRendering.Installed && MixtureWorldRendering.SchedulingActive ? "ready" : "waiting for textures");
                if (frame.SuppliedCells.Count == 0) return;
            }
            var hovered = refreshedCell;
            if (ColonyOwner.Active)
            {
                // The owner's panel describes the cell under the cursor, never another one.
                if (!Grid.IsValidCell(hovered)) return;
                if (!frame.TryGetCell(hovered, out var under) || under == null)
                {
                    measurement = "Cell " + (hovered % Grid.WidthInCells) + ", " + (hovered / Grid.WidthInCells) + " · " +
                        (Grid.Solid[hovered] ? "solid tile, no fluid" : "no gas or liquid");
                    return;
                }
                sampleCell = hovered;
            }
            else
            {
                sampleCell = frame.SuppliedCells.Keys.Min();
                if (frame.TryGetCell(hovered, out var hoveredReading) && hoveredReading != null) sampleCell = hovered;
            }
            if (!frame.TryGetCell(sampleCell, out var reading) || reading == null) return;
            measurement = "Cell " + (sampleCell % frame.Width) + ", " + (sampleCell / frame.Width) + " · " + reading.BreathingStatus + "\n" +
                Number(reading.TotalMassKg) + " kg · " + Number(reading.TemperatureKelvin - 273.15) + " °C\n" +
                "Gas " + Number(reading.TotalPressurePa / 1000) + " kPa · O₂ " + Number(reading.OxygenPartialPressurePa / 1000) + " kPa";
            var liquid = MixtureInspection.ShowsLiquid(reading);
            if (reading.MechanicalPressurePa.HasValue && liquid)
                measurement += "\nFluid " + (reading.MechanicalPressurePa.Value >= 1e6 ?
                    Number(reading.MechanicalPressurePa.Value / 1e6) + " MPa" : Number(reading.MechanicalPressurePa.Value / 1000) + " kPa");
            if (liquid)
            {
                measurement += " · liquid " + Number(100 * reading.LiquidVolumeFraction) + "% by volume";
                var liquids = reading.Constituents.Where(value => value.Phase == MixedAtmospheres.Core.Interop.NativeMatterPhase.Liquid && value.MassKg > 0)
                    .OrderByDescending(value => value.MassKg).Select(value => value.DisplayName).ToArray();
                if (liquids.Length != 0) measurement += " (" + string.Join(", ", liquids) + ")";
            }
            composition = string.Join(" · ", reading.GasConstituents.Where(value => !value.IsTrace)
                .Select(value => value.DisplayName + " " + (MixtureInspection.ExactReadings || !value.GasMoleFraction.HasValue ? value.MolePercentText :
                    MixtureInspection.Friendly(100 * value.GasMoleFraction.Value) + "%")));
            if (composition.Length == 0 && reading.TotalMassKg > 0) composition = reading.DisplayName;
            warning = reading.Warnings.Count == 0 ? "" : reading.Warnings[0];
        }

        private static string Number(double value) => !MixtureInspection.ExactReadings ? MixtureInspection.Friendly(value) : value.ToString(
            value != 0 && (Math.Abs(value) < .001 || Math.Abs(value) >= 1e7) ? "G5" : "0.###", CultureInfo.InvariantCulture);

        private void FocusReadings()
        {
            if (!InColony || frame == null || frame.SuppliedCells.Count == 0 || CameraController.Instance == null) return;
            var cell = frame.SuppliedCells.Keys.Min();
            var point = Grid.CellToPosCCC(cell, Grid.SceneLayer.Background);
            if (TryRectangle(frame, out var minX, out var minY, out var maxX, out var maxY))
                point = (Grid.CellToPosCCC(Grid.XYToCell(minX, minY), Grid.SceneLayer.Background) +
                    Grid.CellToPosCCC(Grid.XYToCell(maxX, maxY), Grid.SceneLayer.Background)) * .5f;
            var world = Grid.WorldIdx[cell];
            if (ClusterManager.Instance != null && world != byte.MaxValue && ClusterManager.Instance.activeWorldId != world)
                ClusterManager.Instance.SetActiveWorld(world);
            // Set both the current and target zoom. Setting OrthographicSize
            // alone lets ONI immediately interpolate back to its old target.
            // SnapTo also leaves the caller's pause state intact.
            CameraController.Instance.SnapTo(point, 4);
        }

        private void CycleOverlay(HashedString active)
        {
            if (!InColony || OverlayScreen.Instance == null || frame == null || !frame.HasWorld) return;
            var kinds = (MixtureOverlayKind[])Enum.GetValues(typeof(MixtureOverlayKind));
            var next = 0;
            for (var index = 0; index < kinds.Length; ++index)
                if (active == MixtureOverlays.Id(kinds[index])) { next = index + 1; break; }
            OverlayScreen.Instance.ToggleOverlay(next == kinds.Length ? OverlayModes.None.ID : MixtureOverlays.Id(kinds[next]), false);
        }

        private void LateUpdate()
        {
            if (!InColony || OverlayScreen.Instance == null)
            { overlayObserved = pendingOverlayCycle = pendingDiffusionOverlayRestore = false; return; }
            // Modified F6 also reaches ONI's plumbing-overlay input route.
            // Restore the pre-key mode after that route has handled the key.
            if (pendingDiffusionOverlayRestore)
            {
                pendingDiffusionOverlayRestore = false;
                if (overlayObserved && OverlayScreen.Instance.GetMode() != observedOverlay)
                    OverlayScreen.Instance.ToggleOverlay(observedOverlay, false);
            }
            // ONI also handles F5 through its native input route. Apply our
            // modified-key request after that route, using the mode observed
            // before the F5 frame. Reading the current mode here would cycle
            // from native Light every time. OnGUI may queue after LateUpdate.
            if (pendingOverlayCycle)
            {
                pendingOverlayCycle = false;
                CycleOverlay(overlayObserved ? observedOverlay : OverlayScreen.Instance.GetMode());
                RecordValidationStatus();
            }
            if ((!(overlayKey != KeyCode.None && Input.GetKeyDown(overlayKey)) && !Input.GetKeyDown(DiffusionKey)) || !overlayObserved)
            {
                observedOverlay = OverlayScreen.Instance.GetMode();
                overlayObserved = true;
            }
        }

        private void OnGUI()
        {
            // Consume each shortcut's event and modifiers together, including
            // paused inspection, without relying on legacy input poll order.
            var key = Event.current;
            var code = key.keyCode;
            var shortcut = code != KeyCode.None && (code == overlayKey || code == DiffusionKey || code == detailsKey || code == findKey || code == boundaryKey || code == settingsKey);
            if (key.type == EventType.KeyDown && shortcut)
                lastShortcutEvent = key.keyCode + " · " + key.modifiers;
            // The settings window takes keys while it is open.
            if (key.type == EventType.KeyDown && (key.shift || key.control) && shortcut && !ModSettingsScreen.IsOpen)
            {
                var accepted = true;
                if (code == overlayKey) pendingOverlayCycle = true;
                else if (code == detailsKey) SetExpanded(!expanded);
                else if (code == findKey) FocusReadings();
                else if (code == boundaryKey) showBoundary = !showBoundary;
                else if (code == settingsKey) ModSettingsScreen.Open(0);
                else if (code == DiffusionKey) { pendingDiffusionOverlayRestore = true; NativeSealedDomainValidation.CycleGasDiffusion(); }
                else accepted = false;
                if (accepted)
                {
                    lastShortcut = (key.shift ? "Shift+" : "Ctrl+") + key.keyCode; ++shortcutsAccepted;
                    key.Use(); RecordValidationStatus();
                }
            }
            var scale = PanelScale;
            if (titleStyle == null)
            {
                // ONI's IMGUI skin supplies an italic debug font. Use Unity's
                // shipped runtime font so this persistent panel stays legible.
                var font = Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");
                titleStyle = new GUIStyle(GUI.skin.label) { font = font, fontSize = 15, fontStyle = FontStyle.Bold, wordWrap = false, clipping = TextClipping.Clip };
                buttonStyle = new GUIStyle(GUI.skin.button) { font = font, fontSize = 12 };
                textStyle = new GUIStyle(GUI.skin.label) { font = font, fontSize = 14, fontStyle = FontStyle.Normal, wordWrap = true };
                hintStyle = new GUIStyle(GUI.skin.label) { font = font, fontSize = 12, fontStyle = FontStyle.Normal, wordWrap = true };
            }
            var previousMatrix = GUI.matrix;
            var previousColor = GUI.color;
            // ONI's full-screen management screens (skills, research, starmap
            // and the like) cover the world; the panel would cover them. A
            // hidden panel leaves only the top-bar button.
            if (InColony && (FullscreenManagementActive() || !visible))
            {
                dragging = false;
                if (Event.current.type == EventType.Repaint) buttonBlocker?.Place(new Rect(-10000, -10000, 0, 0));
                if (!visible && InColony && showBoundary && frame != null && !FullscreenManagementActive())
                    try { GUI.matrix = Matrix4x4.Scale(new Vector3(scale, scale, 1)); DrawBoundary(frame, scale); }
                    finally { GUI.matrix = previousMatrix; GUI.color = previousColor; }
                return;
            }
            try
            {
                GUI.matrix = Matrix4x4.Scale(new Vector3(scale, scale, 1));
                if (InColony && showBoundary && frame != null) DrawBoundary(frame, scale);
                var width = PanelWidth(scale);
                var contentWidth = width - 24;
                if (layoutDirty || layoutWidth != contentWidth || layoutExpanded != expanded) Layout(contentWidth);
                var height = TitleHeight + contentHeight;
                var screenWidth = Screen.width / scale; var screenHeight = Screen.height / scale;
                float left, top;
                if (customPosition) { left = panelX; top = panelY; }
                else
                {
                    // Below ONI's notification list while it reaches the panel.
                    left = 12;
                    top = InColony ? Mathf.Max(200, notificationsBottom / scale + 8) : screenHeight - height - 20;
                    top = Mathf.Max(100, Mathf.Min(top, screenHeight - height - 110));
                }
                left = Mathf.Clamp(left, 0, Mathf.Max(0, screenWidth - width));
                top = Mathf.Clamp(top, 0, Mathf.Max(0, screenHeight - height));
                var rect = new Rect(left, top, width, height);
                panelRect = new Rect(rect.x * scale, rect.y * scale, rect.width * scale, rect.height * scale);
                GUI.color = new Color(.08f, .11f, .16f, .97f);
                GUI.DrawTexture(rect, Texture2D.whiteTexture);
                // Title bar buttons, right to left: close and collapse (colony
                // only), then Settings (docs/settings.md).
                GUI.color = Color.white;
                var right = rect.xMax - 6;
                Rect close = default, collapse = default;
                if (InColony)
                {
                    close = new Rect(right - 22, rect.y + 6, 22, 22); right -= 24;
                    collapse = new Rect(right - 22, rect.y + 6, 22, 22); right -= 26;
                }
                var settings = new Rect(right - 58, rect.y + 6, 58, 22);
                var title = new Rect(rect.x, rect.y, settings.x - rect.x - 2, TitleHeight);
                // Dragging starts on the title (not its buttons); Update moves it.
                var input = Event.current;
                if (input.type == EventType.MouseDown && input.button == 0 && !ModSettingsScreen.IsOpen)
                {
                    var mouse = new Vector2(Input.mousePosition.x / scale, (Screen.height - Input.mousePosition.y) / scale);
                    if (title.Contains(mouse)) { dragging = true; dragOffset = mouse - rect.position; panelX = rect.x; panelY = rect.y; input.Use(); }
                }
                GUI.color = failure.Length == 0 ? Accent : new Color(1, .55f, .35f);
                GUI.Label(new Rect(rect.x + 12, rect.y + 6, title.width - 12, 22), "MIXED ATMOSPHERES", titleStyle);
                GUI.color = Color.white;
                if (GUI.Button(settings, "Settings", buttonStyle!)) ModSettingsScreen.Open(0);
                if (InColony)
                {
                    if (GUI.Button(collapse, expanded ? "–" : "+", buttonStyle!)) SetExpanded(!expanded);
                    if (GUI.Button(close, "×", buttonStyle!)) { visible = false; layoutDirty = true; ModSettingsRuntime.SetPreference("panel.visible", false, persist: true); }
                }
                // A uGUI rectangle under the whole panel keeps its clicks
                // (buttons, dragging, reading) from reaching the game.
                if (Event.current.type == EventType.Repaint)
                {
                    if (buttonBlocker == null && !buttonBlockerFailed) { buttonBlocker = PanelClickBlocker.Create(); buttonBlockerFailed = buttonBlocker == null; }
                    buttonBlocker?.Place(new Rect(rect.x * scale, rect.y * scale, rect.width * scale, rect.height * scale));
                }
                var y = rect.y + TitleHeight;
                if (stateHeight > 0) { GUI.Label(new Rect(rect.x + 12, y, contentWidth, stateHeight), stateText, textStyle); y += stateHeight + 4; }
                if (bodyHeight > 0) { GUI.Label(new Rect(rect.x + 12, y, contentWidth, bodyHeight), bodyText, textStyle); y += bodyHeight + 6; }
                if (diagnosticsHeight > 0)
                {
                    GUI.color = new Color(.76f, .83f, .89f);
                    GUI.Label(new Rect(rect.x + 12, y, contentWidth, diagnosticsHeight), diagnosticsText, hintStyle);
                }
            }
            finally { GUI.matrix = previousMatrix; GUI.color = previousColor; }
        }

        // The panel's text and heights for this content width. Collapsed, it
        // keeps only the title bar and any stop or failure line.
        private void Layout(float contentWidth)
        {
            stateText = failure.Length == 0 ? state : state.Length != 0 ? "Attention · " + state : "Attention";
            // The main menu has no collapse button: it always shows its lines.
            var open = expanded || !InColony;
            var body = open ? measurement : "";
            if (open && composition.Length != 0) body += (body.Length != 0 ? "\n" : "") + composition;
            if (open && warning.Length != 0) body += (body.Length != 0 ? "\n" : "") + warning;
            if (ModSettingsRuntime.Notices.Count != 0) body += (body.Length != 0 ? "\n\n" : "") + "Settings: " + ModSettingsRuntime.Notices[0];
            if (failure.Length != 0)
            {
                var message = failure.Split('\n')[0];
                if (message.Length > 180) message = message.Substring(0, 180) + "… (see Player.log)";
                body += (body.Length != 0 ? "\n\n" : "") + message;
            }
            bodyText = body; diagnosticsText = scope;
            measured.text = stateText;
            stateHeight = stateText.Length == 0 ? 0 : textStyle!.CalcHeight(measured, contentWidth);
            bodyHeight = diagnosticsHeight = 0;
            if (bodyText.Length != 0) { measured.text = bodyText; bodyHeight = textStyle!.CalcHeight(measured, contentWidth); }
            if (open && diagnosticsText.Length != 0) { measured.text = diagnosticsText; diagnosticsHeight = hintStyle!.CalcHeight(measured, contentWidth); }
            measured.text = "";
            contentHeight = stateHeight + (stateHeight > 0 ? 4 : 0) + bodyHeight + (bodyHeight > 0 ? 6 : 0) + diagnosticsHeight;
            if (contentHeight > 0) contentHeight += 8;
            layoutWidth = contentWidth; layoutExpanded = expanded; layoutDirty = false;
        }

        // The lowest visible notification entry, in screen pixels from the top.
        private static float NotificationsBottom()
        {
            try
            {
                var screen = NotificationScreen.Instance;
                if (screen == null) return 0;
                var bottom = 0f;
                Lowest(screen.LabelsFolder, ref bottom);
                Lowest(screen.MessagesFolder, ref bottom);
                return Mathf.Min(bottom, Screen.height * .6f);
            }
            catch (Exception error) when (error is NullReferenceException || error is MissingReferenceException) { return 0; }
        }
        private static void Lowest(GameObject? folder, ref float bottom)
        {
            if (folder == null || !folder.activeInHierarchy) return;
            var canvas = folder.GetComponentInParent<Canvas>();
            var root = canvas != null ? canvas.rootCanvas : null;
            var camera = root == null || root.renderMode == RenderMode.ScreenSpaceOverlay ? null : root.worldCamera;
            var parent = folder.transform;
            for (var i = 0; i < parent.childCount; ++i)
            {
                if (!(parent.GetChild(i) is RectTransform child) || !child.gameObject.activeInHierarchy) continue;
                child.GetWorldCorners(corners);
                var lowest = RectTransformUtility.WorldToScreenPoint(camera, corners[0]).y;
                bottom = Mathf.Max(bottom, Screen.height - lowest);
            }
        }

        private void RecordValidationStatus()
        {
            // Disposable scenes retain UI evidence alongside their existing
            // reports. Ordinary mod loads perform no status-display file I/O.
            if (ColonyRuntimeValidation.Settings == null) return;
            try
            {
                MixturePresentation? reading = null;
                if (frame != null && sampleCell >= 0) frame.TryGetCell(sampleCell, out reading);
                File.WriteAllText(Path.Combine(ColonyRuntimeValidation.OutputDirectory, "status-display.json"), ReportJson.Serialize(new
                {
                    Kind = "actual-game-mixture-status-display", UnityFrame = Time.frameCount,
                    Status = state, Scope = scope, Failure = failure, Expanded = expanded, Visible = visible, BoundaryVisible = showBoundary,
                    PanelRect = new[] { panelRect.x, panelRect.y, panelRect.width, panelRect.height },
                    LastShortcut = lastShortcut, ShortcutsAccepted = shortcutsAccepted,
                    LastShortcutEvent = lastShortcutEvent, ApplicationFocused = Application.isFocused,
                    Overlay = OverlayScreen.Instance?.GetMode().ToString(),
                    GasDiffusion = GasDiffusionOptions.Current,
                    PresentationRevision = frame?.Revision, PublicationGeneration = frame?.PublicationGeneration,
                    SuppliedCellCount = frame?.SuppliedCells.Count ?? 0, SampleCell = sampleCell,
                    Sample = reading == null ? null : new
                    {
                        reading.TotalMassKg, reading.TemperatureKelvin, reading.TotalPressurePa,
                        reading.MechanicalPressurePa, reading.OxygenPartialPressurePa, reading.LiquidVolumeFraction,
                        reading.BreathingStatus
                    },
                    RendererReady = MixtureWorldRendering.Installed && MixtureWorldRendering.SchedulingActive,
                    CameraSize = CameraController.Instance?.OrthographicSize
                }));
            }
            catch (Exception error)
            {
                if (diagnosticWarning) return;
                diagnosticWarning = true;
                Debug.LogWarning("[Mixed Atmospheres] Could not record development status display: " + error.Message);
            }
        }

        private static bool TryRectangle(RuntimeMixturePresentationFrame view, out int minX, out int minY, out int maxX, out int maxY)
        {
            // Bound per-frame decoration even if a future provider owns a world.
            minX = view.Width; minY = view.Height; maxX = maxY = -1;
            if (view.SuppliedCells.Count == 0 || view.SuppliedCells.Count > 512) return false;
            foreach (var cell in view.SuppliedCells.Keys)
            {
                minX = Math.Min(minX, cell % view.Width); maxX = Math.Max(maxX, cell % view.Width);
                minY = Math.Min(minY, cell / view.Width); maxY = Math.Max(maxY, cell / view.Width);
            }
            // A single outline only denotes a filled rectangle; sparse providers
            // must not visually claim the native cells between their readings.
            return (maxX - minX + 1) * (maxY - minY + 1) == view.SuppliedCells.Count;
        }

        private static void DrawBoundary(RuntimeMixturePresentationFrame view, float scale)
        {
            var camera = CameraController.Instance?.baseCamera;
            if (camera == null || !TryRectangle(view, out var minX, out var minY, out var maxX, out var maxY)) return;
            var lower = camera.WorldToScreenPoint(Grid.CellToPosCCC(Grid.XYToCell(minX, minY), Grid.SceneLayer.Background) - new Vector3(.5f, .5f));
            var upper = camera.WorldToScreenPoint(Grid.CellToPosCCC(Grid.XYToCell(maxX, maxY), Grid.SceneLayer.Background) + new Vector3(.5f, .5f));
            if (lower.z <= 0 || upper.z <= 0) return;
            var rect = Rect.MinMaxRect(lower.x / scale, (Screen.height - upper.y) / scale, upper.x / scale, (Screen.height - lower.y) / scale);
            GUI.color = Accent;
            GUI.DrawTexture(new Rect(rect.x - 2, rect.y - 2, rect.width + 4, 2), Texture2D.whiteTexture);
            GUI.DrawTexture(new Rect(rect.x - 2, rect.yMax, rect.width + 4, 2), Texture2D.whiteTexture);
            GUI.DrawTexture(new Rect(rect.x - 2, rect.y, 2, rect.height), Texture2D.whiteTexture);
            GUI.DrawTexture(new Rect(rect.xMax, rect.y, 2, rect.height), Texture2D.whiteTexture);
            GUI.Label(new Rect(rect.x, rect.y - 24, 260, 24), view.IsFixture ? "MIXTURE DISPLAY FIXTURE" : "CUSTOM FLUID REGION");
        }
    }

    // A transparent uGUI rectangle under the status panel: ONI ignores world
    // clicks while the pointer is over uGUI.
    internal sealed class PanelClickBlocker : IDisposable
    {
        private readonly GameObject owner;
        private readonly RectTransform area;
        private PanelClickBlocker(GameObject owner, RectTransform area) { this.owner = owner; this.area = area; }
        internal static PanelClickBlocker? Create()
        {
            try
            {
                var owner = new GameObject("Mixed Atmospheres status panel area");
                UnityEngine.Object.DontDestroyOnLoad(owner);
                var canvas = owner.AddComponent<Canvas>(); canvas.renderMode = RenderMode.ScreenSpaceOverlay; canvas.sortingOrder = 29000;
                owner.AddComponent<UnityEngine.UI.GraphicRaycaster>();
                var child = new GameObject("area", typeof(RectTransform));
                child.transform.SetParent(owner.transform, false);
                var image = child.AddComponent<UnityEngine.UI.Image>(); image.color = new Color(0, 0, 0, 0); image.raycastTarget = true;
                var area = (RectTransform)child.transform;
                area.anchorMin = area.anchorMax = new Vector2(0, 1); area.pivot = new Vector2(0, 1);
                return new PanelClickBlocker(owner, area);
            }
            catch (Exception error) { Debug.LogWarning("[Mixed Atmospheres] Status panel click area unavailable: " + error.Message); return null; }
        }
        // GUI rectangle in screen pixels, origin top left.
        internal void Place(Rect rect)
        {
            if (area == null) return;
            var canvasScale = owner.GetComponent<Canvas>().scaleFactor;
            area.anchoredPosition = new Vector2(rect.x / canvasScale, -rect.y / canvasScale);
            area.sizeDelta = new Vector2(rect.width / canvasScale, rect.height / canvasScale);
        }
        public void Dispose() { if (owner != null) UnityEngine.Object.Destroy(owner); }
    }
}
