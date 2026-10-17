using System;
using System.Globalization;
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
        private RuntimeMixturePresentationFrame? frame;
        private int sampleCell = -1;
        private string state = "Loaded", scope = "", measurement = "", composition = "", warning = "", failure = "";
        private GUIStyle? titleStyle, textStyle, hintStyle;
        private static readonly Color Accent = new Color(.35f, .91f, .91f);

        internal static void Install()
        {
            if (instance != null) return;
            var owner = new GameObject("Mixed Atmospheres status");
            DontDestroyOnLoad(owner);
            instance = owner.AddComponent<MixtureStatusDisplay>();
        }

        internal static void Remove()
        {
            if (instance != null) Destroy(instance.gameObject);
            instance = null;
        }

        private bool InColony => Game.Instance != null && Game.Instance.GameStarted() && Grid.WidthInCells > 0;

        private void Update()
        {
            var control = Input.GetKey(KeyCode.LeftControl) || Input.GetKey(KeyCode.RightControl);
            if (control)
            {
                if (Input.GetKeyDown(KeyCode.F8)) expanded = !expanded;
                if (Input.GetKeyDown(KeyCode.F9)) FocusReadings();
                if (Input.GetKeyDown(KeyCode.F10)) showBoundary = !showBoundary;
            }
            if (Time.unscaledTime < nextRefresh) return;
            nextRefresh = Time.unscaledTime + .2f;
            try { Refresh(); }
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
                    : "ONI owns colony physics. Enable the visual development scene to see simulated mixtures (README).";
                return;
            }
            if (frame == null || !frame.HasWorld)
            {
                state = NativeSealedDomainValidation.IsMode ? "Preparing the fluid region" : "Loaded · native ONI physics";
                scope = "No custom world readings are published. Mixture overlays have no data in this colony.";
                return;
            }
            var paused = SpeedControlScreen.Instance != null && SpeedControlScreen.Instance.IsPaused;
            state = frame.IsFixture ? "Visual fixture · native ONI physics" :
                NativeSealedDomainValidation.Active ? "Custom fluid active" : "Mixture readings available";
            state += paused ? " · paused" : " · running";
            scope = frame.SuppliedCells.Count.ToString(CultureInfo.InvariantCulture) + " cells with readings · frame " +
                frame.Revision.ToString(CultureInfo.InvariantCulture) + "\n" +
                (NativeSealedDomainValidation.Active ? "Bounded development region; surrounding colony uses ONI physics." :
                 frame.IsFixture ? "Colours and readings are a display fixture." : "Only explicitly supplied cells show mixture data.") + "\n" +
                "Renderer: " + (MixtureWorldRendering.Installed && MixtureWorldRendering.SchedulingActive ? "ready" : "waiting for textures");
            if (frame.SuppliedCells.Count == 0) return;
            sampleCell = frame.SuppliedCells.Keys.Min();
            var camera = CameraController.Instance?.baseCamera;
            if (camera != null)
            {
                var hovered = Grid.PosToCell(camera.ScreenToWorldPoint(Input.mousePosition));
                if (frame.TryGetCell(hovered, out var hoveredReading) && hoveredReading != null) sampleCell = hovered;
            }
            if (!frame.TryGetCell(sampleCell, out var reading) || reading == null) return;
            measurement = "Cell " + (sampleCell % frame.Width) + ", " + (sampleCell / frame.Width) + " · " + reading.BreathingStatus + "\n" +
                Number(reading.TotalMassKg) + " kg · " + Number(reading.TemperatureKelvin - 273.15) + " °C\n" +
                "Gas " + Number(reading.TotalPressurePa / 1000) + " kPa · O₂ " + Number(reading.OxygenPartialPressurePa / 1000) + " kPa";
            if (reading.MechanicalPressurePa.HasValue)
                measurement += "\nFluid " + Number(reading.MechanicalPressurePa.Value / 1000) + " kPa";
            if (reading.LiquidVolumeFraction > 0)
                measurement += " · liquid " + Number(100 * reading.LiquidVolumeFraction) + "% by volume";
            composition = string.Join(" · ", reading.GasConstituents.Where(value => !value.IsTrace)
                .Select(value => value.DisplayName + " " + value.MolePercentText));
            if (composition.Length == 0 && reading.TotalMassKg > 0) composition = reading.DisplayName;
            warning = reading.Warnings.Count == 0 ? "" : reading.Warnings[0];
        }

        private static string Number(double value) => value.ToString(
            value != 0 && (Math.Abs(value) < .001 || Math.Abs(value) >= 1e7) ? "G5" : "0.###", CultureInfo.InvariantCulture);

        private void FocusReadings()
        {
            if (!InColony || frame == null || frame.SuppliedCells.Count == 0 || CameraController.Instance == null) return;
            var cell = frame.SuppliedCells.Keys.Min();
            var point = Grid.CellToPosCCC(cell, Grid.SceneLayer.Background);
            CameraController.Instance.SnapTo(point);
            CameraController.Instance.SetTargetPos(point, 20, false);
            CameraController.Instance.OrthographicSize = 4;
        }

        private void OnGUI()
        {
            var scale = Mathf.Max(1, Screen.height / 900f);
            if (titleStyle == null)
            {
                titleStyle = new GUIStyle(GUI.skin.label) { fontSize = 16, fontStyle = FontStyle.Bold, wordWrap = true };
                textStyle = new GUIStyle(GUI.skin.label) { fontSize = 14, wordWrap = true };
                hintStyle = new GUIStyle(GUI.skin.label) { fontSize = 12, wordWrap = true };
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
                if (failure.Length != 0)
                {
                    var message = failure.Split('\n')[0];
                    if (message.Length > 180) message = message.Substring(0, 180) + "… (see Player.log)";
                    body += "\n\nAttention: " + message;
                }
                var hints = "Ctrl+F8 details · Ctrl+F9 find fluid\nCtrl+F10 boundary · overlays: top-right menu\nHover a cell; select it for full composition.";
                var height = 66f + (expanded ? textStyle!.CalcHeight(new GUIContent(body), contentWidth) +
                    hintStyle!.CalcHeight(new GUIContent(hints), contentWidth) + 14 : 0);
                var top = InColony ? 200 : Screen.height / scale - height - 20;
                top = Mathf.Max(100, Mathf.Min(top, Screen.height / scale - height - 110));
                var rect = new Rect(12, top, width, height);
                GUI.color = new Color(.08f, .11f, .16f, .97f);
                GUI.DrawTexture(rect, Texture2D.whiteTexture);
                GUI.color = failure.Length == 0 ? Accent : new Color(1, .55f, .35f);
                GUI.Label(new Rect(rect.x + 12, rect.y + 7, contentWidth, 22), "MIXED ATMOSPHERES", titleStyle);
                GUI.color = Color.white;
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

        private static void DrawBoundary(RuntimeMixturePresentationFrame view, float scale)
        {
            var camera = CameraController.Instance?.baseCamera;
            // Bound per-frame decoration even if a future provider owns a world.
            if (camera == null || view.SuppliedCells.Count == 0 || view.SuppliedCells.Count > 512) return;
            var minX = view.Width; var minY = view.Height; var maxX = -1; var maxY = -1;
            foreach (var cell in view.SuppliedCells.Keys)
            {
                minX = Math.Min(minX, cell % view.Width); maxX = Math.Max(maxX, cell % view.Width);
                minY = Math.Min(minY, cell / view.Width); maxY = Math.Max(maxY, cell / view.Width);
            }
            // A single outline only denotes a filled rectangle; sparse providers
            // must not visually claim the native cells between their readings.
            if ((maxX - minX + 1) * (maxY - minY + 1) != view.SuppliedCells.Count) return;
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
}
