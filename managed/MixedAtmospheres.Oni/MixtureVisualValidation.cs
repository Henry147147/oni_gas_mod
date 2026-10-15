using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using HarmonyLib;
using MixedAtmospheres.Core;
using MixedAtmospheres.Core.Interop;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // Private, explicit test mode. These conserved inventories are visual
    // fixtures; the disposable colony still runs the native simulation.
    internal static class MixtureVisualValidation
    {
        private sealed class Scenario
        {
            internal string Name = "";
            internal FluidInventory Inventory = FluidInventory.Empty;
            internal int Center;
            internal readonly List<int> Cells = new List<int>();
        }
        private static readonly List<Scenario> Scenarios = new List<Scenario>();
        private static readonly List<object> Evidence = new List<object>();
        private static readonly Dictionary<int, FluidInventory> Cells = new Dictionary<int, FluidInventory>();
        private static readonly Dictionary<KCanvasScaler, float> UiScales = new Dictionary<KCanvasScaler, float>();
        private static readonly List<double> FrameMilliseconds = new List<double>();
        private static FluidMaterialCatalog? catalog;
        private static MixturePresentationModel? model;
        private static ColonyFluidState? state;
        private static byte[] checkpoint = Array.Empty<byte>();
        private static float next;
        private static int step, x0, y0, originalWorld;
        private static Vector3 cameraCenter;
        private static bool initialized;
        private static string caption = "Preparing mixture comparison";
        private static CellSelectionObject? selection;
        private static MixtureContainerValidationReport? containers;
        private static MixtureContainerVisualValidation? spawnedContainers;
        private static NitrogenVisualValidation? nitrogenItems;

        internal static void Tick()
        {
            if (initialized) FrameMilliseconds.Add(Time.unscaledDeltaTime * 1000.0);
            if (Time.realtimeSinceStartup < next) return;
            next = Time.realtimeSinceStartup + 1.5f;
            if (!initialized) { Initialize(); return; }
            var phase = step++;
            switch (phase)
            {
                case 0:
                    SpeedControlScreen.Instance.Pause(false);
                    Focus();
                    Show(OverlayModes.None.ID, "Normal world · ONI colours and subtle composition texture");
                    Select(Scenarios[0].Center);
                    break;
                case 1:
                    ValidateReadings();
                    Capture("01-normal-air-world.png"); break;
                case 2:
                    Select(Scenarios[4].Center);
                    caption = "Trace chlorine · explicit warning and complete composition";
                    break;
                case 3: Capture("02-trace-inspection.png"); break;
                case 4: Show(MixtureOverlays.Id(MixtureOverlayKind.Composition), "Gas composition · mole fractions, with adjustable texture contrast"); break;
                case 5: Capture("03-composition-overlay.png"); break;
                case 6: Show(MixtureOverlays.Id(MixtureOverlayKind.Pressure), "Physical total gas pressure · kPa"); break;
                case 7: Capture("04-pressure-overlay.png"); break;
                case 8: Show(MixtureOverlays.Id(MixtureOverlayKind.OxygenPartialPressure), "Oxygen partial pressure · kPa"); break;
                case 9: Capture("05-oxygen-partial-pressure.png"); break;
                case 10: Show(MixtureOverlays.Id(MixtureOverlayKind.Breathability), "Breathability · all warnings, including imperceptible hazardous traces"); break;
                case 11: Capture("06-breathability-overlay.png"); break;
                case 12:
                    MixtureAppearanceSettings.SetContrast(0, false);
                    Show(OverlayModes.None.ID, "Colour only · physical readings and warnings are unchanged"); break;
                case 13:
                    Require(!MixturePresentationRuntime.AcquireFrame()!.Model.VisualOptions.PatternEnabled, "Colour-only setting was not applied.");
                    Capture("07-colour-only.png"); break;
                case 14:
                    MixtureAppearanceSettings.SetContrast(0.08, false);
                    using (var transaction = state!.BeginTransaction())
                    {
                        foreach (var cell in Scenarios[0].Cells)
                        {
                            transaction.Extract(cell, transaction.SampleCell(cell).Inventory.TotalMassKg, "visual-test-replace");
                            transaction.Inject(cell, Scenarios[4].Inventory, "visual-test-contaminant");
                        }
                        transaction.CommitFrame(2, 0.4);
                    }
                    PublishFixture();
                    Select(Scenarios[0].Center);
                    caption = "Paused composition change · the same cell now contains a hazardous trace";
                    break;
                case 15:
                    Require(MixturePresentationRuntime.TryGetCell(Scenarios[0].Center, out var changed) && changed != null &&
                        (changed.Failures & BreathingFailure.Contaminant) != 0, "Paused composition did not update.");
                    Capture("08-paused-change.png"); break;
                case 16:
                    state = ColonyFluidState.Restore(checkpoint, catalog!);
                    Require(checkpoint.SequenceEqual(state.Save()), "Visual fixture save/reload changed conserved state.");
                    PublishFixture(); MixtureAppearanceSettings.SetContrast(0.04, false);
                    Select(Scenarios[0].Center); caption = "Reloaded conserved state · normal air is breathable again"; break;
                case 17:
                    ValidateReadings(); Capture("09-restored.png"); break;
                case 18:
                    CameraController.Instance.OrthographicSize = 11;
                    Select(Scenarios[4].Center); caption = "Zoomed inspection · trace warnings remain explicit"; break;
                case 19: Capture("10-zoom.png"); break;
                case 20:
                    foreach (var scaler in Resources.FindObjectsOfTypeAll<KCanvasScaler>().Where(value => value.gameObject.activeInHierarchy))
                    { UiScales[scaler] = scaler.GetUserScale(); scaler.SetUserScale(1.25f); }
                    caption = "UI scale 125% · complete mixture inspection"; break;
                case 21: Capture("11-ui-scale-125.png"); break;
                case 22:
                    foreach (var entry in UiScales) if (entry.Key != null) entry.Key.SetUserScale(entry.Value);
                    var other = ClusterManager.Instance.WorldContainers.FirstOrDefault(world => world.id != originalWorld);
                    Require(other != null, "Asteroid-switch test needs a second world.");
                    ClusterManager.Instance.SetActiveWorld(other!.id);
                    caption = "Asteroid switch · the publication retains global cell identities"; break;
                case 23:
                    ClusterManager.Instance.SetActiveWorld(originalWorld); Focus();
                    caption = "Returned to the starting asteroid · world texture bindings restored"; break;
                case 24: ValidateReadings(); Capture("12-returned-world.png"); break;
                case 25:
                    Require(containers != null && containers.Passed, "A container presentation surface failed; inspect container-validation.json.");
                    Require(MixtureWorldRendering.Installed && MixtureWorldRendering.SchedulingActive, "Gas texture bindings are not active.");
                    Require(MixtureWorldRendering.ColourCellsWritten > 0 && MixtureWorldRendering.PressureCellsWritten > 0 &&
                        MixtureWorldRendering.DangerCellsWritten > 0, "Gas texture channels were not written.");
                    Require(string.IsNullOrEmpty(MixtureWorldRendering.LastError), MixtureWorldRendering.LastError);
                    Require(MixtureInspection.CellHoverHookReady && MixtureInspection.DetailsHookReady &&
                        MixtureInspection.DetailDrawCount > 0, "The live game inspection hook was not exercised.");
                    var placements = ContainerPlacements();
                    spawnedContainers = MixtureContainerVisualValidation.Create(placements, Scenarios[4].Inventory, 1);
                    caption = "Preparing six real selected-object inspection surfaces";
                    break;
                case 26:
                    if (!spawnedContainers!.IsReady) { --step; return; }
                    Show(OverlayModes.None.ID, "Real spawned containers · separate, labelled conserved inspection fixtures");
                    var stripCenter = Grid.CellToPosCCC(Grid.XYToCell(x0 + 20, y0 + 23), Grid.SceneLayer.Background);
                    CameraController.Instance.SnapTo(stripCenter);
                    CameraController.Instance.SetTargetPos(stripCenter, 20, false);
                    CameraController.Instance.OrthographicSize = 13;
                    break;
                case 45:
                    spawnedContainers!.Dispose();
                    Focus(); Select(Scenarios[4].Center);
                    caption = "Container adapters removed · original mixture world remains coherent";
                    break;
                case 46:
                    foreach (var surface in spawnedContainers!.Surfaces)
                        Require(!MixturePresentationRuntime.TryResolveInventory(surface.InventoryId, out _), "Destroyed container adapter remains published.");
                    nitrogenItems = new NitrogenVisualValidation(x0 + 4, y0 + 21);
                    WriteReport(); caption = "Native nitrogen phases · real pure-material items"; break;
                case 56:
                    nitrogenItems!.RequireAllPassed();
                    // An interactive run retains its native items so the same
                    // disposable colony can exercise actual Save/Quit/Resume.
                    if (ColonyRuntimeValidation.Settings?.QuitWhenComplete != false) nitrogenItems!.Dispose();
                    WriteReport(); ColonyRuntimeValidation.CompletePresentation(); break;
                default:
                    if (phase >= 47 && phase < 56)
                    {
                        var nitrogenIndex = (phase - 47) / 3;
                        if ((phase - 47) % 3 == 0) nitrogenItems!.Select(nitrogenIndex);
                        else if ((phase - 47) % 3 == 1) Capture((19 + nitrogenIndex).ToString("00") + "-native-nitrogen-phase-" + nitrogenIndex + ".png");
                        else nitrogenItems!.ValidateSelected(nitrogenIndex);
                        break;
                    }
                    if (phase < 27 || phase >= 45) break;
                    var surfaceIndex = (phase - 27) / 3;
                    var selectedSurface = spawnedContainers!.Surfaces[surfaceIndex];
                    if ((phase - 27) % 3 == 0)
                    {
                        spawnedContainers.Select(selectedSurface.Kind);
                        caption = "Selected " + selectedSurface.Kind + " · trace warning and complete measured composition";
                    }
                    else if ((phase - 27) % 3 == 1)
                    {
                        // Preserve the actual failed layout as well as successful
                        // surfaces. Unity completes this capture at frame end,
                        // before the following phase can fail or quit the test.
                        Capture((13 + surfaceIndex).ToString("00") + "-container-" + selectedSurface.Kind + ".png");
                    }
                    else
                    {
                        try { spawnedContainers.ValidateSelected(selectedSurface.Kind); }
                        finally { WriteReport(); }
                    }
                    break;
            }
        }

        private static void Initialize()
        {
            initialized = true;
            MixturePresentationLifecycle.EnableShutdownObservation(ColonyRuntimeValidation.OutputDirectory,
                () => ColonyRuntimeValidation.Settings?.Mode == "presentation", () => MixtureInspection.SessionBindingCount == 0);
            var start = SaveLoader.Instance.cachedGSD.baseStartPos + ClusterManager.Instance.GetStartWorld().WorldOffset;
            x0 = start.x - 20; y0 = start.y + 7;
            originalWorld = ClusterManager.Instance.activeWorldId;
            const int width = 40, height = 18;
            catalog = new FluidMaterialCatalog("mixture-visual-fixtures-v1", new[]
            {
                new FluidMaterial("oxygen", NativeMatterPhase.Gas, .0319988, 658),
                new FluidMaterial("nitrogen", NativeMatterPhase.Gas, .0280134, 743),
                new FluidMaterial("carbon_dioxide", NativeMatterPhase.Gas, .0440095, 655),
                new FluidMaterial("chlorine", NativeMatterPhase.Gas, .070906, 361)
            });
            MixtureSpeciesStyle Style(string id, string name, SimHashes hash)
            {
                // Substance stores Color32 (byte channels), while the shared
                // presentation accepts normalized sRGB components.
                Color color = ElementLoader.FindElementByHash(hash).substance.colour;
                return new MixtureSpeciesStyle(id, name, new MixtureSrgbColor(color.r, color.g, color.b));
            }
            model = new MixturePresentationModel(catalog, new[]
            {
                Style("oxygen", "Oxygen", SimHashes.Oxygen),
                Style("nitrogen", "Nitrogen", OniNitrogenElements.Gas),
                Style("carbon_dioxide", "Carbon dioxide", SimHashes.CarbonDioxide),
                Style("chlorine", "Chlorine", SimHashes.ChlorineGas)
            }, new AtmosphereSettings(), "oxygen", "carbon_dioxide", new Dictionary<string, double> { ["chlorine"] = 1e-6 },
                MixtureAppearanceSettings.Options);
            FluidInventory Gas(double pressure, double oxygen = .21, double co2 = 0, double chlorine = 0)
            {
                var moles = pressure / (FluidMaterialCatalog.GasConstantJoulesPerMolKelvin * 293.15);
                return catalog!.CreateInventory(new Dictionary<string, double>
                {
                    ["oxygen"] = moles * oxygen * .0319988,
                    ["nitrogen"] = moles * (1 - oxygen - co2 - chlorine) * .0280134,
                    ["carbon_dioxide"] = moles * co2 * .0440095,
                    ["chlorine"] = moles * chlorine * .070906
                }, 293.15);
            }
            var names = new[] { "Normal air", "Oxygen rich", "Thin air", "CO₂ accumulation", "Trace chlorine · 2 ppm", "Vacuum" };
            var inventories = new[] { Gas(101325), Gas(101325, .5), Gas(8000), Gas(101325, co2: .02), Gas(101325, chlorine: .000002), FluidInventory.Empty };
            for (var i = 0; i < names.Length; ++i)
            {
                var scenario = new Scenario { Name = names[i], Inventory = inventories[i] };
                var left = x0 + 1 + i % 3 * 13; var bottom = y0 + 1 + (1 - i / 3) * 9;
                for (var y = bottom; y < bottom + 7; ++y)
                for (var x = left; x < left + 12; ++x)
                {
                    var cell = Grid.XYToCell(x, y); scenario.Cells.Add(cell); Cells[cell] = inventories[i];
                }
                scenario.Center = Grid.XYToCell(left + 6, bottom + 3); Scenarios.Add(scenario);
            }
            // Empty disposable chambers use native geometry so the shipped gas
            // renderer, lighting and layers are exercised without solid occlusion.
            var eventId = CellEventLogger.Instance.SandBoxTool;
            for (var y = y0; y < y0 + height + 1; ++y)
            for (var x = x0; x < x0 + width + 1; ++x)
            {
                var cell = Grid.XYToCell(x, y); Grid.Visible[cell] = byte.MaxValue;
                var inside = Cells.ContainsKey(cell);
                SimMessages.ReplaceElement(cell, inside ? SimHashes.Oxygen : SimHashes.Unobtanium, eventId, inside ? 1.2f : 20000, 293.15f);
            }
            // A separate supported strip is reserved before pausing. Existing
            // objects are never removed; native placement preflight later
            // rejects any occupied footprint or utility port.
            for (var y = y0 + 20; y <= y0 + 26; ++y)
            for (var x = x0; x <= x0 + width; ++x)
            {
                var cell = Grid.XYToCell(x, y); Grid.Visible[cell] = byte.MaxValue;
                var wall = y == y0 + 20 || y == y0 + 26 || x == x0 || x == x0 + width;
                SimMessages.ReplaceElement(cell, wall ? SimHashes.Unobtanium : SimHashes.Oxygen, eventId, wall ? 20000 : 1.2f, 293.15f);
            }
            var raw = VacuumWorld(width, height, x0, y0);
            var import = NativeColonyImport.StageNewColony(Grid.WidthInCells, Grid.HeightInCells,
                new[] { new NativeWorldPlacement(0, width, height, x0, y0) }, new[] { new NativeWorldSource(0, raw) },
                new Dictionary<int, NativeMatterPhase> { [(int)SimHashes.Vacuum] = NativeMatterPhase.Vacuum },
                new Dictionary<int, NativeFluidBinding>(), (int)SimHashes.Vacuum);
            state = ColonyFluidState.Import(import, catalog);
            using (var transaction = state.BeginTransaction())
            {
                foreach (var cell in Cells.Where(value => value.Value.TotalMassKg > 0)) transaction.Inject(cell.Key, cell.Value, "visual-fixture");
                transaction.CommitFrame(1, .2);
            }
            checkpoint = state.Save(); File.WriteAllBytes(Path.Combine(ColonyRuntimeValidation.OutputDirectory, "visual-fixture-state.bin"), checkpoint);
            MixturePresentationRuntime.Configure(model); PublishFixture();
            cameraCenter = Grid.CellToPosCCC(Grid.XYToCell(x0 + 20, y0 + 9), Grid.SceneLayer.Background);
            Focus();
            var driver = UnityEngine.Object.FindFirstObjectByType<ColonyValidationDriver>();
            driver.gameObject.AddComponent<MixtureVisualValidationLabels>();
            // Game-prefab surface contracts, without spawning machines or
            // injecting fictitious packets into native conduit inventories.
            var owners = new Dictionary<string, GameObject>
            {
                ["pipe"] = Assets.GetPrefab(GasConduitConfig.ID),
                ["reservoir"] = Assets.GetPrefab(GasReservoirConfig.ID),
                ["bottle"] = Assets.GetPrefab(SimHashes.Oxygen.CreateTag()),
                ["suit"] = Assets.GetPrefab(AtmoSuitConfig.ID),
                ["vent"] = Assets.GetPrefab(GasVentConfig.ID),
                ["machine"] = Assets.GetPrefab(ElectrolyzerConfig.ID)
            };
            driver.StartCoroutine(MixtureInspectionValidation.ValidateContainerSurfaces(owners, inventories[4], 1,
                result => { containers = result; File.WriteAllText(Path.Combine(ColonyRuntimeValidation.OutputDirectory, "container-validation.json"), ReportJson.Serialize(result)); }));
            ColonyRuntimeValidation.SetStage("rendering-mixture-fixtures");
            next = Time.realtimeSinceStartup + 3;
        }

        private static void PublishFixture()
        {
            var frame = state!.CaptureSnapshot();
            // Explicit vacuum entries are meaningful owned cells too.
            foreach (var scenario in Scenarios)
                foreach (var cell in scenario.Cells) Cells[cell] = frame.SampleCell(cell).Inventory;
            MixturePresentationRuntime.EnableFixtureFrame(Grid.WidthInCells, Grid.HeightInCells, Cells);
        }
        private static void Focus()
        {
            CameraController.Instance.SnapTo(cameraCenter);
            CameraController.Instance.SetTargetPos(cameraCenter, 20, false);
            CameraController.Instance.OrthographicSize = 17;
        }
        private static IReadOnlyDictionary<string, int> ContainerPlacements()
        {
            var placements = new Dictionary<string, int>
            {
                ["pipe"] = Grid.XYToCell(x0 + 3, y0 + 21), ["reservoir"] = Grid.XYToCell(x0 + 8, y0 + 21),
                ["bottle"] = Grid.XYToCell(x0 + 15, y0 + 21), ["suit"] = Grid.XYToCell(x0 + 21, y0 + 21),
                ["vent"] = Grid.XYToCell(x0 + 26, y0 + 21), ["machine"] = Grid.XYToCell(x0 + 33, y0 + 21)
            };
            bool Clear(int cell)
            {
                if (Grid.Solid[cell]) return false;
                for (var layer = 0; layer < (int)ObjectLayer.NumLayers; ++layer)
                    if (Grid.Objects[cell, layer] != null) return false;
                return true;
            }
            // Natural debris can occupy an otherwise cleared disposable test
            // strip. Choose a nearby empty footprint; never destroy an object
            // or override the native placement checks to manufacture success.
            foreach (var kind in placements.Keys.ToArray())
            {
                var anchor = placements[kind]; var found = false;
                foreach (var offset in new[] { 0, 1, -1, 2, -2 })
                {
                    placements[kind] = anchor + offset;
                    IReadOnlyDictionary<string, IReadOnlyList<int>> required;
                    try { required = MixtureContainerVisualValidation.RequiredCells(placements); }
                    catch (InvalidOperationException error) when (error.Message.StartsWith("Fixture footprints or ports overlap", StringComparison.Ordinal)) { continue; }
                    if (!required[kind].All(Clear)) continue;
                    found = true; break;
                }
                Require(found, "No unoccupied native footprint for disposable " + kind + ".");
            }
            var cells = MixtureContainerVisualValidation.RequiredCells(placements);
            File.WriteAllText(Path.Combine(ColonyRuntimeValidation.OutputDirectory, "container-placement.json"), ReportJson.Serialize(
                cells.Select(entry => new { Kind = entry.Key, Anchor = placements[entry.Key], Cells = entry.Value.Select(cell => new
                { Cell = cell, X = cell % Grid.WidthInCells, Y = cell / Grid.WidthInCells, Solid = Grid.Solid[cell],
                    Element = Grid.Element[cell]?.id.ToString(), MassKg = Grid.Mass[cell] }).ToArray() }).ToArray()));
            return placements;
        }
        private static void Show(HashedString overlay, string text)
        { OverlayScreen.Instance.ToggleOverlay(overlay, false); caption = text; }
        private static void Select(int cell)
        {
            if (selection == null) selection = Resources.FindObjectsOfTypeAll<CellSelectionObject>().First(value => value.gameObject.activeInHierarchy && value.Selectable != null);
            selection.transform.SetPosition(Grid.CellToPosCCC(cell, Grid.SceneLayer.WorldSelection));
            selection.mouseCell = cell;
            SelectTool.Instance.Select(selection.Selectable, true);
            AccessTools.Field(typeof(CellSelectionObject), "selectedCell").SetValue(selection, cell);
            selection.UpdateValues();
        }
        private static void ValidateReadings()
        {
            var expected = new[] { BreathingFailure.None, BreathingFailure.OxygenFractionHigh, BreathingFailure.OxygenPartialPressureLow,
                BreathingFailure.CarbonDioxideFractionHigh | BreathingFailure.CarbonDioxidePartialPressureHigh, BreathingFailure.Contaminant, BreathingFailure.Vacuum };
            for (var i = 0; i < Scenarios.Count; ++i)
            {
                Require(MixturePresentationRuntime.TryGetCell(Scenarios[i].Center, out var reading) && reading != null, "Missing fixture reading.");
                Require(reading!.Failures == expected[i], Scenarios[i].Name + " breathing assessment differs: " + reading.Failures);
                Require(reading.Constituents.Count == Scenarios[i].Inventory.MassesKg.Count, "Constituent dropped from reading.");
            }
            var plain = MixturePresentationRuntime.AcquireFrame()!;
            plain.TryGetCell(Scenarios[0].Center, out var normal); plain.TryGetCell(Scenarios[4].Center, out var trace);
            Require(Math.Abs(normal!.BaseColor.R - trace!.BaseColor.R) < .001 && trace.Warnings.Count > 0,
                "Imperceptible trace should retain an explicit warning independently of colour.");
        }
        private static void Capture(string name)
        {
            ColonyRuntimeValidation.CaptureScreenshot(name);
            var frame = MixturePresentationRuntime.AcquireFrame()!;
            var labels = Resources.FindObjectsOfTypeAll<DetailLabel>()
                .Where(label => label.gameObject.name.StartsWith("MixedAtmospheres.Cell.", StringComparison.Ordinal))
                .OrderBy(label => label.transform.GetSiblingIndex())
                .Select(label =>
                {
                    var text = typeof(DetailLabel).GetField("label")!.GetValue(label);
                    var corners = new Vector3[4];
                    var rect = label.transform as RectTransform;
                    if (rect != null) rect.GetWorldCorners(corners);
                    return new
                    {
                        Name = label.gameObject.name, label.gameObject.activeSelf, label.gameObject.activeInHierarchy,
                        Sibling = label.transform.GetSiblingIndex(), Parent = label.transform.parent?.name,
                        Text = (string?)text?.GetType().GetProperty("text")?.GetValue(text) ?? "",
                        Bottom = corners.Min(point => point.y), Top = corners.Max(point => point.y),
                        Height = rect?.rect.height ?? 0
                    };
                }).ToArray();
            // Model checks alone cannot detect native cached-label ordering or
            // clipping. Inspect the actual active Unity labels after a layout
            // frame, including a safe -> hazardous transition and UI scaling.
            if (name == "02-trace-inspection.png" || name == "08-paused-change.png" ||
                name == "10-zoom.png" || name == "11-ui-scale-125.png")
            {
                MixturePresentation? selected = null;
                Require(selection != null && frame.TryGetCell(selection.SelectedCell, out selected) && selected != null,
                    "Selected mixture has no completed-frame reading.");
                var density = labels.Single(label => label.Name == "MixedAtmospheres.Cell.GasDensity" && label.activeInHierarchy);
                var composition = labels.Single(label => label.Name == "MixedAtmospheres.Cell.Composition" && label.activeInHierarchy);
                for (var i = 0; i < selected!.Warnings.Count; ++i)
                {
                    var warning = labels.SingleOrDefault(label => label.Name == "MixedAtmospheres.Cell.Warning." + i && label.activeInHierarchy);
                    Require(warning != null && warning.Text == selected.Warnings[i] && warning.Height > 0 &&
                        warning.Sibling < density.Sibling && warning.Sibling < composition.Sibling,
                        "The live warning label is missing, stale or below the composition table.");
                }
            }
            Evidence.Add(new { Screenshot = name, UnityFrame = Time.frameCount, frame.PublicationGeneration, frame.Revision,
                Paused = SpeedControlScreen.Instance.IsPaused, ActiveWorld = ClusterManager.Instance.activeWorldId,
                Overlay = OverlayScreen.Instance.GetMode().ToString(), Contrast = frame.Model.VisualOptions.PatternContrast,
                PatternEnabled = frame.Model.VisualOptions.PatternEnabled, ScreenWidth = Screen.width, ScreenHeight = Screen.height,
                OrthographicSize = CameraController.Instance.OrthographicSize, DetailLabels = labels });
            WriteReport();
        }
        private static void WriteReport()
        {
            var frame = MixturePresentationRuntime.AcquireFrame();
            var timings = FrameMilliseconds.OrderBy(value => value).ToArray();
            double Percentile(double percentile) => timings.Length == 0 ? 0 : timings[Math.Min(timings.Length - 1, (int)(timings.Length * percentile))];
            File.WriteAllText(Path.Combine(ColonyRuntimeValidation.OutputDirectory, "mixture-visual-validation.json"), ReportJson.Serialize(new
            {
                Kind = "actual-Unity-rendered-explicit-conserved-mixture-fixtures", Platform = Application.platform.ToString(),
                PhysicsOwner = "native ONI; visual fixture inventories are independent", FirstPlayableColonyPassed = false,
                Scenarios = Scenarios.Select(value => new { value.Name, value.Center,
                    Reading = frame != null && frame.TryGetCell(value.Center, out var reading) ? reading : null }).ToArray(),
                Evidence, Containers = containers, SpawnedContainerSurfaces = spawnedContainers?.Surfaces,
                SpawnedContainerEvidence = spawnedContainers?.Evidence,
                MixtureWorldRendering.Installed, MixtureWorldRendering.SchedulingActive,
                MixtureWorldRendering.TextureBatches, MixtureWorldRendering.TextureWrites, MixtureWorldRendering.MaximumTextureBatchMs,
                MixtureWorldRendering.LastError, MixtureInspection.CellHoverHookReady, MixtureInspection.DetailsHookReady,
                MixtureInspection.HoverDrawCount, MixtureInspection.DetailDrawCount,
                PerformanceScope = "504 explicit conserved presentation cells and up to six registered visual inventories; mostly paused native surrounding colony; includes screenshots and UI transitions; no baseline subtraction; not sustained 1x evidence",
                FrameSamples = timings.Length, FrameP50Ms = Percentile(.5), FrameP95Ms = Percentile(.95), FrameP99Ms = Percentile(.99)
            }));
        }
        internal static void DrawLabels()
        {
            if (!initialized || !MixturePresentationRuntime.IsFixture || CameraController.Instance?.baseCamera == null) return;
            var previous = GUI.color;
            var style = new GUIStyle(GUI.skin.box) { fontSize = 18, alignment = TextAnchor.MiddleCenter, wordWrap = true };
            GUI.Box(new Rect(Screen.width / 2f - 470, 100, 940, 56), "MIXTURE PRESENTATION TEST · disposable colony\n" + caption, style);
            style.fontSize = 16;
            if (step >= 47) { GUI.color = previous; return; }
            if (spawnedContainers != null && step >= 27 && step <= 46)
            {
                foreach (var surface in spawnedContainers.Surfaces)
                {
                    var pos = CameraController.Instance.baseCamera.WorldToScreenPoint(Grid.CellToPosCCC(surface.Cell, Grid.SceneLayer.Background));
                    GUI.Box(new Rect(pos.x - 95, Screen.height - pos.y - 130, 190, 50), surface.Kind + "\ninspection fixture", style);
                }
                GUI.color = previous; return;
            }
            foreach (var scenario in Scenarios)
            {
                var pos = CameraController.Instance.baseCamera.WorldToScreenPoint(Grid.CellToPosCCC(scenario.Center, Grid.SceneLayer.Background));
                GUI.Box(new Rect(pos.x - 150, Screen.height - pos.y - 100, 300, 54), scenario.Name, style);
            }
            GUI.color = previous;
        }
        private static byte[] VacuumWorld(int width, int height, int x, int y)
        {
            var count = (width + 2) * (height + 2);
            using var memory = new MemoryStream(); using var writer = new BinaryWriter(memory, Encoding.UTF8, true);
            writer.Write(29 + 36 * count); writer.Write(new byte[] { 83, 73, 77, 83, 65, 86, 69, 0 }); writer.Write(15);
            writer.Write(width + 2); writer.Write(height + 2); writer.Write(x); writer.Write(y); writer.Write((byte)0);
            for (var i = 0; i < count; ++i) { writer.Write((int)SimHashes.Vacuum); writer.Write(0f); writer.Write(0f); writer.Write(0f); }
            for (var i = 0; i < count; ++i) { writer.Write(0); writer.Write(0); }
            for (var i = 0; i < count; ++i) { writer.Write((int)SimHashes.Vacuum); writer.Write(0f); writer.Write(0f); }
            return memory.ToArray();
        }
        private static void Require(bool condition, string message) { if (!condition) throw new InvalidDataException(message); }
    }

    public sealed class MixtureVisualValidationLabels : MonoBehaviour
    {
        private void OnGUI() => MixtureVisualValidation.DrawLabels();
    }
}
