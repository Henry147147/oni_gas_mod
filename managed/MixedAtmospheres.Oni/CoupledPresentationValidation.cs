using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using Klei.CustomSettings;
using MixedAtmospheres.Core;
using Newtonsoft.Json;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // Explicit small closed-reference scene. Its physical owners feed the real
    // renderer/hover path; it does not take ownership of the surrounding colony.
    internal static class CoupledPresentationValidation
    {
        private sealed class Receipt
        {
            public int Case = -1, Cell = -1;
            public long Revision = -1, PublicationGeneration = -1;
            public bool AllNonTraceRowsVisible = false, TraceRowsOmitted = false, HazardWarningVisible = false, NoSelection = false;
        }
        private static readonly string[] Names = { "completed-mixture", "changed-mixture", "colour-only" };
        private static readonly List<object> Evidence = new List<object>();
        private static CoupledColonySimulation? simulation;
        private static CoupledColonyFrame? held;
        private static object? observation;
        private static bool initialized, arenaReady, captured, completed, aborted;
        private static int target, nativeStartFrame, readyFrame, currentCase, hoverFrames, ignoredStartupShutdowns, lastHoverFrame = -1;
        private static long observedGeneration;
        private static float started, caseStarted, nextReport;
        private static string error = "", callbackError = "";
        private static string DirectoryPath => ColonyRuntimeValidation.OutputDirectory;

        internal static void Tick()
        {
            if (completed || aborted) return;
            if (!initialized) { Initialize(); return; }
            Require(callbackError.Length == 0, callbackError);
            if (!arenaReady)
            {
                Require(Time.realtimeSinceStartup - started < 30, "Native reference arena did not become fluid.");
                if (Time.frameCount - nativeStartFrame < 30 || held!.Projection.Inventories.Keys.Any(cell => Grid.Solid[cell])) return;
                SpeedControlScreen.Instance.Pause(false); arenaReady = true; BeginCase(); return;
            }
            Require(Time.realtimeSinceStartup - caseStarted < 180, "Coupled presentation hover/pixel review exceeded its fixed case deadline.");
            if (Time.frameCount < readyFrame) return;
            if (!captured && hoverFrames >= 3)
            {
                captured = true; ColonyRuntimeValidation.CaptureScreenshot(Screenshot());
                ColonyRuntimeValidation.SetStage("awaiting-coupled-presentation-pixel-review"); WriteReport();
            }
            if (captured)
            {
                var path = Path.Combine(DirectoryPath, Names[currentCase] + ".continue.json");
                if (File.Exists(path))
                {
                    var bytes = File.ReadAllText(path); Require(bytes.Length <= 4096, "Coupled hover receipt is too large.");
                    var receipt = JsonConvert.DeserializeObject<Receipt>(bytes, new JsonSerializerSettings
                    { TypeNameHandling = TypeNameHandling.None, MissingMemberHandling = MissingMemberHandling.Error });
                    Require(receipt != null && receipt.Case == currentCase && receipt.Cell == target && receipt.Revision == held!.Projection.Revision &&
                        receipt.PublicationGeneration == observedGeneration && receipt.AllNonTraceRowsVisible && receipt.TraceRowsOmitted &&
                        receipt.HazardWarningVisible && receipt.NoSelection, "Pixel review does not match the held physical hover observation.");
                    Evidence.Add(new { Name = Names[currentCase], Screenshot = Screenshot(), Observation = observation, Passed = true });
                    ++currentCase;
                    if (currentCase == Names.Length)
                    {
                        completed = true; Abort(); WriteReport();
                        ColonyRuntimeValidation.CompletePresentation("actual-game-coupled-reference-presentation-validation");
                    }
                    else
                    {
                        if (currentCase == 1) held = simulation!.AdvanceFrame(2, .03);
                        else MixturePresentationRuntime.Configure(held!.Presentation!.Model.WithVisualOptions(new MixtureVisualOptions { PatternEnabled = false }));
                        BeginCase();
                    }
                    return;
                }
            }
            if (Time.realtimeSinceStartup >= nextReport) { nextReport = Time.realtimeSinceStartup + 1; WriteReport(); }
        }

        private static void Initialize()
        {
            initialized = true; started = Time.realtimeSinceStartup; nativeStartFrame = Time.frameCount;
            NativeLibraryBootstrap.LoadCoupledReference(Mod.DirectoryPath);
            Game.Instance.debugWasUsed = true;
            CustomGameSettings.Instance.SetQualitySetting(CustomGameSettingConfigs.SandboxMode, "Enabled");
            Game.Instance.SandboxModeActive = true;
            var start = SaveLoader.Instance.cachedGSD.baseStartPos + ClusterManager.Instance.GetStartWorld().WorldOffset;
            var chosen = false;
            for (var y = start.y + 16; y <= start.y + 30 && !chosen; ++y)
            for (var x = start.x - 8; x <= start.x + 8 && !chosen; ++x)
            {
                var clear = true;
                for (var dy = -3; dy <= 4; ++dy) for (var dx = -4; dx <= 6; ++dx)
                {
                    var cell = Grid.XYToCell(x + dx, y + dy); if (!Grid.IsValidCell(cell)) { clear = false; continue; }
                    for (var layer = 0; layer < (int)ObjectLayer.NumLayers; ++layer) if (Grid.Objects[cell, layer] != null) clear = false;
                }
                if (!clear) continue; target = Grid.XYToCell(x, y); chosen = true;
            }
            Require(chosen, "No unoccupied native reference arena is available.");
            var origin = Grid.CellToXY(target); var catalog = CoupledColonySimulation.ReadMaterialCatalog();
            var definitions = new Dictionary<string, (string name, SimHashes hash)>
            {
                ["nitrogen"] = ("Nitrogen", OniNitrogenElements.Gas), ["oxygen"] = ("Oxygen", SimHashes.Oxygen),
                ["carbon_dioxide"] = ("Carbon dioxide", SimHashes.CarbonDioxide), ["steam"] = ("Steam", SimHashes.Steam),
                ["chlorine"] = ("Chlorine", SimHashes.ChlorineGas), ["helium"] = ("Helium", SimHashes.Helium),
                ["hydrogen"] = ("Hydrogen", SimHashes.Hydrogen), ["methane"] = ("Methane", SimHashes.Methane),
                ["water"] = ("Water", SimHashes.Water), ["liquid_nitrogen"] = ("Liquid nitrogen", OniNitrogenElements.Liquid),
                ["liquid_oxygen"] = ("Liquid oxygen", SimHashes.LiquidOxygen)
            };
            var styles = catalog.Materials.Keys.Select(id =>
            {
                var definition = definitions[id]; Color color = ElementLoader.FindElementByHash(definition.hash).substance.colour;
                return new MixtureSpeciesStyle(id, definition.name, new MixtureSrgbColor(color.r, color.g, color.b));
            });
            var model = new MixturePresentationModel(catalog, styles, new AtmosphereSettings(), "oxygen", "carbon_dioxide",
                new Dictionary<string, double> { ["chlorine"] = 1e-9 });
            simulation = CoupledColonySimulation.Create(CoupledPresentationScenario.Import(Grid.WidthInCells, Grid.HeightInCells,
                origin.x, origin.y, ClusterManager.Instance.GetStartWorld().id),
                CoupledPresentationScenario.Configuration(Grid.WidthInCells, Grid.HeightInCells), presentationModel: model);
            held = simulation.AdvanceFrame(1, .03);
            MixturePresentationRuntime.ResetProviders(); MixturePresentationRuntime.BindCompletedSource(() => simulation?.CaptureFrame().Presentation, true);
            var eventId = CellEventLogger.Instance.SandBoxTool;
            for (var dy = -3; dy <= 4; ++dy) for (var dx = -4; dx <= 6; ++dx)
            {
                var cell = Grid.XYToCell(origin.x + dx, origin.y + dy); var wall = dy == -3 || dy == 4 || dx == -4 || dx == 6;
                Grid.Visible[cell] = byte.MaxValue;
                SimMessages.ReplaceElement(cell, wall ? SimHashes.Unobtanium : SimHashes.Oxygen, eventId, wall ? 20000 : 1.2f, 300);
            }
            UnityEngine.Object.FindFirstObjectByType<ColonyValidationDriver>().gameObject.AddComponent<CoupledPresentationValidationLabel>();
            MixtureInspection.CellHoverObserver = ObserveHover;
            ColonyRuntimeValidation.SetStage("preparing-coupled-reference-arena"); WriteReport();
        }
        private static void BeginCase()
        {
            captured = false; observation = null; hoverFrames = 0; lastHoverFrame = -1;
            readyFrame = Time.frameCount + 3; caseStarted = Time.realtimeSinceStartup;
            OverlayScreen.Instance.ToggleOverlay(OverlayModes.None.ID, false);
            var center = Grid.CellToPosCCC(target, Grid.SceneLayer.Background);
            CameraController.Instance.SnapTo(center); CameraController.Instance.SetTargetPos(center, 20, false);
            CameraController.Instance.OrthographicSize = 9;
            ColonyRuntimeValidation.SetStage("waiting-for-coupled-reference-hover"); WriteReport();
        }
        private static void ObserveHover(int cell, MixturePresentation reading, IReadOnlyList<string> contents, bool selected)
        {
            if (!arenaReady || captured || cell != target || Time.frameCount < readyFrame || selected || SelectTool.Instance?.selected != null) return;
            try
            {
                var frame = MixturePresentationRuntime.AcquireFrame(); var physical = held!.SampleCell(cell).PhysicalState;
                Require(frame != null && frame.IsFixture && frame.TryGetCell(cell, out var shared) && ReferenceEquals(reading, shared) &&
                    reading.Revision == held.Projection.Revision && frame.Revision == reading.Revision, "Hover mixed completed publications.");
                Require(reading.TemperatureKelvin == physical.TemperatureKelvin && reading.GasPressurePa == physical.PressurePa &&
                    reading.ThermodynamicReading!.StoredInternalEnergyJoules == physical.InternalEnergyJoules, "Hover reconstructed physical readings.");
                var rows = reading.GasConstituents.Where(row => !row.IsTrace).ToArray();
                Require(rows.Length == 4 && rows.All(row => contents.Any(line => line.StartsWith(row.DisplayName + " " + row.MolePercentText + " by moles", StringComparison.Ordinal))) &&
                    contents.Count(line => line.Contains(" by moles")) == 4, "Actual hover omitted a bulk constituent.");
                Require(reading.GasConstituents.Count == 6 && reading.GasConstituents.Where(row => row.IsTrace).All(row =>
                    !contents.Any(line => line.StartsWith(row.DisplayName + " ", StringComparison.Ordinal))), "Trace rows were lost or shown as bulk gases.");
                Require(reading.Warnings.Any(warning => warning.Contains("Chlorine")), "Transported hazardous trace lacks a warning.");
                if (Time.frameCount != lastHoverFrame) { ++hoverFrames; lastHoverFrame = Time.frameCount; }
                observedGeneration = frame!.PublicationGeneration;
                observation = new
                {
                    Cell = cell, Case = currentCase, UnityFrame = Time.frameCount, Revision = reading.Revision, PublicationGeneration = observedGeneration,
                    held.CompletedFrame, held.SimulationTimeSeconds, PhysicalU = physical.InternalEnergyJoules, physical.PressurePa, physical.TemperatureKelvin,
                    Header = MixtureInspection.FormatHeader(reading), State = MixtureInspection.FormatState(reading), Contents = contents.ToArray(),
                    Warnings = reading.Warnings.ToArray(), AllConstituents = reading.Constituents, NoSelection = true,
                    PatternEnabled = MixturePresentationRuntime.CurrentModel!.VisualOptions.PatternEnabled
                };
            }
            catch (Exception caught) { callbackError = caught.ToString(); }
        }
        internal static void Abort(Exception? cause = null)
        {
            // World generation can shut down an earlier native sim before
            // GameStarted. That is not the end of this unstarted test scene.
            if (!initialized) { ++ignoredStartupShutdowns; return; }
            aborted = !completed; if (cause != null) error = cause.ToString();
            MixtureInspection.CellHoverObserver = null; MixturePresentationRuntime.ClearFixtures(); simulation?.Dispose(); simulation = null;
            if (initialized && aborted) WriteReport();
        }
        private static string Screenshot() => (currentCase + 1).ToString("00") + "-coupled-presentation-" + Names[currentCase] + ".png";
        private static void WriteReport()
        {
            var projected = arenaReady ? CameraController.Instance.baseCamera.WorldToScreenPoint(Grid.CellToPosCCC(target, Grid.SceneLayer.Background)) : Vector3.zero;
            File.WriteAllText(Path.Combine(DirectoryPath, "coupled-presentation.json"), ReportJson.Serialize(new
            {
                Kind = "actual-game-closed-reference-presentation", Platform = Application.platform.ToString(),
                ColonyPhysicsOwner = "native ONI", ReadingPhysicsOwner = CoupledSimulationWorld.ModelIdentity,
                SyntheticSixCellScenario = true, FirstPlayableColonyPassed = false,
                Status = completed ? "passed" : aborted ? "failed" : "pending", Error = error, CallbackError = callbackError,
                Case = currentCase, CaseName = currentCase < Names.Length ? Names[currentCase] : "completed",
                TargetCell = target, ScreenX = projected.x, ScreenYTop = Screen.height - projected.y, ScreenWidth = Screen.width, ScreenHeight = Screen.height,
                Captured = captured, HoverFrames = hoverFrames, Observation = observation, Evidence,
                IgnoredStartupShutdowns = ignoredStartupShutdowns,
                PerCaseDeadlineSeconds = 180, RequiresActualNativeHover = true, RequiresPixelReviewReceipt = true
            }));
        }
        internal static void DrawLabel()
        {
            if (!initialized || completed || aborted) return;
            GUI.Box(new Rect(Screen.width * .18f, 105, Screen.width * .64f, 55),
                "COUPLED REFERENCE TEST · six-cell physical scene\n" + Names[currentCase] + " · surrounding colony physics remains native");
        }
        private static void Require(bool condition, string message) { if (!condition) throw new InvalidDataException(message); }
    }
    public sealed class CoupledPresentationValidationLabel : MonoBehaviour
    {
        private void OnGUI() => CoupledPresentationValidation.DrawLabel();
    }
}
