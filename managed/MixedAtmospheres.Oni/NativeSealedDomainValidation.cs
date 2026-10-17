using System;
using System.Buffers.Binary;
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
    // admits only actors with an explicit conserved point-boundary adapter.
    internal static unsafe partial class NativeSealedDomainValidation
    {
        private const int Width = 3, Height = 2;
        private static readonly string[] Ids = { "nitrogen", "oxygen", "helium", "water", "carbon_dioxide" };
        private static bool prepared, arming, completed, failed;
        private static volatile bool active;
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
        private static bool nativeSaveGuardVerified;
        private static bool gameGridProjectionVerified;
        private static HeatStatus heatStatus;
        private static ThermalStatus thermalStatus;
        private static GravityStatus gravityStatus;
        private static bool gravityVerified;
        private static bool thermalVerified;
        private static bool heatSubmitted, heatVerified, heatAvoidedNativeDispatch;
        private static double heatBeforeEnergy, heatEnergyResidual;
        private static double heatEnergyScale, heatEnergyBound = 1e-6;
        private static int heatPacketsRouted;
        private static TransferStatus transferStatus;
        private static Receipt[] completedReceipts = Array.Empty<Receipt>();
        private static ulong deliveredReceipt;
        private static long transferPacketsRouted, receiptsPublished;
        private static bool transfersSubmitted, transfersVerified, transfersAvoidedNativeDispatch;
        private static bool transferConsumptionSubmitted;
        private static int transferConsumptionCallback;
        private static long radialConsumptionMessagesRouted;
        private static int emissionCallbacks, consumptionCallbacks;
        private static Sim.MassEmittedCallback emissionResult;
        private static Sim.MassConsumedCallback consumptionResult;
        private static double transfersBeforeEnergy, transferEnergyResidual, transferMassError;
        private static double transferEnergyScale, transferEnergyBound = 1e-6;
        private static Receipt? liquidInjectionReceipt, liquidExtractionReceipt;
        private static double[] transfersBeforeMasses = Array.Empty<double>();
        private static System.Action? releaseTransferCallbacks;
        private static ulong projectionPrimedAtFrame;
        private static readonly byte NativeFlowMask = (byte)(Sim.Cell.Properties.GasImpermeable | Sim.Cell.Properties.LiquidImpermeable);
        private static float[] nativeProjectionMasses = Array.Empty<float>();
        private static float[] nativeProjectionTemperatures = Array.Empty<float>();
        private static IReadOnlyDictionary<int, MixtureThermodynamicReading>? publishedReadings;
        private static string ReportPath => Path.Combine(ColonyRuntimeValidation.OutputDirectory, "native-domain.json");
        internal static bool Active => active;
        internal static bool Failed => failed;
        internal static string FailureMessage => error;
        internal static bool SaveBlocked => active || arming;
        internal static bool IsMode => RectangularConsumptionMode || RadialConsumptionMode || MixedPhaseMode || PressureReadMode || BreathingReadMode || CarbonParticleMode || InitialInventoryMode || ColonyRuntimeValidation.Settings?.Mode == "native-domain" ||
            ColonyRuntimeValidation.Settings?.Mode == "native-liquid-domain" || LiquidTransferMode || LiquidActorMode || LiquidHeatMode || ColonyRuntimeValidation.Settings?.Mode == "native-heat-domain" ||
            ColonyRuntimeValidation.Settings?.Mode == "native-transfer-domain" || ColonyRuntimeValidation.Settings?.Mode == "native-actor-domain" ||
            ColonyRuntimeValidation.Settings?.Mode == "native-carbon-domain" || ColonyRuntimeValidation.Settings?.Mode == "native-conduction-domain" ||
            ColonyRuntimeValidation.Settings?.Mode == "native-gravity-domain" || CheckpointMode || ColonyRuntimeValidation.Settings?.Mode == "native-save-domain" || ColonyRestoreMode || SolidHeatMode;
        private static bool LiquidGravityMode => ColonyRuntimeValidation.Settings?.Mode == "native-liquid-gravity-domain";
        private static bool LiquidTransferMode => ColonyRuntimeValidation.Settings?.Mode == "native-liquid-transfer-domain" || LiquidGravityMode;
        private static bool LiquidActorMode => ColonyRuntimeValidation.Settings?.Mode == "native-liquid-actor-domain";
        private static bool LiquidHeatMode => ColonyRuntimeValidation.Settings?.Mode == "native-liquid-heat-domain";
        private static double FixtureHeatJoules => LiquidHeatMode || MixedHeatMode ? 1500000 : 1500;
        private static bool Liquid => restoredScenarioSelected ? restoredScenario==1 || restoredScenario==3 : LiquidPressureReadMode || LiquidInitialInventoryMode || ColonyRuntimeValidation.Settings?.Mode == "native-liquid-domain" || LiquidTransferMode || LiquidActorMode || LiquidHeatMode;
        private static bool HeatMode => ColonyRuntimeValidation.Settings?.Mode == "native-heat-domain" || ConductionMode;
        private static bool ConductionMode => ColonyRuntimeValidation.Settings?.Mode == "native-conduction-domain" || LiquidHeatMode || MixedHeatMode;
        private static bool TransferMode => RectangularConsumptionMode || RadialConsumptionMode || ColonyRuntimeValidation.Settings?.Mode == "native-transfer-domain" || LiquidTransferMode || CarbonMode || GravityMode;
        private static bool CarbonMode => ColonyRuntimeValidation.Settings?.Mode == "native-carbon-domain";
        private static bool GravityMode => restoredScenarioSelected ? restoredScenario==2 || restoredScenario==3 : ColonyRuntimeValidation.Settings?.Mode == "native-gravity-domain" || LiquidGravityMode;
        private static bool ActorMode => ColonyRuntimeValidation.Settings?.Mode == "native-actor-domain" || LiquidActorMode;
        private static double ProjectedPotential => cells.Select((cell, index) => cell.TotalMass * 9.81 * (index / Width + .5)).Sum();
        private static double PhysicalEnergy => cells.Sum(cell => cell.InternalEnergy + cell.ResolvedKineticEnergy) + (GravityMode ? ProjectedPotential : 0);

        internal static void Tick()
        {
            if (completed || failed) return;
            try
            {
                if (ColonyRestoreMode) { TickRestoredColony(); return; }
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
                if (SaveRejectionRequired && projectionPrimed && latest.CompletedFrames - projectionPrimedAtFrame >= 4 && !rejectedSaveVerified)
                { VerifyPendingSaveRejection(); return; }
                if (IoSaveControlRequired && projectionPrimed && latest.CompletedFrames - projectionPrimedAtFrame >= 8 && !ioSaveControlVerified)
                { VerifyIoSaveRecovery(); return; }
                if (HeatMode && projectionPrimed && latest.CompletedFrames - projectionPrimedAtFrame >= 4 && !heatSubmitted)
                {
                    heatBeforeEnergy = PhysicalEnergy;
                    heatEnergyScale = cells.Sum(cell => Math.Abs(cell.InternalEnergy) + cell.ResolvedKineticEnergy);
                    var method = AccessTools.Method(typeof(SimMessages), nameof(SimMessages.ModifyEnergy));
                    NativeMessagePatch.RequirePatched(method);
                    var before = NativeBackendObservation.Capture().Hooks.Single(hook => hook.Name == "message").Entries;
                    SimMessages.ModifyEnergy(target, LiquidHeatMode || MixedHeatMode ? 2000 : 2, 1000, SimMessages.EnergySourceID.DebugHeat);
                    SimMessages.ModifyEnergy(target, LiquidHeatMode || MixedHeatMode ? -500 : -.5f, 1000, SimMessages.EnergySourceID.DebugCool);
                    heatAvoidedNativeDispatch = before == NativeBackendObservation.Capture().Hooks.Single(hook => hook.Name == "message").Entries;
                    Require(heatAvoidedNativeDispatch && heatPacketsRouted == 2, "Actual game heat packets bypassed the custom native owner.");
                    heatSubmitted = true;
                }
                if (TransferMode && projectionPrimed && latest.CompletedFrames - projectionPrimedAtFrame >= 4 && !transfersSubmitted)
                    SubmitGameFluidTransfers();
                if (LiquidTransferMode && transfersSubmitted && !transferConsumptionSubmitted && transferStatus.CommittedBatches == 1)
                    SubmitLiquidConsumption();
                if (ActorMode && projectionPrimed && latest.CompletedFrames - projectionPrimedAtFrame >= 4) TickActorFixture();
                if(PersistentActorValidationRequired && projectionPrimed && latest.CompletedFrames-projectionPrimedAtFrame>=4) StartPersistentActorFixture();
                if (CheckpointMode && projectionPrimed && latest.CompletedFrames - projectionPrimedAtFrame >= 4) TickCheckpointFixture();
                if (PairedCheckpointMode && projectionPrimed && latest.CompletedFrames - projectionPrimedAtFrame >= 4) TickPairedCheckpointFixture();
                if (CarbonParticleMode && projectionPrimed && latest.CompletedFrames - projectionPrimedAtFrame >= 4) StartCarbonParticleFixture();
                if (BreathingReadMode && projectionPrimed && latest.CompletedFrames - projectionPrimedAtFrame >= 4) StartBreathingReadFixture();
                if (LiveBreathingRequired && projectionPrimed && latest.CompletedFrames-projectionPrimedAtFrame>=4) BindLiveBreather(false);
                if (!projectionPrimed || latest.CompletedFrames - projectionPrimedAtFrame < (ulong)ColonyRuntimeValidation.Settings!.CompletedFrames || publications < 2) return;
                if(LiveBreathingRequired && !LiveBreathingDrained) return;
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
                        Grid.Element[global].id == (Liquid ? SimHashes.Water : SimHashes.Oxygen) &&
                        !Grid.LiquidImpermeable[global];
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
                Require(MixedPhaseMode ? MixedPhaseVerified && latest.CurrentOxygenKg != latest.InitialOxygenKg && thermalStatus.Enabled == (MixedConductionMode ? 1U : 0U) :
                    InitialInventoryMode ? InitialInventoryAdvanced && latest.PressurePa > 0 :
                    Liquid ? LiquidActorMode || PersistentActorValidationRequired ? cells.Sum(cell => cell.Water) != 6600 && latest.PressurePa > 0 :
                    LiquidTransferMode ? cells.Sum(cell => cell.Water) > 6600 && latest.PressurePa > 2.2e8 :
                    latest.LiquidMassKg == 1100 && Math.Abs(latest.PressurePa - 2.2e8) < 1 :
                    latest.CurrentOxygenKg != latest.InitialOxygenKg, "Custom fluid state did not exercise its intended physics.");
                if (ViscousMode) Require(ViscosityVerified, "Native worker did not complete physical viscosity and represented heating.");
                if (LiveBreathingRequired) VerifyLiveBreathing();
                VerifyNativeSolidHeatCompletion();
                if (HeatMode)
                {
                    heatEnergyResidual = PhysicalEnergy - heatBeforeEnergy - FixtureHeatJoules;
                    if (LiquidHeatMode || MixedHeatMode)
                    {
                        heatEnergyScale += cells.Sum(cell => Math.Abs(cell.InternalEnergy) + cell.ResolvedKineticEnergy);
                        heatEnergyBound = 128 * 2.2204460492503131e-16 * heatEnergyScale;
                        Require(cells[0].Temperature>300 && cells.Skip(1).Any(cell => cell.Temperature>300+1e-9),
                            "Fluid heat did not change both the source and a conductive neighbor.");
                    }
                    heatVerified = heatSubmitted && heatAvoidedNativeDispatch && heatPacketsRouted == 2 && heatStatus.PendingBatches == 0 &&
                        heatStatus.AcceptedBatches == 2 && heatStatus.CommittedBatches == 2 && heatStatus.CommittedHeatJoules == FixtureHeatJoules &&
                        Math.Abs(heatEnergyResidual) <= heatEnergyBound;
                    Require(heatVerified, "Game heating/cooling did not conserve its explicit native fluid heat account.");
                }
                if (TransferMode)
                {
                    transferEnergyResidual = PhysicalEnergy - transfersBeforeEnergy -
                        transferStatus.MaterialEnergyChange - transferStatus.KineticEnergyChange - gravityStatus.CommittedBoundaryPotential;
                    if (LiquidTransferMode)
                    {
                        transferEnergyScale += cells.Sum(cell => Math.Abs(cell.InternalEnergy) + cell.ResolvedKineticEnergy);
                        transferEnergyBound = 128 * 2.2204460492503131e-16 * transferEnergyScale;
                    }
                    transferMassError = 0;
                    for (var k = 0; k < Ids.Length; ++k)
                    {
                        var current = cells.Sum(cell => cell.Mass(k)); var change = transferStatus.MassChangesKg[k];
                        var scale = Math.Abs(transfersBeforeMasses[k]) + Math.Abs(change);
                        if (scale == 0) Require(current == 0, "Fluid command created an unaccounted constituent.");
                        else transferMassError = Math.Max(transferMassError, Math.Abs(current - transfersBeforeMasses[k] - change) / scale);
                    }
                    var element = ElementLoader.FindElementByHash(LiquidTransferMode ? SimHashes.Water : CarbonMode ? SimHashes.CarbonDioxide : SimHashes.Oxygen).idx;
                    transfersVerified = transfersSubmitted && transfersAvoidedNativeDispatch && transferPacketsRouted == 2 &&
                        transferStatus.AcceptedBatches == 2 && transferStatus.CommittedBatches == 2 && transferStatus.PendingBatches == 0 &&
                        transferStatus.UnacknowledgedReceipts == 0 && receiptsPublished == 2 && emissionCallbacks == 1 && consumptionCallbacks == 1 &&
                        emissionResult.suceeded == 1 && emissionResult.mass == .05f && consumptionResult.mass == FixtureConsumptionKg &&
                        emissionResult.elemIdx == element && consumptionResult.elemIdx == element &&
                        emissionResult.diseaseIdx == 255 && consumptionResult.diseaseIdx == 255 &&
                        emissionResult.diseaseCount == 0 && consumptionResult.diseaseCount == 0 && consumptionResult.temperature > 0 &&
                        transferStatus.MassChangesKg[LiquidTransferMode ? 3 : CarbonMode ? 4 : 1] == (double).05f - FixtureConsumptionKg && transferStatus.KineticEnergyChange < 0 &&
                        (!CarbonMode || cells.Any(cell => cell.CarbonDioxide > 0) && publishedReadings!.Values.Any(value =>
                            value.Constituents.Any(item => item.MaterialId == "carbon_dioxide" && item.MassKg > 0))) &&
                        transferMassError <= 1e-10 && Math.Abs(transferEnergyResidual) <= transferEnergyBound;
                    if (LiquidTransferMode)
                    {
                        Require(liquidInjectionReceipt.HasValue && liquidExtractionReceipt.HasValue, "Liquid point receipts were not both published.");
                        var incoming = liquidInjectionReceipt!.Value; var outgoing = liquidExtractionReceipt!.Value;
                        transfersVerified &= incoming.Species == 3 && outgoing.Species == 3 && incoming.CompletedFrame < outgoing.CompletedFrame &&
                            incoming.MassKg == (double).05f && outgoing.MassKg == (double).02f && outgoing.KineticEnergy > 0 &&
                            transferStatus.KineticEnergyChange == -outgoing.KineticEnergy &&
                            Math.Abs(transferStatus.MaterialEnergyChange - (incoming.InternalEnergy - outgoing.InternalEnergy)) <=
                                64 * 2.2204460492503131e-16 * (Math.Abs(incoming.InternalEnergy) + Math.Abs(outgoing.InternalEnergy));
                    }
                    Require(transfersVerified, "Actual fluid messages, physical accounts or once-only game callbacks failed.");
                    if(RadialConsumptionMode) Require(RadialConsumptionVerified,"Radius consumption did not deliver one callback spanning multiple physical source cells.");
                    if(RectangularConsumptionMode) Require(RectangularConsumptionVerified,"Rectangular consumption did not deliver one callback spanning multiple physical source cells.");
                    releaseTransferCallbacks?.Invoke(); releaseTransferCallbacks = null;
                }
                if (ConductionMode)
                {
                    thermalVerified = heatVerified && thermalStatus.Enabled == 1 && thermalStatus.State == 1 &&
                        thermalStatus.Session == latest.Session && thermalStatus.CompletedFrame == latest.CompletedFrames &&
                        thermalStatus.Stages == 2 * latest.Substeps && thermalStatus.Contacts > 0 && thermalStatus.Iterations > 0 &&
                        thermalStatus.RedistributedEnergy > 0 && thermalStatus.EntropyChange > 0;
                    Require(thermalVerified, "Native internal conduction did not advance physical heat and entropy with the worker clock.");
                }
                if (GravityMode)
                {
                    gravityVerified = transfersVerified && gravityStatus.Enabled == 1 && gravityStatus.State == 1 && gravityStatus.Gravity == 9.81 &&
                        gravityStatus.Session == latest.Session && gravityStatus.CompletedFrame == latest.CompletedFrames &&
                        gravityStatus.PendingBoundaryPotential == 0 && gravityStatus.BodyImpulseY < 0 && gravityStatus.MaximumEnergyResidual <= (Liquid ? transferEnergyBound : 1e-6) &&
                        gravityStatus.Potential > 0 && gravityStatus.Potential != gravityStatus.InitialPotential &&
                        Math.Abs(gravityStatus.Potential - ProjectedPotential) < 1e-9 && cells.Any(cell => cell.MomentumY != 0) &&
                        Math.Abs(gravityStatus.CommittedBoundaryPotential - ((double).05f - .02f) * 9.81 * .5) < 1e-15;
                    Require(gravityVerified, "Gravity force, physical potential or carried boundary energy did not match the native completed frame.");
                }
                VerifySaveGuard();
                if (ActorMode) VerifyActorFixture();
                if(PersistentActorValidationRequired) VerifyPersistentActorFixture();
                if (CarbonParticleMode) VerifyCarbonParticleFixture();
                if (BreathingReadMode) VerifyBreathingReadFixture();
                if (PressureReadMode) VerifyPressureReadFixture();
                if (NaturalAutosaveRequired) VerifyNaturalAutosaveControl();
                VerifyNativeGpuGasTextures();
                if (CheckpointMode) Require(checkpointVerified, "Physical native checkpoint continuation was not verified against the live worker.");
                if (PairedCheckpointMode) Require(pairedVerified,"Paired native/physical checkpoint capture was not verified in the game.");
                if (ColonySaveMode) SavePausedColony();
                completed = true; NativeBackendObservation.Flush(); WriteReport();
                ColonyRuntimeValidation.CaptureScreenshot("native-domain.png");
                ColonyRuntimeValidation.CompletePresentation("actual-game-sealed-native-fluid-domain-validation");
            }
            catch (Exception caught) { Fail(caught); }
        }
        private static void Prepare()
        {
            Require(NativeBackendObservation.Enabled, "Native domain requires an explicitly enabled native backend.");
            Require(Marshal.SizeOf<Configuration>() == 40 && Marshal.SizeOf<Status>() == 440 && Marshal.SizeOf<Cell>() == 96,
                "Native domain ABI layout mismatch.");
            Require(Version() == 2, "Unsupported native domain ABI.");
            Require(HeatVersion() == 1 && Marshal.SizeOf<HeatCommand>() == 48 && Marshal.SizeOf<HeatStatus>() == 56,
                "Unsupported native heat command ABI.");
            Require(TransferVersion() == 2 && Marshal.SizeOf<TransferCommand>() == 64 && Marshal.SizeOf<Receipt>() == 88 &&
                Marshal.SizeOf<TransferStatus>() == 104, "Unsupported native gas transfer ABI.");
            Require(ActorVersion() == 1 && Marshal.SizeOf<ActorCommand>() == 80 && Marshal.SizeOf<ActorStatus>() == 64,
                "Unsupported native actor ABI.");
            Require(ActorCheckpointVersion()==1 && Marshal.SizeOf<PersistentActorCommand>()==96,"Unsupported durable native actor ABI.");
            Require(ThermalVersion() == 1 && Marshal.SizeOf<ThermalStatus>() == 80, "Unsupported native conduction ABI.");
            Require(ViscosityVersion() == 1 && Marshal.SizeOf<ViscosityStatus>() == 120, "Unsupported native physical viscosity ABI.");
            Require(FrameInputsVersion()==1,"Unsupported native accepted frame input journal ABI.");
            RequireChunkHeatCapability();
            Require(RadialConsumptionVersion() == 1,"Unsupported native radius-consumption capability.");
            Require(RectangularConsumptionVersion() == 1,"Unsupported native rectangular-consumption capability.");
            Require(GravityVersion() == 1 && Marshal.SizeOf<GravityStatus>() == 96, "Unsupported native gravity ABI.");
            Require(CheckpointVersion() == 1, "Unsupported native physical checkpoint ABI.");
            Require(PairVersion()==1 && Marshal.SizeOf<PairStatus>()==112,"Unsupported native paired checkpoint ABI.");
            prepared = true; preparedFrame = Time.frameCount; started = Time.realtimeSinceStartup;
            ConfigureNaturalAutosaveControl();
            Game.Instance.debugWasUsed = true;
            CustomGameSettings.Instance.SetQualitySetting(CustomGameSettingConfigs.SandboxMode, "Enabled");
            Game.Instance.SandboxModeActive = true;
            var start = SaveLoader.Instance.cachedGSD.baseStartPos + ClusterManager.Instance.GetStartWorld().WorldOffset;
            var chosen = false;
            for (var yy = start.y + 16; yy <= start.y + 30 && !chosen; ++yy)
            for (var xx = start.x - 8; xx <= start.x + 8 && !chosen; ++xx)
            {
                var clear = true;
                // Replacing a neighbouring support cell can uproot flora and
                // spawn a seed into an otherwise empty chamber. Reserve an
                // object-free margin around every cell this fixture changes,
                // including the solid backer and the outside conduction pair.
                const int occupantMargin = 3;
                for (var dy = -1 - occupantMargin; dy <= Height + occupantMargin; ++dy)
                for (var dx = (SolidHeatMode ? -2 : -1) - occupantMargin; dx <= Width + 3 + occupantMargin; ++dx)
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
                var thermalWall=SolidHeatMode && dx== -1 && dy==0;
                SimMessages.ReplaceElement(index, thermalWall ? SimHashes.Granite : wall ? SimHashes.Unobtanium : Liquid ? SimHashes.Water : InitialInventoryMode && dy == 0 ? SimHashes.CarbonDioxide : SimHashes.Oxygen,
                    eventId, thermalWall ? 10 : wall ? 20000 : ProjectionMass(dx), thermalWall ? 400 : wall ? 300 : ProjectionTemperature(dx));
            }
            if(SolidHeatMode) SimMessages.ReplaceElement(Grid.XYToCell(x-2,y),SimHashes.Unobtanium,eventId,20000,300);
            SimMessages.ReplaceElement(hotSolid, SimHashes.Granite, eventId, 1000, 350);
            SimMessages.ReplaceElement(coldSolid, SimHashes.Granite, eventId, 1000, 250);
            SpeedControlScreen.Instance.Unpause(false);
            ColonyRuntimeValidation.SetStage("preparing-sealed-native-domain"); WriteReport();
        }
        private static bool BoundaryReady(bool requireFluidIdentity = true)
        {
            for (var dy = -1; dy <= Height; ++dy) for (var dx = -1; dx <= Width; ++dx)
            {
                var index = Grid.XYToCell(x + dx, y + dy); var wall = dx == -1 || dx == Width || dy == -1 || dy == Height;
                if (Grid.Solid[index] != wall || (wall && Grid.Element[index].id != (IsSolidHeatBoundary(dx,dy) ? SimHashes.Granite : SimHashes.Unobtanium)) ||
                    (!wall && requireFluidIdentity && !InitialInventoryMode && Grid.Element[index].id != (Liquid ? SimHashes.Water : SimHashes.Oxygen))) return false;
                if (!wall)
                {
                    if (Grid.DiseaseCount[index] != 0 || Grid.Radiation[index] != 0) return false;
                    for (var layer = 0; layer < (int)ObjectLayer.NumLayers; ++layer)
                    {
                        var occupant = Grid.Objects[index, layer];
                        if (occupant != null && !IsLiveBreatherObject(occupant))
                            throw new InvalidOperationException("An actor entered the unbound native domain: cell=" + index +
                                ", layer=" + layer + ", name=" + occupant.name + ", instance=" + occupant.GetInstanceID() +
                                ", position=" + occupant.transform.position + ", components=" +
                                string.Join(",", occupant.GetComponents<Component>().Select(component => component == null ? "missing" : component.GetType().FullName)) + ".");
                    }
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
            ConfigureDomainPresentation();
            arming = true;
            for (var index = 0; index < cells.Length && !InitialInventoryMode; ++index)
                SimMessages.SetCellProperties(Grid.XYToCell(x + index % Width, y + index / Width),
                    NativeFlowMask);
            initialHot = Grid.Temperature[hotSolid]; initialCold = Grid.Temperature[coldSolid];
            var center = Grid.CellToPosCCC(target, Grid.SceneLayer.Background);
            CameraController.Instance.SnapTo(center); CameraController.Instance.SetTargetPos(center, 20, false);
            CameraController.Instance.OrthographicSize = 7;
            SpeedControlScreen.Instance.Unpause(false);
            ColonyRuntimeValidation.SetStage("fencing-sealed-native-domain"); WriteReport();
        }
        private static void ConfigureDomainPresentation()
        {
            var materials = new List<FluidMaterial>(); var styles = new List<MixtureSpeciesStyle>();
            var names = new[] { "Nitrogen", "Oxygen", "Helium", "Water", "Carbon dioxide" };
            var hashes = new[] { OniNitrogenElements.Gas, SimHashes.Oxygen, SimHashes.Helium, SimHashes.Water, SimHashes.CarbonDioxide };
            for (uint k = 0; k < Ids.Length; ++k)
            {
                Check(Definition(k, out var value));
                materials.Add(new FluidMaterial(Ids[k], k == 3 ? NativeMatterPhase.Liquid : NativeMatterPhase.Gas,
                    value.MolarMass, value.Cv, value.Density, value.Offset, 1, 1e6));
                Color color = ElementLoader.FindElementByHash(hashes[k]).substance.colour;
                styles.Add(new MixtureSpeciesStyle(Ids[k], names[k], new MixtureSrgbColor(color.r, color.g, color.b)));
            }
            catalog = new FluidMaterialCatalog("oni-sealed-domain-v2", materials);
            MixturePresentationRuntime.ResetProviders();
            MixturePresentationRuntime.Configure(new MixturePresentationModel(catalog, styles, new AtmosphereSettings(), "oxygen", "carbon_dioxide"));
            MixtureInspection.CellHoverObserver -= ObserveNativeHover;
            MixtureInspection.CellHoverObserver += ObserveNativeHover;
        }
        private static void ActivateOwner(Sim.GameDataUpdate* source)
        {
            Require(BoundaryReady(false), "The sealed boundary changed before native ownership activation.");
            var configuration = new Configuration { Size = 40, Version = 2, Flags = 1, GridWidth = (uint)Grid.WidthInCells,
                X = (uint)x, Y = (uint)y, Width = Width, Height = Height, Scenario = ViscousMode ? 6U : MixedHeatMode ? 5U : MixedPhaseMode ? 4U : LiquidGravityMode ? 3U : Liquid ? 1U : GravityMode ? 2U : 0U };
            latest = Status.Empty;
            if (InitialInventoryMode) ImportNativeInventory(source, ref configuration);
            else Check(Begin(ref configuration, ref latest));
            RegisterNativeSolidHeat(source);
            domainManagedThread=System.Threading.Thread.CurrentThread.ManagedThreadId; active = true; arming = false;
            textureWriteBaseline = MixtureWorldRendering.TextureWrites;
            writingProjection = true;
            try
            {
                if (InitialInventoryMode)
                {
                    Require(initialInventoryVerified && latest.State == 1 && !nativeFlowFenceVerified,
                        "Imported ownership must precede the native flow fence.");
                    initialInventoryBeforeFence = true;
                    for (var index = 0; index < cells.Length; ++index)
                        SimMessages.SetCellProperties(Grid.XYToCell(x + index % Width, y + index / Width), NativeFlowMask);
                }
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
            var receipts = new Receipt[4096];
            Check(CopyReceipts(ref next, values, (uint)values.Length, receipts, (uint)receipts.Length, out var count));
            if (count > receipts.Length) throw new InvalidOperationException("Native receipt count exceeds capacity.");
            completedReceipts = receipts.Take(checked((int)count)).ToArray();
            if (next.Session != latest.Session || next.CompletedFrames < previous) throw new InvalidOperationException("Native domain session changed during capture.");
            // Retained values are independent of all future native advances.
            retainedFrameVerified |= previous > 0 && next.CompletedFrames > previous && held[0].InternalEnergy == cells[0].InternalEnergy;
            latest = next; cells = values;
            var heat = HeatStatus.Empty; Check(CopyHeat(ref heat)); heatStatus = heat;
            var thermal = ThermalStatus.Empty; Check(CopyThermalStatus(ref thermal)); thermalStatus = thermal;
            var viscosity = ViscosityStatus.Empty; Check(CopyViscosityStatus(ref viscosity)); viscosityStatus = viscosity;
            CaptureChunkHeat();
            CaptureNativeSolidHeat();
            var gravity = GravityStatus.Empty; Check(CopyGravityStatus(ref gravity)); gravityStatus = gravity;
            var transfers = TransferStatus.Empty; Check(CopyTransferStatus(ref transfers)); transferStatus = transfers;
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
                    if (InitialInventoryMode ? source->numFramesProcessed <= 0 : !nativeFlowFenceVerified) return pointer;
                    ActivateOwner(source);
                }
                Capture(); Require(latest.State == 1, "Custom native worker fault: " + latest.Error);
                ObserveNativeSolidEndpoints(source);
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
                    readings.Add(global, NativeDomainReading.Create(cell, catalog!, (long)latest.CompletedFrames, global));
                }
                // Capture first and prove every native channel was copied. Only
                // the owned copy is subsequently overlaid with custom readings.
                var copy = candidate = OwnedGameDataSnapshot.Capture(source, Grid.WidthInCells, Grid.HeightInCells);
                copy.AssertEquivalentTo(source); nativeSnapshotVerified = true;
                PrependRestoredNativeEvents(copy);
                CaptureActorRegistrations(source);
                PrepareActorSnapshot(copy);
                var masses = copy.CellBuffer<float>(nameof(Sim.GameDataUpdate.mass));
                var temperatures = copy.CellBuffer<float>(nameof(Sim.GameDataUpdate.temperature));
                var elements = copy.CellBuffer<ushort>(nameof(Sim.GameDataUpdate.elementIdx));
                var properties = copy.CellBuffer<byte>(nameof(Sim.GameDataUpdate.properties));
                for (var index = 0; index < cells.Length; ++index)
                {
                    var global = Grid.XYToCell(x + index % Width, y + index / Width);
                    masses[global] = checked((float)cells[index].TotalMass); temperatures[global] = checked((float)cells[index].Temperature);
                    elements[global] = (ushort)ElementLoader.FindElementByHash(Liquid ? SimHashes.Water : SimHashes.Oxygen).idx;
                    // These two flags fence the original native simulator. In
                    // the game-facing copy they would falsely describe fluid
                    // as a blocked tile and suppress ONI's liquid rendering.
                    // Keep every other flag and every original native byte.
                    properties[global] = (byte)(properties[global] & ~NativeFlowMask);
                }
                var consumed = new List<Sim.MassConsumedCallback>(); var emitted = new List<Sim.MassEmittedCallback>();
                var through = deliveredReceipt;
                foreach (var receipt in completedReceipts)
                {
                    Require(receipt.Session == latest.Session && receipt.CompletedFrame > 0 && receipt.CompletedFrame <= latest.CompletedFrames &&
                        receipt.Sequence == through + 1 && receipt.Species < Ids.Length,
                        "Native gas receipt disagrees with its completed physical frame or delivery order.");
                    through = receipt.Sequence;
                    ObserveCarbonParticleReceipt(receipt);
                    ObserveSavedParticleDeposit(receipt);
                    ObserveRadialConsumptionReceipt(receipt);
                    ObserveRectangularConsumptionReceipt(receipt);
                    ObserveLiveBreathingReceipt(receipt);
                    if (LiquidTransferMode && receipt.Kind == 1) liquidInjectionReceipt = receipt;
                    if (LiquidTransferMode && receipt.Kind == 2) liquidExtractionReceipt = receipt;
                    if (receipt.Callback == -1) continue;
                    var element = (ushort)ElementLoader.FindElementByHash(SpeciesHash(receipt.Species)).idx;
                    var mass = NativeBinary32.ToSingleExact(receipt.MassKg);
                    if (receipt.Kind >= 3) PublishActorReceipt(copy, receipt, element, mass);
                    else if (receipt.Kind == 2) consumed.Add(new Sim.MassConsumedCallback { callbackIdx = receipt.Callback, elemIdx = element,
                        mass = mass, temperature = receipt.MassKg == 0 ? 0 : (float)receipt.Temperature, diseaseIdx = 255, diseaseCount = 0 });
                    else if (receipt.Kind == 1) emitted.Add(new Sim.MassEmittedCallback { callbackIdx = receipt.Callback, elemIdx = element,
                        suceeded = 1, mass = mass, temperature = (float)receipt.Temperature, diseaseIdx = 255, diseaseCount = 0 });
                    else throw new InvalidOperationException("Unknown native gas receipt kind.");
                }
                copy.AppendEvents<Sim.MassConsumedCallback>(SnapshotEventChannel.MassConsumedCallbacks, consumed.ToArray());
                copy.AppendEvents<Sim.MassEmittedCallback>(SnapshotEventChannel.MassEmittedCallbacks, emitted.ToArray());
                if (publishedReadings == null || renderedRevision != latest.CompletedFrames)
                {
                    MixturePresentationRuntime.PublishNativeDomainFrame(Grid.WidthInCells, Grid.HeightInCells, (long)latest.CompletedFrames, readings);
                    publishedReadings = readings; renderedRevision = latest.CompletedFrames;
                }
                ColonyRuntimeValidation.RecordNativeDomainPublication(source, copy);
                var nextReceiptCount = checked(receiptsPublished + (long)(through - deliveredReceipt));
                if (through != deliveredReceipt)
                {
                    Check(AckReceipts(latest.Session, through)); receiptsPublished = nextReceiptCount; deliveredReceipt = through;
                }
                var previous = currentSnapshot; currentSnapshot = candidate; candidate = null; previous?.Dispose();
                ReleaseRestoredNativeEvents(true);
                ++publications;
                VerifyRestoredPhysicalFrame();
                return (IntPtr)copy.Pointer;
            }
            catch (Exception caught) { candidate?.Dispose(); nativeCellsUnchanged = false; Fail(caught); return LastSnapshot(); }
        }
        private static IntPtr LastSnapshot() => currentSnapshot != null ? (IntPtr)currentSnapshot.Pointer :
            throw new InvalidOperationException("Native domain faulted before a valid owned snapshot existed; scalar fallback is forbidden.");
        private static float ProjectionMass(int column) => Liquid ? column == 0 ? 1000 : column == 1 ? 1200 : 1100 :
            column == 0 ? .6f : column == 1 ? 1.2f : 2.4f;
        private static float ProjectionTemperature(int column) => 280 + 20 * column;
        private static void SubmitGameFluidTransfers()
        {
            if(RadialConsumptionMode) {radialSourceCellOxygen=cells[0].Oxygen;radialSourceFrame=latest.CompletedFrames;}
            if(RectangularConsumptionMode) {rectangularSourceCellOxygen=cells[0].Oxygen;rectangularSourceFrame=latest.CompletedFrames;}
            transfersBeforeEnergy = PhysicalEnergy;
            transferEnergyScale = cells.Sum(cell => Math.Abs(cell.InternalEnergy) + cell.ResolvedKineticEnergy);
            transfersBeforeMasses = Enumerable.Range(0, Ids.Length).Select(k => cells.Sum(cell => cell.Mass(k))).ToArray();
            var emissions = Game.Instance.massEmitCallbackManager;
            var consumptions = Game.Instance.massConsumedCallbackManager;
            var emitted = emissions.Add((data, _) => { ++emissionCallbacks; emissionResult = data; },
                new object(), "Mixed Atmospheres native fluid injection control");
            var consumed = consumptions.Add((data, _) => { ++consumptionCallbacks; consumptionResult = data; },
                new object(), "Mixed Atmospheres native selective consumption control");
            transferConsumptionCallback = consumed.index;
            releaseTransferCallbacks = () =>
            {
                if (emissions.IsVersionValid(emitted)) emissions.Release(emitted, "native fluid control complete");
                if (consumptions.IsVersionValid(consumed)) consumptions.Release(consumed, "native fluid control complete");
            };
            var before = NativeBackendObservation.Capture().Hooks.Single(hook => hook.Name == "message").Entries;
            var element = LiquidTransferMode ? SimHashes.Water : CarbonMode ? SimHashes.CarbonDioxide : SimHashes.Oxygen;
            SimMessages.EmitMass(target, (ushort)ElementLoader.FindElementByHash(element).idx, .05f, CarbonMode ? 310 : 300, 255, 0, emitted.index);
            // A resting liquid needs a positive physical interval after
            // injection to exercise carried extraction momentum and K.
            if (RectangularConsumptionMode)
                SimMessages.ConsumeMass(target, element, FixtureConsumptionKg, (byte)Width, (byte)Height, consumed.index);
            else if (!LiquidTransferMode) SimMessages.ConsumeMass(target, element, FixtureConsumptionKg, RadialConsumptionMode ? (byte)3 : (byte)0, consumed.index);
            transfersAvoidedNativeDispatch = before == NativeBackendObservation.Capture().Hooks.Single(hook => hook.Name == "message").Entries;
            Require(transfersAvoidedNativeDispatch && transferPacketsRouted == (LiquidTransferMode ? 1 : 2), "Game fluid messages bypassed custom native routing.");
            transfersSubmitted = true;
        }
        private static void SubmitLiquidConsumption()
        {
            var before = NativeBackendObservation.Capture().Hooks.Single(hook => hook.Name == "message").Entries;
            SimMessages.ConsumeMass(target, SimHashes.Water, .02f, (byte)0, transferConsumptionCallback);
            transfersAvoidedNativeDispatch &= before == NativeBackendObservation.Capture().Hooks.Single(hook => hook.Name == "message").Entries;
            Require(transfersAvoidedNativeDispatch && transferPacketsRouted == 2, "Liquid consumption bypassed custom native routing.");
            transferConsumptionSubmitted = true;
        }
        private static bool OwnsPublicCell(int cell) => cell >= 0 && cell % Grid.WidthInCells >= x && cell % Grid.WidthInCells < x + Width &&
            cell / Grid.WidthInCells >= y && cell / Grid.WidthInCells < y + Height;
        private static SimHashes SpeciesHash(uint species) => species == 0 ? OniNitrogenElements.Gas : species == 1 ? SimHashes.Oxygen :
            species == 2 ? SimHashes.Helium : species == 3 ? SimHashes.Water : species == 4 ? SimHashes.CarbonDioxide : throw new ArgumentOutOfRangeException(nameof(species));
        internal static bool TryHandleTransfers(int message, int length, int count, byte* payload)
        {
            if (!active || (message != 1727657959 && message != 797274363) || count == 0) return false;
            try
            {
                if(payload==null || count<1 || length<1) throw new ArgumentException("Native fluid transfer payload is missing.");
                var plan=NativeFluidTransferPlan.Decode(message,length,count,new ReadOnlySpan<byte>(payload,checked(length*count)),OwnsPublicCell,ActorFluidSpecies,Grid.WidthInCells);
                if(plan.Length==0) return false;
                var requests = new TransferCommand[plan.Length];
                for (var attempt = 0; attempt < 8; ++attempt)
                {
                    var source = Status.Empty; Check(Copy(ref source, new Cell[cells.Length], (uint)cells.Length));
                    for (var i = 0; i < plan.Length; ++i)
                    {
                        var edit=plan[i];
                        requests[i] = new TransferCommand { Size = 64, Version = 2, Session = source.Session, ExpectedFrame = source.CompletedFrames,
                            PublicCell = checked((uint)edit.Cell), Species = edit.Species, Kind = edit.Injection ? 1U : 2U,
                            Flags = edit.Flags, Callback = edit.Callback, MassKg = edit.MassKg, Temperature = edit.Temperature };
                    }
                    var result = TransferStatus.Empty; var code = QueueTransfers(requests, (uint)count, ref result);
                    if (code == 3) continue;
                    Check(code); transferStatus = result; transferPacketsRouted += count;
                    radialConsumptionMessagesRouted+=plan.Count(edit=>((edit.Flags>>8)&255)!=0 && (edit.Flags>>16)==0);
                    rectangularConsumptionMessagesRouted+=plan.Count(edit=>(edit.Flags>>16)!=0);ObserveLiveBreathingRequests(requests);return true;
                }
                throw new InvalidOperationException("Native transfer frame kept changing during reservation.");
            }
            catch (Exception caught) { Fail(caught); throw; }
        }
        internal static bool TryHandleHeat(int message, int length, int count, byte* payload)
        {
            if (!active || message != 818320644 || count == 0) return false;
            try
            {
                if (length != 16 || count < 0 || payload == null) throw new ArgumentException("Invalid native fluid heat batch.");
                _ = checked(length * count);
                var owned = 0;
                for (var index = 0; index < count; ++index)
                    if (OwnsPublicCell(*(int*)(payload + index * length))) ++owned;
                if (owned == 0) return false;
                if (count > 256) throw new NotSupportedException("Native fluid heat batches are limited to 256 reserved edits.");
                if (owned != count) throw new NotSupportedException("Mixed native/custom heat batches need a cross-owner reservation.");
                var requests = new HeatCommand[count];
                for (var attempt = 0; attempt < 8; ++attempt)
                {
                    var source = Status.Empty; Check(Copy(ref source, new Cell[cells.Length], (uint)cells.Length));
                    for (var index = 0; index < count; ++index)
                    {
                        var data = new ReadOnlySpan<byte>(payload + index * length, length);
                        requests[index] = new HeatCommand { Size = 48, Version = 1, Session = source.Session, ExpectedFrame = source.CompletedFrames,
                            PublicCell = checked((uint)BinaryPrimitives.ReadInt32LittleEndian(data)),
                            Joules = 1000.0 * BitConverter.Int32BitsToSingle(BinaryPrimitives.ReadInt32LittleEndian(data.Slice(4))),
                            MaximumTemperature = BitConverter.Int32BitsToSingle(BinaryPrimitives.ReadInt32LittleEndian(data.Slice(8))),
                            Source = unchecked((uint)BinaryPrimitives.ReadInt32LittleEndian(data.Slice(12))) };
                    }
                    var output = HeatStatus.Empty; var result = QueueHeat(requests, (uint)count, ref output);
                    if (result == 3) continue;
                    Check(result); heatStatus = output; heatPacketsRouted += count; return true;
                }
                throw new InvalidOperationException("Native heat source frame kept changing during reservation.");
            }
            catch (Exception caught) { Fail(caught); throw; }
        }
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
            currentSnapshot?.ClearEventsForFault();
            releaseTransferCallbacks?.Invoke(); releaseTransferCallbacks = null;
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
            var managedGuard = rejected && stream.Length == 0 &&
                NativeBackendObservation.Capture().Hooks.Single(hook => hook.Name == "begin-save").Entries == before;
            Require(managedGuard, "Managed save began despite the nonpersistent domain guard.");
            var size = -1; var pointer = ProbeNativeBeginSave(&size, Grid.WidthInCells, Grid.HeightInCells);
            nativeSaveGuardVerified = pointer == null && size == 0 &&
                (Marshal.PtrToStringAnsi(LastError()) ?? "").Contains("saving is disabled") &&
                NativeBackendObservation.Capture().Hooks.Single(hook => hook.Name == "begin-save").Entries == before + 1;
            // No native save started, so EndSave must not be called here.
            saveGuardVerified = managedGuard && nativeSaveGuardVerified;
            Require(saveGuardVerified, "Native save began despite the nonpersistent domain guard.");
        }
        private static void WriteReport() => File.WriteAllText(ReportPath, ReportJson.Serialize(new {
            Kind = "actual-game-sealed-native-fluid-domain", Status = completed ? "passed" : failed ? "failed" : "pending", Error = error,
            Platform = Application.platform.ToString(), Model = "oni-sealed-domain-v2", Scenario = MixedPhaseMode ? "gas-liquid-mixture" : Liquid ? GravityMode ? "compressed-liquid-gravity" : "compressed-liquid" : GravityMode ? "mixed-air-gravity" : "mixed-air",
            EnergyRepresentation = (restoredScenarioSelected ? restoredScenario==3 : LiquidGravityMode) ? "water-anomaly-about-300K-plus-original-reference-from-mass" : "full-internal-energy",
            NativeDomain = latest, NativeCellsUnchanged = nativeCellsUnchanged, NativeSnapshotCopied = nativeSnapshotVerified,
            MixedPhase = MixedPhaseReport(),
            PhysicalViscosity = ViscosityReport(),
            NativeBodyHeat = ChunkHeatReport(),
            NativeSolidHeat = NativeSolidHeatReport(),
            RenderingInputs = RenderingInputs(),
            GpuGasTextures = GpuGasReport(),
            NativeSolidsAdvanced = nativeSolidsAdvanced, RetainedFrameVerified = retainedFrameVerified, Publications = publications,
            NativeFlowControlObserved = nativeFlowControlObserved, ProjectionPrimed = projectionPrimed,
            NativeFlowFenceVerified = nativeFlowFenceVerified, ImportedNativeInventory = InitialInventoryReport(), NativeProjectionMasses = nativeProjectionMasses,
            NativeProjectionTemperatures = nativeProjectionTemperatures,
            NativeRendererAdvanced = nativeRendererAdvanced, SaveGuardVerified = saveGuardVerified,
            NativeSaveGuardVerified = nativeSaveGuardVerified,
            PersistentActors = PersistentActorsReport(),
            NativeOrbitalReferences = NativeOrbitalReferenceReport(),
            GameGridProjectionVerified = gameGridProjectionVerified, ProjectionPrimedAtNativeFrame = projectionPrimedAtFrame,
            HeatCommands = heatStatus, HeatCommandsVerified = heatVerified, HeatPacketsRouted = heatPacketsRouted,
            ThermalConduction = thermalStatus, ThermalConductionVerified = thermalVerified,
            Gravity = gravityStatus, GravityVerified = gravityVerified, ProjectedPotentialJoules = GravityMode ? ProjectedPotential : 0,
            LiquidGravityVerified = Liquid && GravityMode && gravityVerified,
            HeatAvoidedNativeDispatch = heatAvoidedNativeDispatch, HeatBeforeEnergyJoules = heatBeforeEnergy,
            HeatEnergyResidualJoules = heatEnergyResidual,
            LiquidHeatVerified = LiquidHeatMode && heatVerified && thermalVerified,
            MixedHeatVerified = MixedHeatMode && !ColonyRestoreMode && heatVerified && thermalVerified,
            HeatEnergyScaleJoules = heatEnergyScale, HeatEnergyBoundJoules = heatEnergyBound,
            GasTransfers = transferStatus, TransferPacketsRouted = transferPacketsRouted, ReceiptsPublished = receiptsPublished,
            RadialConsumptionMessagesRouted = radialConsumptionMessagesRouted,
            RectangularConsumption = RectangularConsumptionReport(),
            RadialConsumption = RadialConsumptionReport(),
            CellMassEditsRouted = cellMassEditsRouted,
            CarbonParticle = CarbonParticleReport(),
            TransientParticles = TransientParticlesReport(),
            BreathingCellReader = BreathingReadReport(),
            LiveBreathing = LiveBreathingReport(),
            NativePressureReader = PressureReadReport(),
            ColonyClock = ColonyClockReport(), NaturalAutosave = NaturalAutosaveReport(),
            RestoredNativeReaders = RestoredReadersReport(),
            RegisteredActors = ActorReport(),
            PhysicalCheckpoint = CheckpointReport(),
            PairedCheckpoint = PairedCheckpointReport(), ColonyCheckpoint = ColonyCheckpointReport(),
            InProcessReload = InProcessReloadReport(),
            GasTransfersVerified = transfersVerified, TransfersAvoidedNativeDispatch = transfersAvoidedNativeDispatch,
            LiquidTransfersVerified = LiquidTransferMode && transfersVerified,
            FluidTransfers = transferStatus,
            LiquidInjectionReceipt = liquidInjectionReceipt, LiquidExtractionReceipt = liquidExtractionReceipt,
            TransferEnergyScaleJoules = transferEnergyScale, TransferEnergyBoundJoules = transferEnergyBound,
            CarbonDioxideTransfersVerified = CarbonMode && transfersVerified,
            EmissionCallbacks = emissionCallbacks, ConsumptionCallbacks = consumptionCallbacks,
            EmissionResult = emissionResult, ConsumptionResult = consumptionResult,
            TransferEnergyResidualJoules = transferEnergyResidual, TransferMaximumRelativeMassError = transferMassError,
            MixtureWorldRendering.TextureWrites, MixtureWorldRendering.LastTexturePublicationGeneration,
            NativeFlowExclusion = "Impermeable scalar compatibility cells plus gas-interaction and per-cell native phase hooks",
            TargetReading = publishedReadings != null && publishedReadings.TryGetValue(target, out var reading) ? reading : null,
            BoundedFluidAuthority = completed && active && !failed, CustomDomainActivated = active,
            WholeColonyFluidAuthority = false, FirstPlayableColonyPassed = false,
            OwnershipScope = "Sealed six-cell domain with explicitly admitted point actors; native ONI owns the surrounding colony", SavesAllowed = !SaveBlocked,
            PairedColonySaveAdmissionEnabled=active && !failed,
            Width, Height, X = x, Y = y, TargetCell = target, RenderedRevision = renderedRevision
        }));
        internal static void Shutdown()
        {
            MixtureInspection.CellHoverObserver -= ObserveNativeHover;
            ReleasePrivateSaveCallbacks();
            ReleaseRestoredNativeEvents(false);
            ReleasePairedCheckpointFixture();
            releaseTransferCallbacks?.Invoke(); releaseTransferCallbacks = null;
            currentSnapshot?.Dispose(); currentSnapshot = null;
            ResetActorBindings();
            if (active) { var stopped = Status.Empty; Check(Stop(ref stopped)); }
            active = false; arming = false; restoredScenarioSelected=false;
            ObserveReloadShutdown();
            ResetEndedNativeSession();
        }
        private static void Require(bool value, string message) { if (!value) throw new InvalidOperationException(message); }
        private static void Check(int value) { if (value != 0) throw new InvalidOperationException("Native sealed domain " + value + ": " + Marshal.PtrToStringAnsi(LastError())); }
        // Match the inspected private Sim import, bypassing its managed save
        // wrapper only for the explicit rejection control.
        [DllImport("SimDLL", EntryPoint = "SIM_BeginSave")]
        private static extern byte* ProbeNativeBeginSave(int* size, int width, int height);

    }
    [HarmonyPatch(typeof(Sim), nameof(Sim.Save))]
    internal static class NativeSealedDomainSaveGuard
    {
        private static bool Prefix(BinaryWriter __0,int __1,int __2)
        {
            if (NativeSealedDomainValidation.TryWriteStagedColonySave(__0,__1,__2)) return false;
            if (NativeSealedDomainValidation.SaveBlocked) throw new InvalidOperationException("The sealed native development domain requires a staged paused checkpoint; scalar saving is disabled.");
            return true;
        }
    }
}
