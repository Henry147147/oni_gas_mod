using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using Klei.CustomSettings;
using MixedAtmospheres.Core;
using MixedAtmospheres.Core.Interop;
using Newtonsoft.Json;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // Explicit private fixture. Native ONI owns physics; immutable display
    // inventories exercise the real SelectTool/HoverTextDrawer path only.
    internal static class TileHoverContentsValidation
    {
        private sealed class Receipt
        {
            public int Case = -1, Cell = -1, ScreenWidth = 0, ScreenHeight = 0;
            public long Revision = -1, PublicationGeneration = -1;
            public bool AllNonTraceRowsVisible = false, TraceRowsOmitted = false, HazardWarningVisible = false, NoSelection = false;
        }
        private sealed class Observation
        {
            public int Case, Cell, UnityFrame, ScreenWidth, ScreenHeight, DistinctHoverFrames;
            public long Revision, PublicationGeneration, SessionEpoch;
            public string SourceId = "", Header = "", State = "";
            public string[] Contents = Array.Empty<string>(), Warnings = Array.Empty<string>();
            public string[] NonTraceIds = Array.Empty<string>(), TraceIds = Array.Empty<string>();
            public bool NoSelection, ShadowBarSelected;
            public float PointerX, PointerYTop;
            public double TraceThreshold;
        }
        private static readonly List<object> Evidence = new List<object>();
        private static readonly Dictionary<int, FluidInventory> Cells = new Dictionary<int, FluidInventory>();
        private static readonly Dictionary<KCanvasScaler, float> UiScales = new Dictionary<KCanvasScaler, float>();
        private static FluidMaterialCatalog? catalog;
        private static MixturePresentationModel? model;
        private static FluidInventory? inventory;
        private static Observation? observed;
        private static bool initialized, arenaReady, ready, captured, completed, aborted;
        private static int target, currentCase, startFrame, readyFrame, lastHoverFrame = -1, distinctHoverFrames;
        private static float started, caseStarted, nextReport;
        private static double steamFraction;
        private static string callbackError = "";
        private static string terminalError = "";
        private static string DirectoryPath => ColonyRuntimeValidation.OutputDirectory;
        private static readonly string[] Names = { "default-all-gases", "exact-threshold", "below-threshold", "ui-scale-125" };

        internal static void Tick()
        {
            if (completed || aborted) return;
            if (!initialized) { Initialize(); return; }
            Require(callbackError.Length == 0, callbackError);
            if (!arenaReady)
            {
                Require(Time.realtimeSinceStartup - started < 30, "Native hover arena did not become fluid.");
                if (Time.frameCount - startFrame < 30 || Cells.Keys.Any(cell => Grid.Solid[cell])) return;
                SpeedControlScreen.Instance.Pause(false); arenaReady = true; BeginCase(); return;
            }
            Require(Time.realtimeSinceStartup - caseStarted < 180, "Native tile hover/pixel review exceeded the fixed case deadline.");
            if (!ready && Time.frameCount >= readyFrame)
            {
                var frame = MixturePresentationRuntime.AcquireFrame();
                Require(frame != null && frame.TryGetCell(target, out var reading) && reading != null &&
                    reading.Revision == frame.Revision, "Native hover fixture has no completed target reading.");
                ready = true; ColonyRuntimeValidation.SetStage("waiting-for-native-tile-hover"); WriteReport();
            }
            if (!ready) return;
            if (observed != null && distinctHoverFrames >= 3 && !captured)
            {
                observed.DistinctHoverFrames = distinctHoverFrames;
                captured = true;
                ColonyRuntimeValidation.CaptureScreenshot(Screenshot());
                ColonyRuntimeValidation.SetStage("awaiting-tile-hover-pixel-review"); WriteReport();
            }
            if (captured)
            {
                var receiptPath = Path.Combine(DirectoryPath, Names[currentCase] + ".continue.json");
                if (File.Exists(receiptPath))
                {
                    var bytes = TileHoverReceiptFile.Read(receiptPath);
                    Require(bytes.Length <= 4096, "Hover continuation exceeds its byte limit.");
                    var receipt = JsonConvert.DeserializeObject<Receipt>(bytes, new JsonSerializerSettings
                    { TypeNameHandling = TypeNameHandling.None, MissingMemberHandling = MissingMemberHandling.Error });
                    var held = observed!;
                    Require(receipt != null && receipt.Case == currentCase && receipt.Cell == target && receipt.Revision == held.Revision &&
                        receipt.PublicationGeneration == held.PublicationGeneration && receipt.ScreenWidth == held.ScreenWidth &&
                        receipt.ScreenHeight == held.ScreenHeight && receipt.AllNonTraceRowsVisible && receipt.TraceRowsOmitted &&
                        receipt.HazardWarningVisible && receipt.NoSelection, "Hover continuation does not match its held native observation.");
                    Evidence.Add(new { Name = Names[currentCase], Screenshot = Screenshot(), Observation = held, Passed = true });
                    ++currentCase;
                    if (currentCase == Names.Length)
                    {
                        completed = true; Abort(); WriteReport();
                        ColonyRuntimeValidation.CompletePresentation("actual-game-tile-hover-contents-fixture-validation");
                    }
                    else BeginCase();
                    return;
                }
            }
            if (Time.realtimeSinceStartup >= nextReport) { nextReport = Time.realtimeSinceStartup + 1; WriteReport(); }
        }

        private static void Initialize()
        {
            initialized = true; started = Time.realtimeSinceStartup; startFrame = Time.frameCount;
            Game.Instance.debugWasUsed = true;
            CustomGameSettings.Instance.SetQualitySetting(CustomGameSettingConfigs.SandboxMode, "Enabled");
            Game.Instance.SandboxModeActive = true;
            MixturePresentationLifecycle.EnableShutdownObservation(DirectoryPath,
                () => ColonyRuntimeValidation.Settings?.Mode == "tile-hover", () => MixtureInspection.SessionBindingCount == 0);
            catalog = new FluidMaterialCatalog("tile-hover-fixture-v1", new[]
            {
                new FluidMaterial("nitrogen", NativeMatterPhase.Gas, .0280134, 743),
                new FluidMaterial("oxygen", NativeMatterPhase.Gas, .0319988, 658),
                new FluidMaterial("carbon_dioxide", NativeMatterPhase.Gas, .0440095, 655),
                new FluidMaterial("steam", NativeMatterPhase.Gas, .01801528, 1480),
                new FluidMaterial("hydrogen", NativeMatterPhase.Gas, .00201588, 10180),
                new FluidMaterial("chlorine", NativeMatterPhase.Gas, .070906, 361)
            });
            var fractions = new Dictionary<string, double>
            { ["nitrogen"] = .609997, ["oxygen"] = .25, ["carbon_dioxide"] = .1, ["steam"] = .04, ["hydrogen"] = .000001, ["chlorine"] = .000002 };
            var moles = 101325 / (FluidMaterialCatalog.GasConstantJoulesPerMolKelvin * 293.15);
            inventory = catalog.CreateInventory(fractions.ToDictionary(item => item.Key,
                item => item.Value * moles * catalog.Materials[item.Key].MolarMassKgPerMol), 293.15);
            model = Model(.01);
            steamFraction = model.Describe(inventory, 1).GasConstituents.Single(row => row.MaterialId == "steam").GasMoleFraction!.Value;
            var start = SaveLoader.Instance.cachedGSD.baseStartPos + ClusterManager.Instance.GetStartWorld().WorldOffset;
            var chosen = false;
            for (var y = start.y + 16; y <= start.y + 30 && !chosen; ++y)
            for (var x = start.x - 8; x <= start.x + 8 && !chosen; ++x)
            {
                var cell = Grid.XYToCell(x, y); if (!Grid.IsValidCell(cell)) continue;
                var clear = true;
                for (var layer = 0; layer < (int)ObjectLayer.NumLayers; ++layer) if (Grid.Objects[cell, layer] != null) clear = false;
                if (!clear) continue;
                target = cell; chosen = true;
            }
            Require(chosen, "No empty native hover target is available.");
            var origin = Grid.CellToXY(target);
            var eventId = CellEventLogger.Instance.SandBoxTool;
            for (var dy = -4; dy <= 4; ++dy)
            for (var dx = -6; dx <= 6; ++dx)
            {
                var cell = Grid.XYToCell(origin.x + dx, origin.y + dy);
                Require(Grid.IsValidCell(cell), "Hover arena extends beyond the native grid.");
                var wall = Math.Abs(dy) == 4 || Math.Abs(dx) == 6;
                Grid.Visible[cell] = byte.MaxValue;
                SimMessages.ReplaceElement(cell, wall ? SimHashes.Unobtanium : SimHashes.Oxygen, eventId, wall ? 20000 : 1.2f, 293.15f);
                if (!wall) Cells.Add(cell, inventory);
            }
            var driver = UnityEngine.Object.FindFirstObjectByType<ColonyValidationDriver>();
            driver.gameObject.AddComponent<TileHoverContentsValidationLabel>();
            MixtureInspection.CellHoverObserver = ObserveDraw;
            ColonyRuntimeValidation.SetStage("preparing-native-tile-hover-arena"); WriteReport();
        }

        private static MixturePresentationModel Model(double threshold)
        {
            var ids = new[] { "nitrogen", "oxygen", "carbon_dioxide", "steam", "hydrogen", "chlorine" };
            var names = new[] { "Nitrogen", "Oxygen", "Carbon dioxide", "Steam", "Hydrogen", "Chlorine" };
            var hashes = new[] { SimHashes.Vacuum, SimHashes.Oxygen, SimHashes.CarbonDioxide, SimHashes.Steam, SimHashes.Hydrogen, SimHashes.ChlorineGas };
            var styles = ids.Select((id, i) =>
            {
                Color color = hashes[i] == SimHashes.Vacuum ? new Color(.65f, .65f, .8f) : (Color)ElementLoader.FindElementByHash(hashes[i]).substance.colour;
                return new MixtureSpeciesStyle(id, names[i], new MixtureSrgbColor(color.r, color.g, color.b));
            });
            return new MixturePresentationModel(catalog!, styles, new AtmosphereSettings(), "oxygen", "carbon_dioxide",
                new Dictionary<string, double> { ["chlorine"] = 1e-6, ["hydrogen"] = .01 },
                new MixtureVisualOptions { MeaningfulGasMoleFraction = threshold });
        }

        private static void BeginCase()
        {
            ready = false; captured = false; observed = null; distinctHoverFrames = 0; lastHoverFrame = -1;
            caseStarted = Time.realtimeSinceStartup; readyFrame = Time.frameCount + 3;
            var threshold = currentCase == 1 ? steamFraction : currentCase == 2 ?
                BitConverter.Int64BitsToDouble(BitConverter.DoubleToInt64Bits(steamFraction) + 1) : .01;
            model = Model(threshold);
            MixturePresentationRuntime.ResetProviders(); MixturePresentationRuntime.Configure(model);
            MixturePresentationRuntime.EnableFixtureFrame(Grid.WidthInCells, Grid.HeightInCells, Cells);
            OverlayScreen.Instance.ToggleOverlay(OverlayModes.None.ID, false);
            var center = Grid.CellToPosCCC(target, Grid.SceneLayer.Background);
            CameraController.Instance.SnapTo(center); CameraController.Instance.SetTargetPos(center, 20, false);
            CameraController.Instance.OrthographicSize = currentCase == 1 || currentCase == 2 ? 11 : 17;
            foreach (var scaler in Resources.FindObjectsOfTypeAll<KCanvasScaler>().Where(value => value.gameObject.activeInHierarchy))
            {
                if (!UiScales.ContainsKey(scaler)) UiScales.Add(scaler, scaler.GetUserScale());
                scaler.SetUserScale(currentCase == 3 ? 1.25f : 1f);
            }
            ColonyRuntimeValidation.SetStage("preparing-tile-hover-case"); WriteReport();
        }

        private static void ObserveDraw(int cell, MixturePresentation reading, IReadOnlyList<string> contents, bool selected)
        {
            if (!ready || captured || cell != target || Time.frameCount < readyFrame) return;
            if (SelectTool.Instance == null || SelectTool.Instance.selected != null || selected) return;
            try
            {
                var frame = MixturePresentationRuntime.AcquireFrame();
                Require(frame != null && frame.IsFixture && frame.TryGetCell(cell, out var shared) && ReferenceEquals(reading, shared) &&
                    frame.Revision == reading.Revision, "Actual native hover did not use its pinned completed reading.");
                var rows = reading.GasConstituents.Where(row => !row.IsTrace).ToArray();
                Require(rows.Length == (currentCase == 2 ? 3 : 4), "Actual hover has an unexpected non-trace gas count.");
                Require(rows.All(row => contents.Any(line => line.StartsWith(row.DisplayName + " " + row.MolePercentText + " by moles", StringComparison.Ordinal))) &&
                    contents.Count(line => line.Contains(" by moles")) == rows.Length, "Actual native hover omitted or duplicated a non-trace gas.");
                Require(reading.GasConstituents.Where(row => row.IsTrace).All(row => !contents.Any(line => line.StartsWith(row.DisplayName + " ", StringComparison.Ordinal))),
                    "Actual native hover listed a trace as a bulk gas.");
                Require(reading.Warnings.Any(warning => warning.Contains("Chlorine")) &&
                    !reading.Warnings.Any(warning => warning.Contains("Hydrogen")), "Hazardous and benign traces lost their distinct assessments.");
                if (currentCase == 1) Require(!reading.GasConstituents.Single(row => row.MaterialId == "steam").IsTrace, "Exact-threshold steam is missing.");
                if (currentCase == 2) Require(reading.GasConstituents.Single(row => row.MaterialId == "steam").IsTrace, "Below-threshold steam was included.");
                if (lastHoverFrame != Time.frameCount) { ++distinctHoverFrames; lastHoverFrame = Time.frameCount; }
                var pointer = Input.mousePosition;
                observed = new Observation
                {
                    Case = currentCase, Cell = cell, UnityFrame = Time.frameCount, Revision = reading.Revision,
                    PublicationGeneration = frame!.PublicationGeneration, SessionEpoch = frame.SessionEpoch, SourceId = reading.SourceId,
                    Header = MixtureInspection.FormatHeader(reading), State = MixtureInspection.FormatState(reading),
                    Contents = contents.ToArray(), Warnings = reading.Warnings.ToArray(), NonTraceIds = rows.Select(row => row.MaterialId).ToArray(),
                    TraceIds = reading.GasConstituents.Where(row => row.IsTrace).Select(row => row.MaterialId).ToArray(),
                    NoSelection = true, ShadowBarSelected = selected, ScreenWidth = Screen.width, ScreenHeight = Screen.height,
                    PointerX = pointer.x, PointerYTop = Screen.height - pointer.y, TraceThreshold = model!.VisualOptions.MeaningfulGasMoleFraction
                };
            }
            catch (Exception error) { callbackError = error.ToString(); }
        }

        internal static void Abort(Exception? error = null)
        {
            aborted = !completed;
            terminalError = error?.ToString() ?? "";
            MixtureInspection.CellHoverObserver = null;
            foreach (var entry in UiScales) if (entry.Key != null) entry.Key.SetUserScale(entry.Value);
            UiScales.Clear();
            MixturePresentationRuntime.ClearFixtures();
            if (aborted) WriteReport();
        }

        private static string Screenshot() => (currentCase + 1).ToString("00") + "-tile-hover-" + Names[currentCase] + ".png";
        private static void WriteReport()
        {
            var projected = arenaReady ? CameraController.Instance.baseCamera.WorldToScreenPoint(Grid.CellToPosCCC(target, Grid.SceneLayer.Background)) : Vector3.zero;
            File.WriteAllText(Path.Combine(DirectoryPath, "tile-hover-contents.json"), ReportJson.Serialize(new
            {
                Kind = "actual-native-tile-hover-contents-fixture", Platform = Application.platform.ToString(),
                PhysicsOwner = "native ONI; private immutable display inventories only", FirstPlayableColonyPassed = false,
                Status = completed ? "passed" : aborted ? "failed" : "pending", Error = terminalError,
                Passed = completed && Evidence.Count == 4, Case = currentCase, CaseName = currentCase < Names.Length ? Names[currentCase] : "completed",
                TargetCell = target, ScreenX = projected.x, ScreenYTop = Screen.height - projected.y,
                ScreenWidth = Screen.width, ScreenHeight = Screen.height, Ready = ready, Captured = captured,
                DistinctHoverFrames = distinctHoverFrames, Observation = observed, Evidence, CallbackError = callbackError,
                PerCaseDeadlineSeconds = 180, RequiresActualNativeHover = true, RequiresPixelReviewReceipt = true
            }));
        }
        internal static void DrawLabel()
        {
            if (!initialized || completed || aborted) return;
            GUI.Box(new Rect(Screen.width * .2f, 105, Screen.width * .6f, 55),
                "TILE HOVER CONTENTS TEST · display fixture\n" + Names[Math.Min(currentCase, 3)] + " · hover the centre tile with no selection");
        }
        private static void Require(bool condition, string reason) { if (!condition) throw new InvalidDataException(reason); }
    }

    public sealed class TileHoverContentsValidationLabel : MonoBehaviour
    {
        private void OnGUI() => TileHoverContentsValidation.DrawLabel();
    }
}
