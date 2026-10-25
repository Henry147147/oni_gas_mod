using System;
using System.Globalization;
using System.IO;
using System.Linq;
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
        private Rect settingsButton;
        private SettingsButtonBlocker? buttonBlocker;
        private bool buttonBlockerFailed;

        internal static void Install()
        {
            if (instance != null) return;
            var owner = new GameObject("Mixed Atmospheres status");
            DontDestroyOnLoad(owner);
            instance = owner.AddComponent<MixtureStatusDisplay>();
            instance.ApplyPreferences(true);
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
            if (initial || panelExpanded != lastExpandedPreference) expanded = panelExpanded;
            if (initial || panelBoundary != lastBoundaryPreference) showBoundary = panelBoundary;
            lastExpandedPreference = panelExpanded; lastBoundaryPreference = panelBoundary;
        }
        private bool lastExpandedPreference = true, lastBoundaryPreference = true;
        private static string Key(KeyCode key) => key == KeyCode.None ? "" : "Shift+" + key;

        internal static void Remove()
        {
            if (instance != null) { instance.buttonBlocker?.Dispose(); Destroy(instance.gameObject); }
            instance = null;
        }

        private bool InColony => Game.Instance != null && Game.Instance.GameStarted() && Grid.WidthInCells > 0;

        private void Update()
        {
            if (Time.unscaledTime < nextRefresh) return;
            nextRefresh = Time.unscaledTime + .2f;
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

        private void Refresh()
        {
            frame = InColony ? MixturePresentationRuntime.AcquireFrame() : null;
            failure = NativeSealedDomainValidation.Failed ? NativeSealedDomainValidation.FailureMessage : MixtureWorldRendering.LastError;
            measurement = composition = warning = "";
            sampleCell = -1;
            if (!InColony)
            {
                state = "Loaded · waiting for a colony";
                scope = NativeSealedDomainValidation.IsMode
                    ? "Development scene enabled. Start a disposable new colony to see the custom fluid region."
                    : ColonyOwner.Available ? "Start a new base-game colony: Mixed Atmospheres takes over its gases and liquids."
                    : "ONI owns colony physics. Enable the visual development scene to see simulated mixtures (README).";
                return;
            }
            if (ColonyOwner.Active)
            {
                // The colony owner (docs/colony-owner.md) holds every gas and liquid.
                var owner = ColonyOwner.Status;
                var paused = SpeedControlScreen.Instance != null && SpeedControlScreen.Instance.IsPaused;
                state = owner.State == 2 ? "Fluid simulation stopped" : "Mixed Atmospheres owns the asteroid's fluids" + (paused ? " · paused" : " · running");
                if (owner.State == 2) failure = owner.Error;
                scope = "Frame " + owner.CompletedFrames.ToString(CultureInfo.InvariantCulture) + " · " +
                    (frame?.SuppliedCells.Count ?? 0).ToString(CultureInfo.InvariantCulture) + " cells with readings\n" +
                    "Solver pace " + ColonyPacing.OwnerRate.ToString("0.00", CultureInfo.InvariantCulture) + "× real time · game clock " +
                    Time.timeScale.ToString("0.00", CultureInfo.InvariantCulture) + "×\nRenderer: " + (MixtureWorldRendering.LastError.Length != 0 ? "error" :
                    MixtureWorldRendering.Installed && MixtureWorldRendering.SchedulingActive ? "ready" : "waiting for textures") +
                    "\n" + ColonyPipeRuntime.StatusLine;
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
            var camera = CameraController.Instance?.baseCamera;
            var hovered = camera != null ? Grid.PosToCell(camera.ScreenToWorldPoint(Input.mousePosition)) : -1;
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
            if (reading.MechanicalPressurePa.HasValue && reading.LiquidVolumeFraction > 0)
                measurement += "\nFluid " + (reading.MechanicalPressurePa.Value >= 1e6 ?
                    Number(reading.MechanicalPressurePa.Value / 1e6) + " MPa" : Number(reading.MechanicalPressurePa.Value / 1000) + " kPa");
            if (reading.LiquidVolumeFraction > 0)
                measurement += " · liquid " + Number(100 * reading.LiquidVolumeFraction) + "% by volume";
            var liquids = reading.Constituents.Where(value => value.Phase == MixedAtmospheres.Core.Interop.NativeMatterPhase.Liquid && value.MassKg > 0)
                .OrderByDescending(value => value.MassKg).Select(value => value.DisplayName).ToArray();
            if (liquids.Length != 0) measurement += " (" + string.Join(", ", liquids) + ")";
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
                else if (code == detailsKey) expanded = !expanded;
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
            var scale = Mathf.Max(1, Screen.height / 900f);
            if (titleStyle == null)
            {
                // ONI's IMGUI skin supplies an italic debug font. Use Unity's
                // shipped runtime font so this persistent panel stays legible.
                var font = Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");
                titleStyle = new GUIStyle(GUI.skin.label) { font = font, fontSize = 16, fontStyle = FontStyle.Bold, wordWrap = true };
                buttonStyle = new GUIStyle(GUI.skin.button) { font = font, fontSize = 12 };
                textStyle = new GUIStyle(GUI.skin.label) { font = font, fontSize = 14, fontStyle = FontStyle.Normal, wordWrap = true };
                hintStyle = new GUIStyle(GUI.skin.label) { font = font, fontSize = 12, fontStyle = FontStyle.Normal, wordWrap = true };
            }
            var previousMatrix = GUI.matrix;
            var previousColor = GUI.color;
            try
            {
                GUI.matrix = Matrix4x4.Scale(new Vector3(scale, scale, 1));
                if (InColony && showBoundary && frame != null) DrawBoundary(frame, scale);
                var width = Mathf.Min(328, Screen.width / scale - 24);
                var contentWidth = width - 24;
                var body = scope;
                if (measurement.Length != 0) body += "\n\n" + measurement;
                if (composition.Length != 0) body += "\n" + composition;
                if (warning.Length != 0) body += "\n" + warning;
                if (ModSettingsRuntime.Notices.Count != 0) body += "\n\nSettings: " + ModSettingsRuntime.Notices[0];
                if (failure.Length != 0)
                {
                    var message = failure.Split('\n')[0];
                    if (message.Length > 180) message = message.Substring(0, 180) + "… (see Player.log)";
                    body += "\n\nAttention: " + message;
                }
                var hints = Hints();
                var height = 66f + (expanded ? textStyle!.CalcHeight(new GUIContent(body), contentWidth) +
                    hintStyle!.CalcHeight(new GUIContent(hints), contentWidth) + 14 : 0);
                var top = InColony ? 200 : Screen.height / scale - height - 20;
                top = Mathf.Max(100, Mathf.Min(top, Screen.height / scale - height - 110));
                var rect = new Rect(12, top, width, height);
                GUI.color = new Color(.08f, .11f, .16f, .97f);
                GUI.DrawTexture(rect, Texture2D.whiteTexture);
                GUI.color = failure.Length == 0 ? Accent : new Color(1, .55f, .35f);
                GUI.Label(new Rect(rect.x + 12, rect.y + 7, contentWidth - 80, 22), "MIXED ATMOSPHERES", titleStyle);
                GUI.color = Color.white;
                // The settings window (docs/settings.md); a uGUI rectangle under
                // the button keeps its click from reaching the game.
                settingsButton = new Rect(rect.xMax - 84, rect.y + 6, 74, 22);
                if (GUI.Button(settingsButton, "Settings", buttonStyle!)) ModSettingsScreen.Open(0);
                if (Event.current.type == EventType.Repaint)
                {
                    if (buttonBlocker == null && !buttonBlockerFailed) { buttonBlocker = SettingsButtonBlocker.Create(); buttonBlockerFailed = buttonBlocker == null; }
                    buttonBlocker?.Place(new Rect(settingsButton.x * scale, settingsButton.y * scale, settingsButton.width * scale, settingsButton.height * scale));
                }
                GUI.Label(new Rect(rect.x + 12, rect.y + 31, contentWidth, 32), failure.Length == 0 ? state : "Attention · " + state, textStyle);
                if (expanded)
                {
                    var bodyHeight = textStyle!.CalcHeight(new GUIContent(body), contentWidth);
                    GUI.Label(new Rect(rect.x + 12, rect.y + 64, contentWidth, bodyHeight), body, textStyle);
                    GUI.color = new Color(.76f, .83f, .89f);
                    GUI.Label(new Rect(rect.x + 12, rect.y + 72 + bodyHeight, contentWidth, height - 72 - bodyHeight), hints, hintStyle);
                }
            }
            finally { GUI.matrix = previousMatrix; GUI.color = previousColor; }
        }

        private string Hints()
        {
            var lines = new System.Collections.Generic.List<string>();
            var first = string.Join(" · ", new[] { Key(detailsKey).Length != 0 ? Key(detailsKey) + " details" : "", Key(findKey).Length != 0 ? Key(findKey) + " find fluid" : "" }.Where(t => t.Length != 0));
            var second = string.Join(" · ", new[] { Key(overlayKey).Length != 0 ? Key(overlayKey) + " overlay" : "", Key(boundaryKey).Length != 0 ? Key(boundaryKey) + " boundary" : "" }.Where(t => t.Length != 0));
            if (first.Length != 0) lines.Add(first);
            if (second.Length != 0) lines.Add(second);
            if (NativeSealedDomainValidation.IsMode) lines.Add("Shift+F6 diffusion: physical / accelerated / off");
            if (settingsKey != KeyCode.None) lines.Add(Key(settingsKey) + " settings");
            lines.Add("Hover a cell; select it for full composition.");
            return string.Join("\n", lines);
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
                    Status = state, Scope = scope, Failure = failure, Expanded = expanded, BoundaryVisible = showBoundary,
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

    // A transparent uGUI rectangle under the status panel's Settings button:
    // ONI ignores world clicks while the pointer is over uGUI.
    internal sealed class SettingsButtonBlocker : IDisposable
    {
        private readonly GameObject owner;
        private readonly RectTransform area;
        private SettingsButtonBlocker(GameObject owner, RectTransform area) { this.owner = owner; this.area = area; }
        internal static SettingsButtonBlocker? Create()
        {
            try
            {
                var owner = new GameObject("Mixed Atmospheres status button area");
                UnityEngine.Object.DontDestroyOnLoad(owner);
                var canvas = owner.AddComponent<Canvas>(); canvas.renderMode = RenderMode.ScreenSpaceOverlay; canvas.sortingOrder = 29000;
                owner.AddComponent<UnityEngine.UI.GraphicRaycaster>();
                var child = new GameObject("area", typeof(RectTransform));
                child.transform.SetParent(owner.transform, false);
                var image = child.AddComponent<UnityEngine.UI.Image>(); image.color = new Color(0, 0, 0, 0); image.raycastTarget = true;
                var area = (RectTransform)child.transform;
                area.anchorMin = area.anchorMax = new Vector2(0, 1); area.pivot = new Vector2(0, 1);
                return new SettingsButtonBlocker(owner, area);
            }
            catch (Exception error) { Debug.LogWarning("[Mixed Atmospheres] Status button area unavailable: " + error.Message); return null; }
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
