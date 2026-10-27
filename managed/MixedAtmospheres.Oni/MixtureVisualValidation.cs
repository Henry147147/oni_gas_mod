using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using HarmonyLib;
using Klei.CustomSettings;
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
        // Software renderers (Linux llvmpipe) can take seconds per frame, so
        // wall-clock pacing alone may leave one frame per step. A probe write
        // is fresh only after a texture batch follows the last republish.
        private const int MinimumFramesPerStep = 3;
        private static int nextFrame;
        private static int step, x0, y0, originalWorld;
        private static Vector3 cameraCenter;
        private static bool initialized;
        private static string caption = "Preparing mixture comparison";
        private static CellSelectionObject? selection;
        private static MixtureContainerValidationReport? containers;
        private static MixtureContainerVisualValidation? spawnedContainers;
        private static OwnedReservoirVisualValidation? ownedReservoirs;
        private static readonly Dictionary<string, FluidContainerHandle> OwnedHandles = new Dictionary<string, FluidContainerHandle>(StringComparer.Ordinal);
        private static readonly List<object> ContainerConservation = new List<object>();
        private static readonly List<object> ContainerCoreAgreement = new List<object>();
        private static ColonyFrameSnapshot? containerBaseline;
        private static FluidContainerHandle retiredBottle;
        private static readonly List<object> AppearanceCases = new List<object>();
        private static readonly List<MixtureWorldRendering.TextureProbeEvidence> TextureProbeEvidence = new List<MixtureWorldRendering.TextureProbeEvidence>();
        private static readonly List<object> TextureProbeRearms = new List<object>();
        private static readonly List<object> AppearanceComparisons = new List<object>();
        private static MixtureWorldRendering.TextureProbeEvidence? equalMassTextureBefore;
        private static int appearanceStep;
        private static string appearanceCase = "";
        private static BreathingFailure appearanceFailures;
        private static bool appearanceLiquid;
        private static FluidInventory? equalMassBefore, equalMassAfter;
        private static string appearanceCheckpoint = "", appearanceCheckpointSha256 = "";
        private static int appearanceNativeElement;
        private static float appearanceNativeMass, appearanceNativeTemperature;
        private static bool appearanceRestored;

        internal static void Tick()
        {
            try { TickCore(); }
            catch { MixtureWorldRendering.DisableValidationProbes(); throw; }
        }

        private static void TickCore()
        {
            if (initialized) FrameMilliseconds.Add(Time.unscaledDeltaTime * 1000.0);
            if (Time.realtimeSinceStartup < next || Time.frameCount < nextFrame) return;
            next = Time.realtimeSinceStartup + 1.5f;
            nextFrame = Time.frameCount + MinimumFramesPerStep;
            if (!initialized) { Initialize(); return; }
            // The container-surface coroutine registers and unregisters six
            // inventories, republishing each time. Finish it before the first
            // texture acceptance instead of relying on elapsed seconds.
            if (step == 0 && containers == null) return;
            // OnReset/reallocation invalidates the exact destination buffers.
            // Re-arm only in this explicit private driver, before advancing its
            // next phase, and wait for a fresh completed renderer batch.
            if (step > 0 && step <= 25 && step != 16 && step != 23 && !MixtureWorldRendering.ValidationProbesActive)
            { EnableTextureProbes("property-texture reset before phase " + step); return; }
            var phase = step++;
            switch (phase)
            {
                case 0:
                    SpeedControlScreen.Instance.Pause(false);
                    Focus();
                    Show(OverlayModes.None.ID, "Normal world · ONI colours and subtle composition texture");
                    Select(Scenarios[0].Center);
                    EnableTextureProbes("initial known-cell acceptance");
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
                    MixturePresentationRuntime.ResetProviders(); MixturePresentationRuntime.Configure(model!);
                    PublishFixture(); MixtureAppearanceSettings.SetContrast(0.04, false);
                    EnableTextureProbes("exact checkpoint/provider restore");
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
                    EnableTextureProbes("explicit return to original asteroid");
                    caption = "Returned to the starting asteroid · world texture bindings restored"; break;
                case 24: ValidateReadings(); Capture("12-returned-world.png"); break;
                case 25:
                    if (!TickAdditionalAppearanceCases()) { --step; return; }
                    MixtureWorldRendering.DisableValidationProbes();
                    Require(containers != null && containers.Passed, "A container presentation surface failed; inspect container-validation.json.");
                    Require(MixtureWorldRendering.Installed && MixtureWorldRendering.SchedulingActive, "Gas texture bindings are not active.");
                    Require(MixtureWorldRendering.ColourCellsWritten > 0 && MixtureWorldRendering.PressureCellsWritten > 0 &&
                        MixtureWorldRendering.DangerCellsWritten > 0, "Gas texture channels were not written.");
                    Require(string.IsNullOrEmpty(MixtureWorldRendering.LastError), MixtureWorldRendering.LastError);
                    Require(MixtureInspection.CellHoverHookReady && MixtureInspection.DetailsHookReady &&
                        MixtureInspection.DetailDrawCount > 0, "The live game inspection hook was not exercised.");
                    containerBaseline = state!.CaptureSnapshot();
                    using (var transfer = state.BeginTransaction())
                    {
                        var kinds = new[] { "pipe", "reservoir", "bottle", "suit", "vent", "machine" };
                        for (var i = 0; i < kinds.Length; ++i)
                        {
                            var handle = transfer.CreateContainer("conserved-visual-container:" + kinds[i], new FluidContainerDefinition(1, 10, FluidContainerPhases.Gas));
                            OwnedHandles.Add(kinds[i], handle);
                            transfer.TransferAllCellToContainer(Scenarios[4].Cells[i], handle);
                        }
                        transfer.CommitFrame(3, .6);
                    }
                    CheckContainerConservation("six world cells transferred into six containers"); PublishFixture();
                    caption = "Preparing six real selected-object inspection surfaces";
                    break;
                case 26:
                    if (spawnedContainers == null)
                    {
                        spawnedContainers = MixtureContainerVisualValidation.CreateOwned(ContainerPlacements(), OwnedHandles);
                        --step; return;
                    }
                    if (!spawnedContainers!.IsReady) { --step; return; }
                    Show(OverlayModes.None.ID, "Real spawned containers · one conserved snapshot with the world cells");
                    var stripCenter = Grid.CellToPosCCC(Grid.XYToCell(x0 + 20, y0 + 23), Grid.SceneLayer.Background);
                    CameraController.Instance.SnapTo(stripCenter);
                    CameraController.Instance.SetTargetPos(stripCenter, 20, false);
                    CameraController.Instance.OrthographicSize = 13;
                    break;
                case 45:
                    using (var transfer = state!.BeginTransaction())
                    {
                        transfer.TransferContainerToContainer(OwnedHandles["reservoir"], OwnedHandles["bottle"], "oxygen", .1);
                        transfer.CommitFrame(4, .8);
                    }
                    CheckContainerConservation("oxygen transferred from reservoir to bottle"); PublishFixture();
                    spawnedContainers!.Select("bottle"); caption = "Conserved transfer · bottle oxygen fraction and pressure have increased";
                    break;
                case 46: Capture("22-container-selective-transfer.png"); break;
                case 47: ValidateOwnedContainer("bottle"); spawnedContainers!.ValidateSelected("bottle"); WriteReport(); break;
                case 48:
                    var containerCheckpoint = state!.Save();
                    state = ColonyFluidState.Restore(containerCheckpoint, catalog!);
                    Require(containerCheckpoint.SequenceEqual(state.Save()), "Owned-container checkpoint changed state or allocation identity.");
                    File.WriteAllBytes(Path.Combine(ColonyRuntimeValidation.OutputDirectory, "owned-container-fixture-state.bin"), containerCheckpoint);
                    CheckContainerConservation("custom checkpoint restored cells and containers"); PublishFixture();
                    caption = "Custom checkpoint restored · exact container bindings and trace quantities retained";
                    break;
                case 49: ValidateOwnedContainer("bottle"); spawnedContainers!.ValidateSelected("bottle"); Capture("23-container-checkpoint-restore.png"); break;
                case 50:
                    retiredBottle = OwnedHandles["bottle"];
                    using (var replacement = state!.BeginTransaction())
                    {
                        replacement.RetireContainerToCell(retiredBottle, Scenarios[4].Cells[2]);
                        var nextBottle = replacement.CreateContainer(retiredBottle.Id, new FluidContainerDefinition(1, 10, FluidContainerPhases.Gas));
                        replacement.TransferAllCellToContainer(Scenarios[0].Cells[0], nextBottle);
                        OwnedHandles["bottle"] = nextBottle; replacement.CommitFrame(5, 1);
                    }
                    CheckContainerConservation("retired allocation evacuated and ID reused with normal air"); PublishFixture();
                    caption = "Retired bottle binding · explicit unavailable status until the new allocation is bound";
                    break;
                case 51: Capture("24-container-stale-binding.png"); break;
                case 52:
                    Require(!MixturePresentationRuntime.TryResolveInventory(retiredBottle, out _), "The retired bottle handle resolved its replacement.");
                    spawnedContainers!.ValidateUnavailable("bottle"); WriteReport(); break;
                case 53:
                    spawnedContainers!.Rebind("bottle", OwnedHandles["bottle"], false);
                    // A late cleanup for the old allocation must preserve the new binding.
                    MixtureInspection.UnregisterContainerOwner(spawnedContainers.Objects["bottle"], retiredBottle);
                    MixtureInspection.UnregisterInventoryOwner(spawnedContainers.Objects["bottle"], retiredBottle.Id);
                    spawnedContainers.Select("bottle"); caption = "Replacement bottle · breathable air, with no stale trace warning";
                    break;
                case 54: Capture("25-container-replacement.png"); break;
                case 55: ValidateOwnedContainer("bottle", requireBreathable: true); spawnedContainers!.ValidateSelected("bottle"); WriteReport(); break;
                case 56:
                    spawnedContainers!.Dispose();
                    using (var evacuation = state!.BeginTransaction())
                    {
                        var index = 0;
                        foreach (var handle in OwnedHandles.Values) evacuation.RetireContainerToCell(handle, Scenarios[4].Cells[index++]);
                        evacuation.CommitFrame(6, 1.2);
                    }
                    CheckContainerConservation("all owned containers evacuated and retired"); PublishFixture();
                    Focus(); Select(Scenarios[4].Center);
                    caption = "Containers evacuated and retired · all constituents remain in the world";
                    break;
                case 57:
                    foreach (var surface in spawnedContainers!.Surfaces)
                        Require(!MixturePresentationRuntime.TryResolveInventory(surface.InventoryId, out _), "Destroyed container adapter remains published.");
                    step = 67; // The removed custom-element inspection stages are retired.
                    caption = "Preparing actual isolated reservoir ownership"; WriteReport(); break;
                case 68:
                    if (ownedReservoirs == null)
                    {
                        var placements = new Dictionary<string, int>
                        { ["reservoir-a"] = Grid.XYToCell(x0 + 8, y0 + 29), ["reservoir-b"] = Grid.XYToCell(x0 + 26, y0 + 29) };
                        bool Clear(int cell)
                        {
                            if (Grid.Solid[cell]) return false;
                            for (var layer = 0; layer < (int)ObjectLayer.NumLayers; ++layer) if (Grid.Objects[cell, layer] != null) return false;
                            return true;
                        }
                        var candidates = new List<object>(); var foundPair = false;
                        foreach (var columnA in Enumerable.Range(3, 35).OrderBy(column => Math.Abs(column - 8)))
                        {
                            placements["reservoir-a"] = Grid.XYToCell(x0 + columnA, y0 + 29);
                            foreach (var columnB in Enumerable.Range(3, 35).OrderBy(column => Math.Abs(column - 26)))
                            {
                                if (Math.Abs(columnA - columnB) < 6) continue;
                                placements["reservoir-b"] = Grid.XYToCell(x0 + columnB, y0 + 29);
                                var required = MixtureContainerVisualValidation.RequiredReservoirPairCells(placements);
                                var blockers = required.Values.SelectMany(cells => cells).Distinct().Where(cell => !Clear(cell)).ToArray();
                                candidates.Add(new { ColumnA = columnA, ColumnB = columnB, Blockers = blockers.Select(cell => new
                                { Cell = cell, X = cell % Grid.WidthInCells, Y = cell / Grid.WidthInCells, Solid = Grid.Solid[cell],
                                    Element = Grid.Element[cell]?.id.ToString(), MassKg = Grid.Mass[cell],
                                    Objects = Enumerable.Range(0, (int)ObjectLayer.NumLayers).Where(layer => Grid.Objects[cell, layer] != null)
                                        .Select(layer => new { Layer = layer, Name = Grid.Objects[cell, layer].name, Instance = Grid.Objects[cell, layer].GetInstanceID() }).ToArray() }).ToArray() });
                                if (blockers.Length != 0) continue;
                                foundPair = true; break;
                            }
                            if (foundPair) break;
                        }
                        File.WriteAllText(Path.Combine(ColonyRuntimeValidation.OutputDirectory, "owned-reservoir-placement.json"), ReportJson.Serialize(
                            new { Found = foundPair, Placements = placements, Candidates = candidates }));
                        Require(foundPair, "No unoccupied native reservoir pair footprint is available; inspect owned-reservoir-placement.json.");
                        ownedReservoirs = new OwnedReservoirVisualValidation(state!, catalog!, MixturePresentationRuntime.CurrentModel ?? model!, placements, Scenarios[4].Cells);
                        var ownedCenter = Grid.CellToPosCCC(Grid.XYToCell(x0 + 20, y0 + 31), Grid.SceneLayer.Background);
                        CameraController.Instance.SnapTo(ownedCenter); CameraController.Instance.SetTargetPos(ownedCenter, 20, false);
                        CameraController.Instance.OrthographicSize = 13;
                        --step; break;
                    }
                    var ownedDone = ownedReservoirs.Tick(); caption = ownedReservoirs.Caption;
                    if (ownedDone) { WriteReport(); ColonyRuntimeValidation.CompletePresentation(); }
                    else --step;
                    break;
                default:
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
                        try { ValidateOwnedContainer(selectedSurface.Kind); spawnedContainers.ValidateSelected(selectedSurface.Kind); }
                        finally { WriteReport(); }
                    }
                    break;
            }
        }

        private static void Initialize()
        {
            initialized = true;
            // This disposable development colony must not earn achievements
            // from native factory calls or from its deliberately edited arena.
            Game.Instance.debugWasUsed = true;
            CustomGameSettings.Instance.SetQualitySetting(CustomGameSettingConfigs.SandboxMode, "Enabled");
            Game.Instance.SandboxModeActive = true;
            MixturePresentationLifecycle.EnableShutdownObservation(ColonyRuntimeValidation.OutputDirectory,
                () => ColonyRuntimeValidation.Settings?.Mode == "presentation", () => MixtureInspection.SessionBindingCount == 0);
            var start = SaveLoader.Instance.cachedGSD.baseStartPos + ClusterManager.Instance.GetStartWorld().WorldOffset;
            x0 = start.x - 20; y0 = start.y + 7;
            originalWorld = ClusterManager.Instance.activeWorldId;
            const int width = 40, height = 18;
            catalog = new FluidMaterialCatalog("mixture-visual-fixtures-v2", new[]
            {
                new FluidMaterial("oxygen", NativeMatterPhase.Gas, .0319988, 658),
                new FluidMaterial("nitrogen", NativeMatterPhase.Gas, .0280134, 743),
                new FluidMaterial("carbon_dioxide", NativeMatterPhase.Gas, .0440095, 655),
                new FluidMaterial("chlorine", NativeMatterPhase.Gas, .070906, 361),
                // Explicit constant-density display fixture only. This is no
                // production water EOS or owned native liquid geometry.
                new FluidMaterial("water", NativeMatterPhase.Liquid, .01801528, 4184, liquidDensityKgPerCubicMetre: 1000)
            });
            MixtureSpeciesStyle Style(string id, string name, SimHashes hash)
            {
                // Substance stores Color32 (byte channels), while the shared
                // presentation accepts normalized sRGB components.
                Color color = hash == SimHashes.Vacuum ? new Color(.65f, .65f, .8f) : (Color)ElementLoader.FindElementByHash(hash).substance.colour;
                return new MixtureSpeciesStyle(id, name, new MixtureSrgbColor(color.r, color.g, color.b));
            }
            model = new MixturePresentationModel(catalog, new[]
            {
                Style("oxygen", "Oxygen", SimHashes.Oxygen),
                Style("nitrogen", "Nitrogen", SimHashes.Vacuum),
                Style("carbon_dioxide", "Carbon dioxide", SimHashes.CarbonDioxide),
                Style("chlorine", "Chlorine", SimHashes.ChlorineGas),
                Style("water", "Water", SimHashes.Water)
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
            foreach (var lane in new[] { 20, 28 })
            for (var y = y0 + lane; y <= y0 + lane + 6; ++y)
            for (var x = x0; x <= x0 + width; ++x)
            {
                var cell = Grid.XYToCell(x, y); Grid.Visible[cell] = byte.MaxValue;
                var wall = y == y0 + lane || y == y0 + lane + 6 || x == x0 || x == x0 + width;
                SimMessages.ReplaceElement(cell, wall ? SimHashes.Unobtanium : SimHashes.Oxygen, eventId, wall ? 20000 : 1.2f, 293.15f);
            }
            var raw = FixtureWorld(width, height, x0, y0);
            var import = NativeColonyImport.StageNewColony(Grid.WidthInCells, Grid.HeightInCells,
                new[] { new NativeWorldPlacement(0, width, height, x0, y0) }, new[] { new NativeWorldSource(0, raw) },
                new Dictionary<int, NativeMatterPhase> { [(int)SimHashes.Vacuum] = NativeMatterPhase.Vacuum, [(int)SimHashes.Unobtanium] = NativeMatterPhase.Solid },
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
            MixturePresentationRuntime.PublishConservedFixture(frame);
        }

        private static void EnableTextureProbes(string reason)
        {
            Require(TextureProbeRearms.Count < 12, "Repeated texture reallocation exceeded the private probe re-arm bound.");
            var frame = MixturePresentationRuntime.AcquireFrame()!;
            MixtureWorldRendering.EnableValidationProbes(frame, Scenarios.Select(scenario => scenario.Center).ToArray(), explicitPrivateOptIn: true);
            TextureProbeRearms.Add(new { Reason = reason, frame.UnityFrame, frame.Revision, frame.PublicationGeneration,
                ProbeEpoch = MixtureWorldRendering.ValidationProbeEpoch });
        }

        private static FluidInventory AdditionalGas(double pressure, double oxygen, double co2 = 0)
        {
            var moles = pressure / (FluidMaterialCatalog.GasConstantJoulesPerMolKelvin * 293.15);
            return catalog!.CreateInventory(new Dictionary<string, double>
            {
                ["oxygen"] = moles * oxygen * catalog.Materials["oxygen"].MolarMassKgPerMol,
                ["nitrogen"] = moles * (1 - oxygen - co2) * catalog.Materials["nitrogen"].MolarMassKgPerMol,
                ["carbon_dioxide"] = moles * co2 * catalog.Materials["carbon_dioxide"].MolarMassKgPerMol
            }, 293.15);
        }

        private static FluidInventory EqualMassComposition(FluidMaterialCatalog materials, FluidInventory before)
        {
            var mass = before.TotalMassKg;
            var oxygenMass = mass * materials.Materials["oxygen"].MolarMassKgPerMol /
                (materials.Materials["oxygen"].MolarMassKgPerMol + materials.Materials["nitrogen"].MolarMassKgPerMol);
            var result = materials.CreateInventory(new Dictionary<string, double>
            { ["oxygen"] = oxygenMass, ["nitrogen"] = mass - oxygenMass }, 293.15);
            Require(result.TotalMassKg == mass, "Equal-mass fixture construction changed its total mass.");
            return result;
        }

        private static FluidInventory GasLiquidProjection(FluidMaterialCatalog materials, FluidInventory air)
        {
            var entries = air.MassesKg.ToDictionary(entry => entry.Key, entry => entry.Value * .5, StringComparer.Ordinal);
            entries.Add("water", 500); return materials.CreateInventory(entries, 293.15);
        }

        private static void SetAdditionalAppearanceCase(string key, string name, FluidInventory inventory, BreathingFailure failures, bool liquid = false)
        {
            var before = state!.CaptureSnapshot();
            using (var transaction = state.BeginTransaction())
            {
                foreach (var cell in Scenarios[0].Cells)
                {
                    var previous = transaction.SampleCell(cell).Inventory;
                    if (previous.TotalMassKg > 0) transaction.Extract(cell, previous.TotalMassKg, "appearance-fixture-replacement");
                    transaction.Inject(cell, inventory, "appearance-fixture-input");
                }
                transaction.CommitFrame(before.CompletedFrame + 1, before.SimulationTimeSeconds + .2);
            }
            appearanceCase = name; appearanceFailures = failures; appearanceLiquid = liquid;
            Scenarios[0].Name = name;
            PublishFixture(); Show(OverlayModes.None.ID, name + (liquid ? " · reading-only fixture; native liquid surface unowned" : " · explicit conserved presentation fixture"));
            Focus(); Select(Scenarios[0].Center);
            var center = Scenarios[0].Center;
            appearanceNativeElement = (int)Grid.Element[center].id; appearanceNativeMass = Grid.Mass[center]; appearanceNativeTemperature = Grid.Temperature[center];
            var bytes = state.Save(); appearanceCheckpoint = "appearance-case-" + key + ".bin";
            File.WriteAllBytes(Path.Combine(ColonyRuntimeValidation.OutputDirectory, appearanceCheckpoint), bytes);
            using var hash = SHA256.Create(); appearanceCheckpointSha256 = BitConverter.ToString(hash.ComputeHash(bytes)).Replace("-", "").ToLowerInvariant();
        }

        private static bool TickAdditionalAppearanceCases()
        {
            // Repeat phase25 while this bounded six-capture sequence runs. All
            // old arena coordinates, container phases and screenshot names are
            // retained. Temporary inputs/outputs use the explicit fixture
            // ledgers; these replacements are not an internal-transfer claim.
            switch (appearanceStep++)
            {
                case 0:
                    SetAdditionalAppearanceCase("oxygen-fraction-low", "Low oxygen fraction · adequate O2 partial pressure",
                        AdditionalGas(200000, .10), BreathingFailure.OxygenFractionLow); break;
                case 1: Capture("29-appearance-oxygen-fraction-low.png"); break;
                case 2:
                    SetAdditionalAppearanceCase("co2-fraction-only", "CO2 fraction failure only",
                        AdditionalGas(55000, .30, .006), BreathingFailure.CarbonDioxideFractionHigh); break;
                case 3: Capture("30-appearance-co2-fraction-only.png"); break;
                case 4:
                    SetAdditionalAppearanceCase("co2-pressure-only", "CO2 partial-pressure failure only",
                        AdditionalGas(200000, .21, .004), BreathingFailure.CarbonDioxidePartialPressureHigh); break;
                case 5: Capture("31-appearance-co2-pressure-only.png"); break;
                case 6:
                    equalMassBefore = Scenarios[0].Inventory;
                    SetAdditionalAppearanceCase("equal-mass-before", "Equal mass composition · before",
                        equalMassBefore, BreathingFailure.None); break;
                case 7: Capture("32-appearance-equal-mass-before.png"); break;
                case 8:
                    equalMassAfter = EqualMassComposition(catalog!, equalMassBefore!);
                    SetAdditionalAppearanceCase("equal-mass-after", "Equal mass composition · after, oxygen rich",
                        equalMassAfter, BreathingFailure.OxygenFractionHigh); break;
                case 9: Capture("33-appearance-equal-mass-after.png"); break;
                case 10:
                    SetAdditionalAppearanceCase("gas-liquid", "Gas + liquid · phase-separated readout",
                        GasLiquidProjection(catalog!, Scenarios[0].Inventory), BreathingFailure.None, liquid: true); break;
                case 11: Capture("34-appearance-gas-liquid.png"); break;
                case 12:
                    MixtureWorldRendering.DisableValidationProbes();
                    state = ColonyFluidState.Restore(checkpoint, catalog!);
                    Require(checkpoint.SequenceEqual(state.Save()), "Additional appearance sequence did not restore the exact fixture checkpoint.");
                    appearanceRestored = true; appearanceCase = ""; Scenarios[0].Name = "Normal air";
                    MixturePresentationRuntime.ResetProviders(); MixturePresentationRuntime.Configure(model!);
                    PublishFixture(); MixtureAppearanceSettings.SetContrast(.04, false); EnableTextureProbes("additional appearance baseline/provider restore");
                    Focus(); Select(Scenarios[0].Center); caption = "Original fixture restored · existing container acceptance follows";
                    break;
                default:
                    Require(appearanceRestored && AppearanceCases.Count == 6, "Additional appearance acceptance did not complete all six cases.");
                    ValidateReadings(); WriteReport(); return true;
            }
            return false;
        }

        private static object ValidateAdditionalAppearanceCase(RuntimeMixturePresentationFrame frame)
        {
            var source = state!.CaptureSnapshot(); var expected = source.SampleCell(Scenarios[0].Center);
            Require(frame.TryGetCell(Scenarios[0].Center, out var actual) && actual != null, "Additional appearance reading is unavailable.");
            var equalMass = !ReferenceEquals(equalMassBefore, null) && !ReferenceEquals(equalMassAfter, null) && equalMassBefore!.TotalMassKg == equalMassAfter!.TotalMassKg;
            var passed = actual!.Revision == source.Revision && actual.TotalMassKg == expected.Inventory.TotalMassKg &&
                actual.TemperatureKelvin == expected.TemperatureKelvin && actual.TotalPressurePa == expected.GasPressurePa &&
                actual.Failures == appearanceFailures && actual.Constituents.Count == expected.Inventory.MassesKg.Count &&
                actual.Constituents.All(row => expected.Inventory.MassesKg.TryGetValue(row.MaterialId, out var mass) && mass == row.MassKg);
            var nativeCellUnchanged = (int)Grid.Element[Scenarios[0].Center].id == appearanceNativeElement &&
                Grid.Mass[Scenarios[0].Center] == appearanceNativeMass && Grid.Temperature[Scenarios[0].Center] == appearanceNativeTemperature;
            if (appearanceLiquid)
            {
                var water = actual.Constituents.Single(row => row.MaterialId == "water");
                passed &= water.Phase == NativeMatterPhase.Liquid && water.GasMoleFraction == null && water.PartialPressurePa == null &&
                    water.MassKg == 500 && actual.LiquidVolumeFraction == .5 && actual.GasVolumeCubicMetres == .5;
            }
            if (appearanceCase == "Equal mass composition · after, oxygen rich")
                passed &= equalMass && !actual.IsBreathable;
            var result = new
            {
                Name = appearanceCase, Cell = Scenarios[0].Center, frame.UnityFrame, frame.PublicationGeneration, frame.Revision,
                ExpectedRevision = source.Revision, ExpectedFailures = appearanceFailures.ToString(), ActualFailures = actual.Failures.ToString(),
                actual.TotalMassKg, actual.GasMassKg, actual.GasVolumeCubicMetres, actual.GasDensityKgPerCubicMetre,
                actual.TemperatureKelvin, actual.TotalPressurePa, actual.LiquidVolumeFraction, actual.Warnings,
                Constituents = actual.Constituents, CheckpointFile = appearanceCheckpoint, CheckpointSha256 = appearanceCheckpointSha256,
                EqualMassBeforeKg = equalMassBefore?.TotalMassKg, EqualMassAfterKg = equalMassAfter?.TotalMassKg,
                EqualMassPairExact = equalMass, NativeKnownCellUnchanged = nativeCellUnchanged,
                NativeKnownElement = appearanceNativeElement, NativeKnownMassKg = appearanceNativeMass, NativeKnownTemperatureKelvin = appearanceNativeTemperature,
                GasLiquidScope = appearanceLiquid ? "Read-only phase projection; native liquid geometry and fluid physics remain unowned." : "Gas-only presentation fixture.",
                Passed = passed && nativeCellUnchanged
            };
            AppearanceCases.Add(result);
            if (!result.Passed) WriteReport();
            Require(result.Passed, "Additional appearance case differs from exact Core state or its isolated expected failure: " + appearanceCase);
            return result;
        }

        private static void CheckContainerConservation(string stage)
        {
            var before = containerBaseline ?? throw new InvalidOperationException("Container baseline is missing.");
            var after = state!.CaptureSnapshot(); var expected = before.Totals(); var actual = after.Totals();
            var massResiduals = catalog!.Materials.Keys.ToDictionary(id => id, id =>
                (actual.MassesKg.TryGetValue(id, out var a) ? a : 0) - (expected.MassesKg.TryGetValue(id, out var b) ? b : 0), StringComparer.Ordinal);
            bool Close(double a, double b) => Math.Abs(a - b) <= 2e-12 * Math.Max(Math.Abs(a), Math.Abs(b));
            bool EqualInventory(FluidInventory a, FluidInventory b) => a.TotalEnergyJoules == b.TotalEnergyJoules &&
                a.MomentumX == b.MomentumX && a.MomentumY == b.MomentumY && a.RadiationQuantity == b.RadiationQuantity &&
                a.MassesKg.Count == b.MassesKg.Count && a.MassesKg.All(pair => b.MassesKg.TryGetValue(pair.Key, out var mass) && mass == pair.Value) &&
                a.DiseaseCounts.Count == b.DiseaseCounts.Count && a.DiseaseCounts.All(pair => b.DiseaseCounts.TryGetValue(pair.Key, out var count) && count == pair.Value);
            var conserved = expected.MassesKg.Count == actual.MassesKg.Count && expected.MassesKg.All(pair =>
                actual.MassesKg.TryGetValue(pair.Key, out var mass) && mass > 0 && Close(pair.Value, mass)) &&
                Close(expected.TotalEnergyJoules, actual.TotalEnergyJoules) && Close(expected.MomentumX, actual.MomentumX) &&
                Close(expected.MomentumY, actual.MomentumY) && Close(expected.RadiationQuantity, actual.RadiationQuantity) &&
                expected.DiseaseCounts.Count == actual.DiseaseCounts.Count && expected.DiseaseCounts.All(pair =>
                    actual.DiseaseCounts.TryGetValue(pair.Key, out var count) && Close(pair.Value, count)) && EqualInventory(before.SuppliedInventory, after.SuppliedInventory) &&
                EqualInventory(before.RemovedInventory, after.RemovedInventory) && before.ExternalHeatJoules == after.ExternalHeatJoules;
            ContainerConservation.Add(new { Stage = stage, after.Revision, Containers = after.Containers.Count,
                TotalMassKg = actual.TotalMassKg, TotalEnergyJoules = actual.TotalEnergyJoules, MassResiduals = massResiduals,
                EnergyResidualJoules = actual.TotalEnergyJoules - expected.TotalEnergyJoules, Conserved = conserved });
            Require(conserved, "World/container conservation or external ledger changed during " + stage);
        }

        private static void ValidateOwnedContainer(string kind, bool requireBreathable = false)
        {
            var snapshot = state!.CaptureSnapshot(); var handle = OwnedHandles[kind];
            var expected = snapshot.SampleContainer(handle); var frame = MixturePresentationRuntime.AcquireFrame();
            MixturePresentation? actual = null;
            var resolved = frame != null && frame.TryResolveInventory(handle, out actual) && actual != null;
            // Compare with the authority, independently of UI rows generated
            // from the runtime view. A self-consistent stale frame must fail.
            var passed = resolved && frame!.Revision == snapshot.Revision && actual!.Revision == snapshot.Revision &&
                actual.TotalMassKg == expected.Inventory.TotalMassKg && actual.TemperatureKelvin == expected.TemperatureKelvin &&
                actual.TotalPressurePa == expected.GasPressurePa && actual.Constituents.Count == expected.Inventory.MassesKg.Count &&
                actual.Constituents.All(row => expected.Inventory.MassesKg.TryGetValue(row.MaterialId, out var mass) && mass == row.MassKg) &&
                (!requireBreathable || (actual.Failures == BreathingFailure.None && actual.Warnings.Count == 0 &&
                    actual.Constituents.All(row => row.MaterialId != "chlorine")));
            ContainerCoreAgreement.Add(new { Kind = kind, UnityFrame = Time.frameCount, ExpectedRevision = snapshot.Revision,
                ActualRevision = actual?.Revision, Handle = handle, ExpectedMassKg = expected.Inventory.TotalMassKg,
                ActualMassKg = actual?.TotalMassKg, ExpectedPressurePa = expected.GasPressurePa, ActualPressurePa = actual?.TotalPressurePa,
                ExpectedTemperatureKelvin = expected.TemperatureKelvin, ActualTemperatureKelvin = actual?.TemperatureKelvin,
                RequireBreathable = requireBreathable, ActualFailures = actual?.Failures.ToString(), ActualWarningCount = actual?.Warnings.Count, Passed = passed });
            Require(passed, "Container presentation differs from its completed authority or retains a stale breathing warning: " + kind);
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
            MixtureWorldRendering.TextureProbeEvidence? textureProof = null;
            object? additionalAppearance = null;
            var screenshotNumber = int.Parse(name.Substring(0, 2), CultureInfo.InvariantCulture);
            if (screenshotNumber <= 12 || screenshotNumber >= 29 && screenshotNumber <= 34)
            {
                Require(selection != null, "Texture acceptance needs an actual selected known cell.");
                textureProof = MixtureWorldRendering.CaptureValidationProbe(frame, selection!.SelectedCell);
                Require(TextureProbeEvidence.Count < 32, "Bounded texture evidence is full.");
                TextureProbeEvidence.Add(textureProof);
                if (screenshotNumber >= 29) additionalAppearance = ValidateAdditionalAppearanceCase(frame);
                if (screenshotNumber == 32) equalMassTextureBefore = textureProof;
                if (screenshotNumber == 33 || screenshotNumber == 34)
                {
                    var before = equalMassTextureBefore ?? throw new InvalidOperationException("Equal-mass baseline texture observation is missing.");
                    var colorBefore = before.Channels[0]; var colorAfter = textureProof.Channels[0];
                    var sameColor = colorBefore.ReadBackBytes.SequenceEqual(colorAfter.ReadBackBytes);
                    var sameOpacity = before.Channels[1].ReadBackBytes.SequenceEqual(textureProof.Channels[1].ReadBackBytes);
                    var expectedDanger = screenshotNumber == 33 ? byte.MaxValue : (byte)0;
                    var relation = new
                    {
                        Kind = screenshotNumber == 33 ? "equal-total-mass-composition-change" : "gas-liquid-gas-channel-separation",
                        BeforeRevision = before.Revision, AfterRevision = textureProof.Revision,
                        BeforeMassKg = colorBefore.TotalMassKg, AfterMassKg = colorAfter.TotalMassKg,
                        BeforeGasDensity = colorBefore.GasDensityKgPerCubicMetre, AfterGasDensity = colorAfter.GasDensityKgPerCubicMetre,
                        BeforePressurePa = colorBefore.PhysicalPressurePa, AfterPressurePa = colorAfter.PhysicalPressurePa,
                        ColorUnchanged = sameColor, DensityOpacityUnchanged = sameOpacity,
                        BeforeDanger = before.Channels[2].ReadBackBytes[0], AfterDanger = textureProof.Channels[2].ReadBackBytes[0],
                        Passed = before.Passed && textureProof.Passed && sameOpacity && colorBefore.GasDensityKgPerCubicMetre == colorAfter.GasDensityKgPerCubicMetre &&
                            before.Channels[2].ReadBackBytes[0] == 0 && textureProof.Channels[2].ReadBackBytes[0] == expectedDanger &&
                            (screenshotNumber == 33
                                ? colorBefore.TotalMassKg == colorAfter.TotalMassKg && !sameColor && colorBefore.PhysicalPressurePa != colorAfter.PhysicalPressurePa
                                : sameColor && colorAfter.LiquidVolumeFraction == .5 && colorAfter.GasVolumeCubicMetres == .5)
                    };
                    AppearanceComparisons.Add(relation);
                    if (!relation.Passed) WriteReport();
                    Require(relation.Passed, "Actual texture relationships differ for " + relation.Kind);
                }
            }
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
            object? warningLabelProof = null, liquidLabelProof = null;
            var warningLabelsPassed = true; var liquidLabelsPassed = true;
            // Model checks alone cannot detect native cached-label ordering or
            // clipping. Inspect the actual active Unity labels after a layout
            // frame, including a safe -> hazardous transition and UI scaling.
            if (name == "02-trace-inspection.png" || name == "08-paused-change.png" ||
                name == "10-zoom.png" || name == "11-ui-scale-125.png" || screenshotNumber >= 29 && screenshotNumber <= 34)
            {
                MixturePresentation? selected = null;
                var resolved = selection != null && frame.TryGetCell(selection.SelectedCell, out selected) && selected != null;
                var densities = labels.Where(label => label.Name == "MixedAtmospheres.Cell.GasDensity" && label.activeInHierarchy).ToArray();
                var compositions = labels.Where(label => label.Name == "MixedAtmospheres.Cell.Composition" && label.activeInHierarchy).ToArray();
                var warnings = labels.Where(label => label.Name.StartsWith("MixedAtmospheres.Cell.Warning.", StringComparison.Ordinal) && label.activeInHierarchy).ToArray();
                warningLabelsPassed = resolved && densities.Length == 1 && compositions.Length == 1 && warnings.Length == selected!.Warnings.Count;
                for (var i = 0; resolved && i < selected!.Warnings.Count; ++i)
                {
                    warningLabelsPassed &= i < warnings.Length && warnings[i].Name == "MixedAtmospheres.Cell.Warning." + i &&
                        warnings[i].Text == selected.Warnings[i] && warnings[i].Height > 0 &&
                        (i == 0 || warnings[i - 1].Sibling < warnings[i].Sibling) && densities.Length == 1 && compositions.Length == 1 &&
                        warnings[i].Sibling < densities[0].Sibling && warnings[i].Sibling < compositions[0].Sibling;
                }
                warningLabelProof = new { Resolved = resolved, ExpectedWarnings = selected?.Warnings, ActualWarnings = warnings,
                    GasDensityLabels = densities.Length, CompositionLabels = compositions.Length, Passed = warningLabelsPassed };
            }
            if (screenshotNumber == 34)
            {
                var waters = labels.Where(label => label.Name == "MixedAtmospheres.Cell.Constituent.water" && label.activeInHierarchy).ToArray();
                var volumes = labels.Where(label => label.Name == "MixedAtmospheres.Cell.LiquidVolume" && label.activeInHierarchy).ToArray();
                liquidLabelsPassed = waters.Length == 1 && waters[0].Text.Contains("Liquid constituent") && waters[0].Text.Contains("not applicable") &&
                    volumes.Length == 1 && volumes[0].Text.Contains("50%");
                liquidLabelProof = new { WaterLabels = waters, LiquidVolumeLabels = volumes, Passed = liquidLabelsPassed };
            }
            Evidence.Add(new { Screenshot = name, UnityFrame = Time.frameCount, frame.PublicationGeneration, frame.Revision,
                Paused = SpeedControlScreen.Instance.IsPaused, ActiveWorld = ClusterManager.Instance.activeWorldId,
                Overlay = OverlayScreen.Instance.GetMode().ToString(), Contrast = frame.Model.VisualOptions.PatternContrast,
                PatternEnabled = frame.Model.VisualOptions.PatternEnabled, ScreenWidth = Screen.width, ScreenHeight = Screen.height,
                OrthographicSize = CameraController.Instance.OrthographicSize, DetailLabels = labels, TextureProof = textureProof,
                AdditionalAppearance = additionalAppearance, WarningLabelProof = warningLabelProof, LiquidLabelProof = liquidLabelProof });
            WriteReport();
            Require(textureProof == null || textureProof.Passed,
                "Actual gas texture bytes, destination, pinned reading or completed-batch coherence differ for " + name);
            Require(warningLabelsPassed, "The live warning labels are missing, stale, duplicated or below the composition table.");
            Require(liquidLabelsPassed, "Actual gas/liquid UI omitted its separate liquid row, declared volume or invented liquid gas pressure.");
        }
        private static void WriteReport()
        {
            var frame = MixturePresentationRuntime.AcquireFrame();
            var timings = FrameMilliseconds.OrderBy(value => value).ToArray();
            double Percentile(double percentile) => timings.Length == 0 ? 0 : timings[Math.Min(timings.Length - 1, (int)(timings.Length * percentile))];
            File.WriteAllText(Path.Combine(ColonyRuntimeValidation.OutputDirectory, "mixture-visual-validation.json"), ReportJson.Serialize(new
            {
                Kind = "actual-Unity-rendered-explicit-conserved-mixture-fixtures", Platform = Application.platform.ToString(),
                PhysicsOwner = "native ONI; explicit custom cells and containers share one conserved presentation snapshot", FirstPlayableColonyPassed = false,
                Scenarios = Scenarios.Select(value => new { value.Name, value.Center,
                    Reading = frame != null && frame.TryGetCell(value.Center, out var reading) ? reading : null }).ToArray(),
                Evidence, Containers = containers, SpawnedContainerSurfaces = spawnedContainers?.Surfaces,
                SpawnedContainerEvidence = spawnedContainers?.Evidence,
                ContainerAllocationEvidence = spawnedContainers?.AllocationEvidence, ContainerConservation, ContainerCoreAgreement,
                AppearanceCases, AppearanceComparisons, TextureProbeEvidence, TextureProbeRearms, AppearanceFixtureRestored = appearanceRestored,
                AppearanceSettings = frame?.Model.VisualOptions, BreathingSettings = frame?.Model.BreathingSettings,
                Styles = frame?.Model.Styles.Values,
                MixtureWorldRendering.Installed, MixtureWorldRendering.SchedulingActive,
                MixtureWorldRendering.TextureBatches, MixtureWorldRendering.TextureWrites, MixtureWorldRendering.MaximumTextureBatchMs,
                MixtureWorldRendering.LastError, MixtureInspection.CellHoverHookReady, MixtureInspection.DetailsHookReady,
                MixtureInspection.HoverDrawCount, MixtureInspection.DetailDrawCount,
                PerformanceScope = "504 explicit conserved presentation cells and up to six owned containers; mostly paused native surrounding colony; includes private bounded CPU texture probes, screenshots and UI transitions; no baseline subtraction; not sustained 1x evidence",
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
            if (step >= 58) { GUI.color = previous; return; }
            if (spawnedContainers != null && step >= 27 && step <= 57)
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
        private static byte[] FixtureWorld(int width, int height, int x, int y)
        {
            var count = (width + 2) * (height + 2);
            using var memory = new MemoryStream(); using var writer = new BinaryWriter(memory, Encoding.UTF8, true);
            writer.Write(29 + 36 * count); writer.Write(new byte[] { 83, 73, 77, 83, 65, 86, 69, 0 }); writer.Write(15);
            writer.Write(width + 2); writer.Write(height + 2); writer.Write(x); writer.Write(y); writer.Write((byte)0);
            for (var row = 0; row < height + 2; ++row)
            for (var column = 0; column < width + 2; ++column)
            {
                var fluid = Cells.ContainsKey(Grid.XYToCell(x + column - 1, y + row - 1));
                writer.Write((int)(fluid ? SimHashes.Vacuum : SimHashes.Unobtanium));
                writer.Write(fluid ? 0f : 293.15f); writer.Write(fluid ? 0f : 20000f); writer.Write(0f);
            }
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
