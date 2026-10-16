using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using static MixedAtmospheres.Oni.OniNativeDomain;
using HarmonyLib;
using Klei.CustomSettings;
using MixedAtmospheres.Core;
using MixedAtmospheres.Core.Interop;
using Newtonsoft.Json;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // A real, bounded native physical owner inside a disposable ONI colony.
    // The surrounding colony stays native. This sealed development domain is
    // intentionally inaccessible to actors until their adapters are implemented.
    internal static unsafe class NativeSealedDomainValidation
    {
        private const int Width = 3, Height = 2;
        private static readonly string[] Ids = { "nitrogen", "oxygen", "helium", "water" };
        private static bool prepared, active, arming, completed, failed;
        private static int x, y, target, hotSolid, coldSolid, preparedFrame, publications;
        private static float started, nextReport;
        private static int rendererWaitFrame = -1;
        private static long textureWriteBaseline;
        private static float initialHot, initialCold;
        private static readonly Dictionary<int, (ushort element, int mass, int temperature, byte disease, int count)> nativeBaseline =
            new Dictionary<int, (ushort, int, int, byte, int)>();
        private static OwnedGameDataSnapshot? currentSnapshot;
        private static FluidMaterialCatalog? catalog;
        private static Status latest;
        private static Cell[] cells = new Cell[Width * Height];
        private static string error = "";
        private static ulong renderedRevision;
        private static bool retainedFrameVerified, nativeCellsUnchanged = true, nativeSolidsAdvanced, nativeSnapshotVerified;
        private static bool nativeFlowControlObserved, projectionPrimed, writingProjection;
        private static bool nativeFlowFenceVerified;
        private static bool nativeRendererAdvanced, saveGuardVerified;
        private static bool gameGridProjectionVerified;
        private static ulong projectionPrimedAtFrame;
        private static readonly byte NativeFlowMask = (byte)(Sim.Cell.Properties.GasImpermeable | Sim.Cell.Properties.LiquidImpermeable);
        private static float[] nativeProjectionMasses = Array.Empty<float>();
        private static float[] nativeProjectionTemperatures = Array.Empty<float>();
        private static IReadOnlyDictionary<int, MixtureThermodynamicReading>? publishedReadings;
        private static string ReportPath => Path.Combine(ColonyRuntimeValidation.OutputDirectory, "native-domain.json");
        internal static bool Active => active;
        internal static bool SaveBlocked => active || arming;
        internal static bool IsMode => ColonyRuntimeValidation.Settings?.Mode == "native-domain" ||
            ColonyRuntimeValidation.Settings?.Mode == "native-liquid-domain";
        private static bool Liquid => ColonyRuntimeValidation.Settings?.Mode == "native-liquid-domain";

        internal static void Tick()
        {
            if (completed || failed) return;
            try
            {
                if (!prepared) { Prepare(); return; }
                if (!active)
                {
                    Require(Time.realtimeSinceStartup - started < 90, "Sealed native arena did not finish preparation.");
                    if (arming) return;
                    if (Time.frameCount - preparedFrame < 30 || !BoundaryReady()) return;
                    Start(); return;
                }
                Capture();
                Require(latest.State == 1, "Custom native worker fault: " + latest.Error);
                Require(BoundaryReady(projectionPrimed), "The sealed domain boundary changed or an unbound actor entered it.");
                nativeSolidsAdvanced = Grid.Temperature[hotSolid] < initialHot && Grid.Temperature[coldSolid] > initialCold;
                if (Time.realtimeSinceStartup >= nextReport) { nextReport = Time.realtimeSinceStartup + 1; WriteReport(); }
                Require(projectionPrimed || latest.CompletedFrames <= 10, "Native projection bootstrap did not retain its requested fluid cells.");
                if (!projectionPrimed || latest.CompletedFrames - projectionPrimedAtFrame < (ulong)ColonyRuntimeValidation.Settings!.CompletedFrames || publications < 2) return;
                if (rendererWaitFrame < 0)
                {
                    rendererWaitFrame = Time.frameCount; SpeedControlScreen.Instance.Pause(false); return;
                }
                if (Time.frameCount - rendererWaitFrame < 3) return;
                var view = MixturePresentationRuntime.AcquireFrame(); MixturePresentation? reading = null;
                Require(view != null && !view.IsFixture && view.TryGetCell(target, out reading) &&
                    reading!.Revision == (long)renderedRevision && reading.ThermodynamicReading != null,
                    "Actual presentation did not consume a completed native domain frame.");
                Require(publishedReadings != null && ReferenceEquals(reading!.ThermodynamicReading, publishedReadings[target]),
                    "Presentation did not retain the supplied native physical reading.");
                gameGridProjectionVerified = Enumerable.Range(0, cells.Length).All(index =>
                {
                    var global = Grid.XYToCell(x + index % Width, y + index / Width);
                    return Math.Abs(Grid.Mass[global] - (float)cells[index].TotalMass) <= 1e-6 &&
                        Math.Abs(Grid.Temperature[global] - (float)cells[index].Temperature) <= 1e-4 &&
                        Grid.Element[global].id == (Liquid ? SimHashes.Water : SimHashes.Oxygen);
                });
                Require(gameGridProjectionVerified, "Actual game Grid fields did not consume the physical domain projection.");
                nativeRendererAdvanced = MixtureWorldRendering.TextureWrites > textureWriteBaseline &&
                    MixtureWorldRendering.LastTexturePublicationGeneration == view!.PublicationGeneration &&
                    string.IsNullOrEmpty(MixtureWorldRendering.LastError);
                Require(nativeRendererAdvanced, "The actual game texture jobs did not render the completed native domain frame.");
                Require(latest.SolverThread != latest.StartThread && latest.SolverThread != 0,
                    "Physical solver did not run on the native simulation worker.");
                Require(nativeSolidsAdvanced, "Native solid-only heat conduction stopped during custom fluid ownership.");
                Require(nativeSnapshotVerified && nativeCellsUnchanged && retainedFrameVerified && nativeFlowControlObserved && projectionPrimed,
                    "Native compatibility cells or retained completed frame changed unexpectedly.");
                Require(latest.SuppressedPostProcess > 0 && latest.SuppressedTemperature > 0 && nativeFlowFenceVerified,
                    "Native fluid phases were not demonstrably excluded from the bounded domain.");
                Require(latest.MaximumRelativeMassError <= 1e-8 && latest.MaximumRelativeEnergyError <= 1e-6,
                    "Custom fluid conservation exceeded its fixed bounds.");
                Require(Liquid ? latest.LiquidMassKg == 1100 && Math.Abs(latest.PressurePa - 2.2e8) < 1 :
                    latest.CurrentOxygenKg != latest.InitialOxygenKg, "Custom fluid state did not exercise its intended physics.");
                VerifySaveGuard();
                completed = true; NativeBackendObservation.Flush(); WriteReport();
                ColonyRuntimeValidation.CaptureScreenshot("native-domain.png");
                ColonyRuntimeValidation.CompletePresentation("actual-game-sealed-native-fluid-domain-validation");
            }
            catch (Exception caught) { Fail(caught); }
        }
        private static void Prepare()
        {
            Require(NativeBackendObservation.Enabled, "Native domain requires an explicitly enabled native backend.");
            Require(Marshal.SizeOf<Configuration>() == 40 && Marshal.SizeOf<Status>() == 440 && Marshal.SizeOf<Cell>() == 88,
                "Native domain ABI layout mismatch.");
            Require(Version() == 1, "Unsupported native domain ABI.");
            prepared = true; preparedFrame = Time.frameCount; started = Time.realtimeSinceStartup;
            Game.Instance.debugWasUsed = true;
            CustomGameSettings.Instance.SetQualitySetting(CustomGameSettingConfigs.SandboxMode, "Enabled");
            Game.Instance.SandboxModeActive = true;
            var start = SaveLoader.Instance.cachedGSD.baseStartPos + ClusterManager.Instance.GetStartWorld().WorldOffset;
            var chosen = false;
            for (var yy = start.y + 16; yy <= start.y + 30 && !chosen; ++yy)
            for (var xx = start.x - 8; xx <= start.x + 8 && !chosen; ++xx)
            {
                var clear = true;
                for (var dy = -1; dy <= Height; ++dy) for (var dx = -1; dx <= Width + 3; ++dx)
                {
                    var index = Grid.XYToCell(xx + dx, yy + dy);
                    if (!Grid.IsValidCell(index)) { clear = false; continue; }
                    for (var layer = 0; layer < (int)ObjectLayer.NumLayers; ++layer)
                        if (Grid.Objects[index, layer] != null) clear = false;
                }
                if (clear) { x = xx; y = yy; chosen = true; }
            }
            Require(chosen, "No unoccupied sealed native arena is available.");
            target = Grid.XYToCell(x, y); hotSolid = Grid.XYToCell(x + Width + 2, y); coldSolid = hotSolid + 1;
            var eventId = CellEventLogger.Instance.SandBoxTool;
            for (var dy = -1; dy <= Height; ++dy) for (var dx = -1; dx <= Width; ++dx)
            {
                var index = Grid.XYToCell(x + dx, y + dy); var wall = dx == -1 || dx == Width || dy == -1 || dy == Height;
                Grid.Visible[index] = byte.MaxValue;
                SimMessages.ReplaceElement(index, wall ? SimHashes.Unobtanium : Liquid ? SimHashes.Water : SimHashes.Oxygen,
                    eventId, wall ? 20000 : ProjectionMass(dx), wall ? 300 : ProjectionTemperature(dx));
            }
            SimMessages.ReplaceElement(hotSolid, SimHashes.Granite, eventId, 1000, 350);
            SimMessages.ReplaceElement(coldSolid, SimHashes.Granite, eventId, 1000, 250);
            SpeedControlScreen.Instance.Unpause(false);
            UnityEngine.Object.FindFirstObjectByType<ColonyValidationDriver>().gameObject.AddComponent<NativeSealedDomainLabel>();
            ColonyRuntimeValidation.SetStage("preparing-sealed-native-domain"); WriteReport();
        }
        private static bool BoundaryReady(bool requireFluidIdentity = true)
        {
            for (var dy = -1; dy <= Height; ++dy) for (var dx = -1; dx <= Width; ++dx)
            {
                var index = Grid.XYToCell(x + dx, y + dy); var wall = dx == -1 || dx == Width || dy == -1 || dy == Height;
                if (Grid.Solid[index] != wall || (wall && Grid.Element[index].id != SimHashes.Unobtanium) ||
                    (!wall && requireFluidIdentity && Grid.Element[index].id != (Liquid ? SimHashes.Water : SimHashes.Oxygen))) return false;
                if (!wall)
                {
                    if (Grid.DiseaseCount[index] != 0 || Grid.Radiation[index] != 0) return false;
                    for (var layer = 0; layer < (int)ObjectLayer.NumLayers; ++layer)
                        if (Grid.Objects[index, layer] != null) throw new InvalidOperationException("An actor entered the unbound native domain.");
                }
            }
            return Grid.Element[hotSolid].id == SimHashes.Granite && Grid.Element[coldSolid].id == SimHashes.Granite;
        }
        private static void Start()
        {
            // First establish that vanilla would move this intentionally
            // unbalanced scalar state. The same projection is then restored
            // under exclusions and must remain byte-identical thereafter.
            nativeFlowControlObserved = Enumerable.Range(0, Width * Height).Any(index =>
                Math.Abs(Grid.Mass[Grid.XYToCell(x + index % Width, y + index / Width)] - ProjectionMass(index % Width)) > .001);
            Require(nativeFlowControlObserved, "Unhooked native pressure-gradient control did not flow.");
            var materials = new List<FluidMaterial>(); var styles = new List<MixtureSpeciesStyle>();
            var names = new[] { "Nitrogen", "Oxygen", "Helium", "Water" };
            var hashes = new[] { OniNitrogenElements.Gas, SimHashes.Oxygen, SimHashes.Helium, SimHashes.Water };
            for (uint k = 0; k < Ids.Length; ++k)
            {
                Check(Definition(k, out var value));
                materials.Add(new FluidMaterial(Ids[k], k == 3 ? NativeMatterPhase.Liquid : NativeMatterPhase.Gas,
                    value.MolarMass, value.Cv, value.Density, value.Offset, 1, 1e6));
                Color color = ElementLoader.FindElementByHash(hashes[k]).substance.colour;
                styles.Add(new MixtureSpeciesStyle(Ids[k], names[k], new MixtureSrgbColor(color.r, color.g, color.b)));
            }
            Check(Definition(4, out var co2));
            materials.Add(new FluidMaterial("carbon_dioxide", NativeMatterPhase.Gas, co2.MolarMass, co2.Cv, 0, co2.Offset, 1, 1e6));
            Color co2Color = ElementLoader.FindElementByHash(SimHashes.CarbonDioxide).substance.colour;
            styles.Add(new MixtureSpeciesStyle("carbon_dioxide", "Carbon dioxide", new MixtureSrgbColor(co2Color.r, co2Color.g, co2Color.b)));
            catalog = new FluidMaterialCatalog("oni-sealed-domain-v1", materials);
            MixturePresentationRuntime.ResetProviders();
            MixturePresentationRuntime.Configure(new MixturePresentationModel(catalog, styles, new AtmosphereSettings(), "oxygen", "carbon_dioxide"));
            arming = true;
            for (var index = 0; index < cells.Length; ++index)
                SimMessages.SetCellProperties(Grid.XYToCell(x + index % Width, y + index / Width),
                    NativeFlowMask);
            initialHot = Grid.Temperature[hotSolid]; initialCold = Grid.Temperature[coldSolid];
            var center = Grid.CellToPosCCC(target, Grid.SceneLayer.Background);
            CameraController.Instance.SnapTo(center); CameraController.Instance.SetTargetPos(center, 20, false);
            CameraController.Instance.OrthographicSize = 7;
            SpeedControlScreen.Instance.Unpause(false);
            ColonyRuntimeValidation.SetStage("fencing-sealed-native-domain"); WriteReport();
        }
        private static void ActivateOwner()
        {
            Require(BoundaryReady(false), "The sealed boundary changed before native ownership activation.");
            var configuration = new Configuration { Size = 40, Version = 1, Flags = 1, GridWidth = (uint)Grid.WidthInCells,
                X = (uint)x, Y = (uint)y, Width = Width, Height = Height, Scenario = Liquid ? 1U : 0U };
            latest = Status.Empty;
            Check(Begin(ref configuration, ref latest)); active = true; arming = false;
            textureWriteBaseline = MixtureWorldRendering.TextureWrites;
            writingProjection = true;
            try
            {
                for (var index = 0; index < cells.Length; ++index)
                {
                    var global = Grid.XYToCell(x + index % Width, y + index / Width);
                    SimMessages.ReplaceElement(global, Liquid ? SimHashes.Water : SimHashes.Oxygen,
                        CellEventLogger.Instance.SandBoxTool, ProjectionMass(index % Width), ProjectionTemperature(index % Width));
                }
            }
            finally { writingProjection = false; }
            ColonyRuntimeValidation.SetStage("running-sealed-native-domain"); WriteReport();
        }
        private static void Capture()
        {
            var held = (Cell[])cells.Clone(); var previous = latest.CompletedFrames;
            var next = Status.Empty; var values = new Cell[Width * Height];
            Check(Copy(ref next, values, (uint)values.Length));
            if (next.Session != latest.Session || next.CompletedFrames < previous) throw new InvalidOperationException("Native domain session changed during capture.");
            // Retained values are independent of all future native advances.
            retainedFrameVerified |= previous > 0 && next.CompletedFrames > previous && held[0].InternalEnergy == cells[0].InternalEnergy;
            latest = next; cells = values;
        }
        internal static IntPtr AfterNativeMessage(int message, IntPtr pointer)
        {
            if ((!active && !arming) || pointer == IntPtr.Zero || (message != -931446686 && message != 1078620451)) return pointer;
            if (failed) return LastSnapshot();
            OwnedGameDataSnapshot? candidate = null;
            try
            {
                var source = (Sim.GameDataUpdate*)pointer;
                nativeFlowFenceVerified = Enumerable.Range(0, cells.Length).All(index =>
                    (source->properties[Grid.XYToCell(x + index % Width, y + index / Width)] & NativeFlowMask) == NativeFlowMask);
                if (arming)
                {
                    if (!nativeFlowFenceVerified) return pointer;
                    ActivateOwner();
                }
                Capture(); Require(latest.State == 1, "Custom native worker fault: " + latest.Error);
                nativeProjectionMasses = Enumerable.Range(0, cells.Length).Select(index =>
                    source->mass[Grid.XYToCell(x + index % Width, y + index / Width)]).ToArray();
                nativeProjectionTemperatures = Enumerable.Range(0, cells.Length).Select(index =>
                    source->temperature[Grid.XYToCell(x + index % Width, y + index / Width)]).ToArray();
                if (!projectionPrimed)
                {
                    projectionPrimed = nativeFlowFenceVerified && Enumerable.Range(0, cells.Length).All(index =>
                        source->mass[Grid.XYToCell(x + index % Width, y + index / Width)] == ProjectionMass(index % Width) &&
                        source->temperature[Grid.XYToCell(x + index % Width, y + index / Width)] == ProjectionTemperature(index % Width) &&
                        source->elementIdx[Grid.XYToCell(x + index % Width, y + index / Width)] == ElementLoader.FindElementByHash(Liquid ? SimHashes.Water : SimHashes.Oxygen).idx);
                    if (projectionPrimed) projectionPrimedAtFrame = latest.CompletedFrames;
                }
                var readings = new Dictionary<int, MixtureThermodynamicReading>();
                for (var index = 0; index < cells.Length; ++index)
                {
                    var global = Grid.XYToCell(x + index % Width, y + index / Width); var cell = cells[index];
                    var native = (source->elementIdx[global], BitConverter.SingleToInt32Bits(source->mass[global]),
                        BitConverter.SingleToInt32Bits(source->temperature[global]), source->diseaseIdx[global], source->diseaseCount[global]);
                    if (projectionPrimed)
                    {
                        if (!nativeBaseline.TryGetValue(global, out var first)) nativeBaseline.Add(global, native);
                        else Require(first.Equals(native), "An unhandled native writer changed custom domain compatibility cell " +
                            global + ": " + first + " -> " + native + ".");
                    }
                    var constituents = new List<MixtureThermodynamicConstituent>();
                    for (var k = 0; k < Ids.Length; ++k)
                    {
                        if (cell.Mass(k) == 0) continue;
                        var material = catalog!.Materials[Ids[k]];
                        var volume = k == 3 ? 1 : cell.Mass(k) / material.MolarMassKgPerMol * FluidMaterialCatalog.GasConstantJoulesPerMolKelvin * cell.Temperature / cell.Pressure;
                        constituents.Add(new MixtureThermodynamicConstituent(Ids[k], material.Phase, cell.Mass(k), material.MolarMassKgPerMol, volume));
                    }
                    readings.Add(global, new MixtureThermodynamicReading(constituents, cell.TotalMass, cell.InternalEnergy, cell.ResolvedKineticEnergy,
                        cell.Temperature, Liquid ? 0 : cell.Pressure, cell.Pressure, 1, Liquid ? 0 : 1, Liquid ? 1 : 0, 0,
                        catalog!.Version, catalog.Fingerprint, (long)latest.CompletedFrames, "native-domain:cell:" + global,
                        momentumX: cell.MomentumX, momentumY: cell.MomentumY));
                }
                // Capture first and prove every native channel was copied. Only
                // the owned copy is subsequently overlaid with custom readings.
                var copy = candidate = OwnedGameDataSnapshot.Capture(source, Grid.WidthInCells, Grid.HeightInCells);
                copy.AssertEquivalentTo(source); nativeSnapshotVerified = true;
                var masses = copy.CellBuffer<float>(nameof(Sim.GameDataUpdate.mass));
                var temperatures = copy.CellBuffer<float>(nameof(Sim.GameDataUpdate.temperature));
                var elements = copy.CellBuffer<ushort>(nameof(Sim.GameDataUpdate.elementIdx));
                for (var index = 0; index < cells.Length; ++index)
                {
                    var global = Grid.XYToCell(x + index % Width, y + index / Width);
                    masses[global] = checked((float)cells[index].TotalMass); temperatures[global] = checked((float)cells[index].Temperature);
                    elements[global] = (ushort)ElementLoader.FindElementByHash(Liquid ? SimHashes.Water : SimHashes.Oxygen).idx;
                }
                if (publishedReadings == null || renderedRevision != latest.CompletedFrames)
                {
                    MixturePresentationRuntime.PublishNativeDomainFrame(Grid.WidthInCells, Grid.HeightInCells, (long)latest.CompletedFrames, readings);
                    publishedReadings = readings; renderedRevision = latest.CompletedFrames;
                }
                ColonyRuntimeValidation.RecordNativeDomainPublication(source, copy);
                var previous = currentSnapshot; currentSnapshot = candidate; candidate = null; previous?.Dispose();
                ++publications;
                return (IntPtr)copy.Pointer;
            }
            catch (Exception caught) { candidate?.Dispose(); nativeCellsUnchanged = false; Fail(caught); return LastSnapshot(); }
        }
        private static IntPtr LastSnapshot() => currentSnapshot != null ? (IntPtr)currentSnapshot.Pointer :
            throw new InvalidOperationException("Native domain faulted before a valid owned snapshot existed; scalar fallback is forbidden.");
        private static float ProjectionMass(int column) => Liquid ? column == 0 ? 1000 : column == 1 ? 1200 : 1100 :
            column == 0 ? .6f : column == 1 ? 1.2f : 2.4f;
        private static float ProjectionTemperature(int column) => 280 + 20 * column;
        internal static void BeforeNativeMutation(int message, int length, byte* payload)
        {
            if (!active || writingProjection || (message != -1252920804 && message != 818320644 && message != 1727657959 && message != 797274363 && message != -469311643)) return;
            if (length < 4 || payload == null) throw new ArgumentException("Invalid native mutation payload.");
            var cell = *(int*)payload; var xx = cell % Grid.WidthInCells; var yy = cell / Grid.WidthInCells;
            if (cell >= 0 && xx >= x - 1 && xx <= x + Width && yy >= y - 1 && yy <= y + Height)
            {
                var rejection = new NotSupportedException("Native fluid mutations/actors have no adapter for the sealed domain; the session is stopped.");
                Fail(rejection); throw rejection;
            }
        }
        private static void Fail(Exception caught)
        {
            if (failed) return;
            failed = true; error = caught.ToString();
            if (active) { var stopped = Status.Empty; Check(Stop(ref stopped)); latest = stopped; }
            SpeedControlScreen.Instance?.Pause(true); WriteReport(); ColonyRuntimeValidation.Fail(caught);
        }
        private static void VerifySaveGuard()
        {
            var before = NativeBackendObservation.Capture().Hooks.Single(hook => hook.Name == "begin-save").Entries;
            using var stream = new MemoryStream(); using var writer = new BinaryWriter(stream);
            var rejected = false;
            try { Sim.Save(writer, Grid.WidthInCells, Grid.HeightInCells); }
            catch (InvalidOperationException caught) when (caught.Message.Contains("saving is disabled")) { rejected = true; }
            saveGuardVerified = rejected && stream.Length == 0 &&
                NativeBackendObservation.Capture().Hooks.Single(hook => hook.Name == "begin-save").Entries == before;
            Require(saveGuardVerified, "Native save began despite the nonpersistent domain guard.");
        }
        private static void WriteReport() => File.WriteAllText(ReportPath, ReportJson.Serialize(new {
            Kind = "actual-game-sealed-native-fluid-domain", Status = completed ? "passed" : failed ? "failed" : "pending", Error = error,
            Platform = Application.platform.ToString(), Model = "oni-sealed-domain-v1", Scenario = Liquid ? "compressed-liquid" : "mixed-air",
            NativeDomain = latest, NativeCellsUnchanged = nativeCellsUnchanged, NativeSnapshotCopied = nativeSnapshotVerified,
            NativeSolidsAdvanced = nativeSolidsAdvanced, RetainedFrameVerified = retainedFrameVerified, Publications = publications,
            NativeFlowControlObserved = nativeFlowControlObserved, ProjectionPrimed = projectionPrimed,
            NativeFlowFenceVerified = nativeFlowFenceVerified, NativeProjectionMasses = nativeProjectionMasses,
            NativeProjectionTemperatures = nativeProjectionTemperatures,
            NativeRendererAdvanced = nativeRendererAdvanced, SaveGuardVerified = saveGuardVerified,
            GameGridProjectionVerified = gameGridProjectionVerified, ProjectionPrimedAtNativeFrame = projectionPrimedAtFrame,
            MixtureWorldRendering.TextureWrites, MixtureWorldRendering.LastTexturePublicationGeneration,
            NativeFlowExclusion = "Impermeable scalar compatibility cells plus gas-interaction and per-cell native phase hooks",
            TargetReading = publishedReadings != null && publishedReadings.TryGetValue(target, out var reading) ? reading : null,
            BoundedFluidAuthority = completed && active && !failed, CustomDomainActivated = active,
            WholeColonyFluidAuthority = false, FirstPlayableColonyPassed = false,
            OwnershipScope = "Sealed actor-free six-cell domain; native ONI owns the surrounding colony", SavesAllowed = !SaveBlocked,
            Width, Height, X = x, Y = y, TargetCell = target, RenderedRevision = renderedRevision
        }));
        internal static void Shutdown()
        {
            currentSnapshot?.Dispose(); currentSnapshot = null;
            if (active) { var stopped = Status.Empty; Check(Stop(ref stopped)); }
            active = false; arming = false;
        }
        internal static void DrawLabel()
        {
            if (!prepared) return;
            GUI.Box(new Rect(Screen.width * .12f, 100, Screen.width * .76f, 60),
                "SEALED NATIVE FLUID DOMAIN · " + (Liquid ? "compressed water" : "mixed air") + "\n" +
                latest.CompletedFrames + " native frames · 6 owned cells · surrounding colony: original ONI · saves disabled");
        }
        private static void Require(bool value, string message) { if (!value) throw new InvalidOperationException(message); }
        private static void Check(int value) { if (value != 0) throw new InvalidOperationException("Native sealed domain " + value + ": " + Marshal.PtrToStringAnsi(LastError())); }

    }
    public sealed class NativeSealedDomainLabel : MonoBehaviour { private void OnGUI() => NativeSealedDomainValidation.DrawLabel(); }
    [HarmonyPatch(typeof(Sim), nameof(Sim.Save))]
    internal static class NativeSealedDomainSaveGuard
    {
        private static void Prefix()
        {
            if (NativeSealedDomainValidation.SaveBlocked) throw new InvalidOperationException("The sealed native development domain has no integrated checkpoint format; saving is disabled.");
        }
    }
}
