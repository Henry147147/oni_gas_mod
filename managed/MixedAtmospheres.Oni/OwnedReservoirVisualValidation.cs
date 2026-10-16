using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Threading;
using HarmonyLib;
using MixedAtmospheres.Core;
using MixedAtmospheres.Core.Interop;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // Explicit disposable-colony acceptance only. Two actual isolated native
    // reservoirs own Core allocations. The surrounding native world/pipe flow,
    // heat, power and persistence are not taken over by this experiment.
    internal sealed unsafe class OwnedReservoirVisualValidation
    {
        private static OwnedGameDataSnapshot? boundarySnapshot;
        private static bool snapshotRequested;
        private static int captureThread;
        private static readonly Dictionary<MeterController, float> MeterValues = new Dictionary<MeterController, float>();
        private static readonly FieldInfo ReservoirMeter = AccessTools.Field(typeof(Reservoir), "meter");
        private readonly ColonyFluidState state;
        private readonly bool containersOnly;
        private readonly FluidMaterialCatalog catalog;
        private readonly MixturePresentationModel model;
        private readonly int evacuationCell;
        private readonly MixtureContainerVisualValidation objects;
        private readonly List<object> evidence = new List<object>();
        private readonly List<string> rejections = new List<string>();
        private readonly FluidContainerHandle a, b;
        private readonly ColonyFrameSnapshot baseline;
        private readonly string rejectedSave;
        private OwnedFluidActorSession? session;
        private OwnedFluidActorLease leaseA, leaseB;
        private Storage storageA = null!, storageB = null!;
        private int eventsA, eventsB, phase;
        private float started;
        private int startedFrame;
        private bool complete;
        internal string Caption { get; private set; } = "Preparing actual owned reservoirs";

        static OwnedReservoirVisualValidation()
        {
            MixturePresentationRuntime.SessionEnding += ReleaseCapture;
        }

        internal OwnedReservoirVisualValidation(ColonyFluidState fixtureState, FluidMaterialCatalog catalog, MixturePresentationModel model,
            IReadOnlyDictionary<string, int> placements, IReadOnlyList<int> sourceCells)
        {
            if (ColonyRuntimeValidation.Settings?.Mode != "presentation") throw new InvalidOperationException("Explicit private presentation mode is required.");
            containersOnly = ColonyRuntimeValidation.Settings?.IdleReservoirLeaseOnly == true;
            state = containersOnly ? CreateContainerOnlyAuthority(placements, catalog) : fixtureState;
            this.catalog = catalog; this.model = model;
            Require(sourceCells.Count > 0, "An explicit owned Core source cell is required.");
            evacuationCell = containersOnly ? placements["reservoir-a"] : sourceCells[0];
            var harmony = new Harmony("MixedAtmospheres.PrivateOwnedReservoirValidation");
            OwnedGasReservoirBridge.Install(harmony);
            harmony.Patch(AccessTools.Method(typeof(MeterController), nameof(MeterController.SetPositionPercent)),
                postfix: new HarmonyMethod(AccessTools.Method(typeof(OwnedReservoirVisualValidation), nameof(ObserveMeter))));
            OwnedGasReservoirBridge.FailureReporter = message => rejections.Add(message);
            rejectedSave = Path.Combine(ColonyRuntimeValidation.OutputDirectory, "owned-reservoir-rejected.sav");
            Require(!File.Exists(rejectedSave), "Rejected-save evidence path must be fresh.");
            MixturePresentationRuntime.ResetProviders();
            using (var tx = state.BeginTransaction())
            {
                // Physical volume is explicit fixture input. Native capacity is
                // an independent mass limit; neither is inferred from pressure.
                a = tx.CreateContainer("owned-native-reservoir:a", new FluidContainerDefinition(10, 1000, FluidContainerPhases.Gas));
                b = tx.CreateContainer("owned-native-reservoir:b", new FluidContainerDefinition(10, 1000, FluidContainerPhases.Gas));
                if (containersOnly)
                {
                    // Explicit new experimental input, rather than adopting
                    // the earlier read-only world fixture or native packets.
                    var inventory = catalog.CreateInventory(new Dictionary<string, double>
                        { ["nitrogen"] = 79, ["oxygen"] = 21, ["chlorine"] = .0005 }, 293.15,
                        diseaseCounts: new Dictionary<int, double> { [1234567] = .375 }, radiationQuantity: .125);
                    tx.Inject(evacuationCell, inventory, "explicit-containers-only-reservoir-input");
                    tx.TransferAllCellToContainer(evacuationCell, a);
                    tx.CreateContainer("owned-native-reservoir:empty-unbound", new FluidContainerDefinition(.5, 10, FluidContainerPhases.Gas));
                }
                else
                {
                    tx.Inject(evacuationCell, new FluidInventory(new Dictionary<string, double>(), 0,
                        diseaseCounts: new Dictionary<int, double> { [1234567] = .375 }, radiationQuantity: .125), "explicit-owned-reservoir-metadata-fixture");
                    foreach (var cell in sourceCells) tx.TransferAllCellToContainer(cell, a);
                }
                var prior = state.CaptureSnapshot(); tx.CommitFrame(prior.CompletedFrame + 1, prior.SimulationTimeSeconds + .2);
            }
            baseline = state.CaptureSnapshot();
            if (containersOnly) Require(baseline.Inventories.Count == 0 && baseline.Containers.Count == 3,
                "Containers-only eligibility fixture retained world inventory or lost an empty allocation.");
            Require(state.CaptureSnapshot().SampleContainer(a).Inventory.TotalMassKg > 0, "Reservoir source must contain conserved gas.");
            objects = MixtureContainerVisualValidation.CreateEmptyReservoirPair(placements);
            captureThread = Thread.CurrentThread.ManagedThreadId; snapshotRequested = true; started = Time.realtimeSinceStartup; startedFrame = Time.frameCount;
            WriteReport();
        }

        private static ColonyFluidState CreateContainerOnlyAuthority(IReadOnlyDictionary<string, int> placements, FluidMaterialCatalog catalog)
        {
            var anchors = placements.Values.ToArray();
            Require(anchors.Length == 2 && anchors.All(Grid.IsValidCell), "Exactly two valid native anchors are required.");
            var worldId = (int)Grid.WorldIdx[anchors[0]];
            Require(anchors.All(cell => (int)Grid.WorldIdx[cell] == worldId), "Native owners must share one explicit world.");
            var x = anchors.Min(cell => cell % Grid.WidthInCells) - 3;
            var y = anchors.Min(cell => cell / Grid.WidthInCells) - 1;
            var width = anchors.Max(cell => cell % Grid.WidthInCells) - x + 4;
            var height = anchors.Max(cell => cell / Grid.WidthInCells) - y + 5;
            Require(x >= 0 && y >= 0 && x + width <= Grid.WidthInCells && y + height <= Grid.HeightInCells,
                "Container-only declared anchor geometry lies outside the native grid.");
            var count = checked((width + 2) * (height + 2));
            using var memory = new MemoryStream(); using var writer = new BinaryWriter(memory, Encoding.UTF8, true);
            writer.Write(checked(29 + 36 * count)); writer.Write(new byte[] { 83, 73, 77, 83, 65, 86, 69, 0 }); writer.Write(15);
            writer.Write(width + 2); writer.Write(height + 2); writer.Write(x); writer.Write(y); writer.Write((byte)0);
            // Synthetic empty Core geometry only. This buffer is never sent to
            // the native simulator and adopts no native world-fluid inventory.
            for (var i = 0; i < count; ++i) { writer.Write((int)SimHashes.Vacuum); writer.Write(0f); writer.Write(0f); writer.Write(0f); }
            for (var i = 0; i < count; ++i) { writer.Write(0); writer.Write(0); }
            for (var i = 0; i < count; ++i) { writer.Write((int)SimHashes.Vacuum); writer.Write(0f); writer.Write(0f); }
            var import = NativeColonyImport.StageNewColony(Grid.WidthInCells, Grid.HeightInCells,
                new[] { new NativeWorldPlacement(worldId, width, height, x, y) }, new[] { new NativeWorldSource(worldId, memory.ToArray()) },
                new Dictionary<int, MixedAtmospheres.Core.Interop.NativeMatterPhase> { [(int)SimHashes.Vacuum] = MixedAtmospheres.Core.Interop.NativeMatterPhase.Vacuum },
                new Dictionary<int, NativeFluidBinding>(), (int)SimHashes.Vacuum);
            return ColonyFluidState.Import(import, catalog);
        }

        // Called only immediately after a native completed snapshot message.
        // The original result is still returned to Game. This owned copy is
        // privately inspected; its event channels are never replayed into Game.
        internal static void ObserveNativeSnapshot(Sim.GameDataUpdate* source)
        {
            if (!snapshotRequested) return;
            Require(Thread.CurrentThread.ManagedThreadId == captureThread, "Native snapshot was not observed on the activation thread.");
            var captured = OwnedGameDataSnapshot.Capture(source, Grid.WidthInCells, Grid.HeightInCells);
            try { captured.AssertEquivalentTo(source); }
            catch { captured.Dispose(); throw; }
            boundarySnapshot?.Dispose(); boundarySnapshot = captured; snapshotRequested = false;
        }

        private static void ObserveMeter(MeterController __instance, float __0)
        { if (MeterValues.ContainsKey(__instance)) MeterValues[__instance] = __0; }
        private static void ReleaseCapture()
        { snapshotRequested = false; boundarySnapshot?.Dispose(); boundarySnapshot = null; MeterValues.Clear(); }

        internal bool Tick()
        {
            try { return TickCore(); }
            catch (Exception error)
            {
                evidence.Add(new { Kind = "actual-owned-reservoir-validation-failure", Phase = phase, Error = error.ToString() });
                WriteReport(); throw;
            }
        }

        private bool TickCore()
        {
            if (complete) return true;
            if ((phase == 0 || phase == 1) && (boundarySnapshot == null || !objects.IsReady))
            {
                // Seconds and frames, as for the spawned-surface wait.
                Require(Time.realtimeSinceStartup - started < 30 || Time.frameCount - startedFrame <= 60, "Timed out waiting for native snapshot and reservoir spawn: " + objects.PendingReason); return false;
            }
            switch (phase++)
            {
                case 0:
                    storageA = objects.Surfaces[0].Owner.GetComponent<Storage>(); storageB = objects.Surfaces[1].Owner.GetComponent<Storage>();
                    Require(storageA.items.Count == 0 && storageB.items.Count == 0 && storageA.capacityKg == 1000 && storageB.capacityKg == 1000,
                        "Native reservoir factory did not produce empty 1000 kg storages.");
                    var router = new ExperimentalFluidMessageRouter(state, catalog,
                        new[] { new FluidMessageElementBinding(0, MixedAtmospheres.Core.Interop.NativeMatterPhase.Vacuum) }, new Dictionary<byte, int>());
                    session = OwnedGasReservoirBridge.BeginSession(state, router, true);
                    CompleteBatch(Array.Empty<OwnedContainerTransferOperation>(), false);
                    MixturePresentationRuntime.BindCompletedSource(() => session.Current?.PreparedPresentation, true, includeWorldCells: !containersOnly);
                    leaseA = OwnedGasReservoirBridge.BindNativeEmptyReservoir(storageA, a);
                    leaseB = OwnedGasReservoirBridge.BindNativeEmptyReservoir(storageB, b);
                    storageA.Subscribe(-1697596308, _ => ++eventsA); storageB.Subscribe(-1697596308, _ => ++eventsB);
                    foreach (var storage in new[] { storageA, storageB })
                    {
                        var meter = (MeterController)ReservoirMeter.GetValue(storage.GetComponent<Reservoir>());
                        Require(meter != null, "Native reservoir meter has not spawned."); MeterValues[meter] = float.NaN;
                        // Native event refresh, outside the actor operation's
                        // event count, initializes the existing meter renderer.
                        storage.Trigger(-1697596308, null);
                    }
                    eventsA = eventsB = 0;
                    Require((float)(state.CaptureSnapshot().SampleContainer(a).Inventory.TotalMassKg * .25) / storageA.capacityKg > .01f,
                        "Both nonempty native logic fixtures must exceed the declared 1% upper threshold.");
                    SetLogicThresholds(storageA, 99, 100); SetLogicThresholds(storageB, 99, 100);
                    Require(LogicValue(storageA) == 1 && LogicValue(storageB) == 1, "Native smart reservoir did not activate below 100%.");
                    // Native hysteresis uses inclusive bounds. Distinct 0/1
                    // bounds keep empty gas ON and every nonempty fixture OFF;
                    // equal 0/0 bounds would toggle at equality on each update.
                    SetLogicThresholds(storageA, 0, 1); SetLogicThresholds(storageB, 0, 1);
                    Require(LogicValue(storageA) == 0 && LogicValue(storageB) == 1, "Native logic did not distinguish owned gas from empty native packets.");
                    evidence.Add(new { Kind = "actual-native-reservoirs-bound", Passed = true, Source = a, Destination = b,
                        OwnedGasReservoirBridge.BindingCount, OwnedGasReservoirBridge.SaveIsBlocked,
                        NativeSourceItems = storageA.items.Count, NativeDestinationItems = storageB.items.Count });
                    Caption = "Waiting for ONI to spawn the newly added owner lifetime components";
                    break;
                case 1:
                    objects.ObserveReservoirBinding("reservoir-a", a); objects.ObserveReservoirBinding("reservoir-b", b);
                    objects.Surfaces[1].ExpectHazardousTrace = false;
                    objects.Select("reservoir-a"); Caption = "Actual owned reservoir · native packet list empty; complete Core mixture";
                    break;
                case 2:
                    ValidateCompleted("seeded", storageA, a); objects.ValidateSelected("reservoir-a"); Capture("26-owned-native-reservoir-source.png");
                    if (containersOnly) CheckIdleSaveLease("seeded-empty-destination");
                    break;
                case 3:
                    var sourceMass = state.CaptureSnapshot().SampleContainer(a).Inventory.TotalMassKg;
                    CompleteBatch(new[] { new OwnedContainerTransferOperation(leaseA, leaseB, sourceMass * .25) }, true);
                    Require(eventsA == 1 && eventsB == 1, "An actor operation did not emit exactly one native storage event per affected owner.");
                    Require(LogicValue(storageB) == 0, "Destination native logic did not react to owned gas.");
                    objects.Surfaces[1].ExpectHazardousTrace = true; objects.Select("reservoir-b");
                    Caption = "Conserved reservoir transfer · trace, energy and fractional metadata move together";
                    break;
                case 4:
                    ValidateCompleted("after-transfer-source", storageA, a); ValidateCompleted("after-transfer-destination", storageB, b);
                    objects.ValidateSelected("reservoir-b"); Capture("27-owned-native-reservoir-destination.png");
                    if (containersOnly) CheckIdleSaveLease("both-nonempty");
                    break;
                case 5:
                    CheckFences(); CheckSaveBlocked("active owned reservoirs");
                    Caption = "Native packet mutation, ports, deconstruction and lossy saves rejected before changes";
                    break;
                case 6:
                    CompleteBatch(new[] { OwnedContainerTransferOperation.EvacuateToCell(leaseA, evacuationCell) }, false);
                    Require(eventsA == 2 && eventsB == 1, "Evacuation did not notify exactly its affected reservoir.");
                    objects.Surfaces[0].ExpectHazardousTrace = false; objects.Select("reservoir-a");
                    Caption = "Acknowledged Core evacuation · reservoir empty; native world unchanged";
                    break;
                case 7:
                    ValidateCompleted("acknowledged-empty-source", storageA, a); objects.ValidateSelected("reservoir-a"); Capture("28-owned-native-reservoir-evacuated.png");
                    if (containersOnly)
                    {
                        Reject(() => OwnedGasReservoirBridge.ReserveIdleSaveEligibility(state.CapturePublication()), "idle eligibility after owned world evacuation");
                        evidence.Add(new { Kind = "actual-idle-eligibility-rejects-world-recovery", Passed = true, session!.WorldRecoveryRequired,
                            WorldInventories = state.CaptureSnapshot().Inventories.Count });
                    }
                    // ScreenCapture renders asynchronously. Keep this exact
                    // owned empty view alive through the capture frame.
                    Caption = "Owned reservoir empty · completed vacuum reading retained for capture";
                    break;
                case 8:
                    OwnedGasReservoirBridge.UnbindEmpty(storageA);
                    Require(OwnedGasReservoirBridge.BindingCount == 1, "Normal empty unbind removed another owner.");
                    Reject(() => session!.ReadCompleted(leaseA), "retired owner lease");
                    evidence.Add(new { Kind = "normal-empty-unbind", Passed = true, Handle = a, Revision = state.CaptureSnapshot().Revision });
                    CheckStaleStoreSource();
                    break;
                case 9:
                    var retained = state.CaptureSnapshot().SampleContainer(b).Inventory;
                    Require(retained.TotalMassKg > 0, "Forced cleanup fixture must retain nonempty owned gas.");
                    // Retire this private UI source before intentionally
                    // faulting its authority through native destruction.
                    MixturePresentationRuntime.ClearFixtures();
                    Util.KDestroyGameObject(storageB.gameObject);
                    Caption = "Forced native cleanup · custom reservoir allocation retained for recovery";
                    break;
                case 10:
                    Require(OwnedGasReservoirBridge.BindingCount == 0 && session!.Orphans.Any(item => item.Owner.Container.Equals(b)), "Forced native destruction did not retain the exact orphan allocation.");
                    var retainedSnapshot = state.CaptureSnapshot();
                    Require(retainedSnapshot.SampleContainer(b).Inventory.TotalMassKg > 0, "Forced cleanup removed custom gas.");
                    Conserved();
                    MixturePresentationRuntime.ClearFixtures();
                    OwnedGasReservoirBridge.EndSession("Disposable validation ended with explicit world and orphan recovery obligations.");
                    CheckSaveBlocked("retained orphan and unpersisted Core world after teardown");
                    File.WriteAllBytes(Path.Combine(ColonyRuntimeValidation.OutputDirectory, "owned-native-reservoir-recovery.bin"), state.Save());
                    evidence.Add(new { Kind = "forced-cleanup-retained", Passed = true, Handle = b, retainedSnapshot.Revision,
                        Allocation = retainedSnapshot.SampleContainer(b), Orphans = OwnedGasReservoirBridge.RecoveryOrphans.Select(orphan => new
                        { orphan.Owner.OwnerId, Handle = orphan.Owner.Container, orphan.Reason, orphan.Revision, orphan.DetachmentRevision, orphan.Allocation }).ToArray() });
                    objects.Dispose(); ReleaseCapture();
                    OwnedGasReservoirBridge.FailureReporter = null;
                    complete = true; Caption = "Actual reservoir ownership proof complete · full colony and persistence gates remain open";
                    break;
            }
            WriteReport(); return complete;
        }

        private void CompleteBatch(IReadOnlyList<OwnedContainerTransferOperation> operations, bool injectSnapshotFailure)
        {
            var before = state.CapturePublication(); var priorA = eventsA; var priorB = eventsB;
            using var batch = session!.Prepare(operations, before.Snapshot.CompletedFrame + 1, before.Snapshot.SimulationTimeSeconds + .2, model);
            Require(ReferenceEquals(before, state.CapturePublication()) && eventsA == priorA && eventsB == priorB,
                "Staging published custom state or native events.");
            if (operations.Count != 0)
            {
                Require(storageA.MassStored() == (float)before.Snapshot.SampleContainer(a).Inventory.TotalMassKg &&
                    storageB.MassStored() == (float)before.Snapshot.SampleContainer(b).Inventory.TotalMassKg, "Native queries observed unpublished staged mass.");
                Reject(() => OwnedGasReservoirBridge.UnbindEmpty(storageA), "unbind before batch acknowledgement");
            }
            if (injectSnapshotFailure)
            {
                var invalid = *boundarySnapshot!.Pointer; invalid.mass = null;
                var failed = false;
                try { session.PrepareSnapshot(batch, &invalid); } catch (InvalidDataException) { failed = true; }
                Require(failed && ReferenceEquals(before, state.CapturePublication()) && eventsA == priorA && eventsB == priorB,
                    "Failed native descriptor published state, events or discarded the retryable transaction.");
                evidence.Add(new { Kind = "native-snapshot-validation-retry", Passed = true, BeforeRevision = before.Snapshot.Revision });
            }
            session.PrepareSnapshot(batch, boundarySnapshot!.Pointer); var publication = session.Publish(batch);
            Require(ReferenceEquals(publication.PreparedPresentation!.Source, publication.Snapshot) &&
                ReferenceEquals(publication, state.CapturePublication().Companion), "Core and presentation were not published together.");
            publication.NativeSnapshot.AssertEquivalentTo(boundarySnapshot.Pointer);
            Require(eventsA == priorA && eventsB == priorB, "Publication emitted native notifications before consumption.");
            session.BeginConsumption(batch); session.NotifyActors(batch);
            // Private deep-owned channels have been compared and have no
            // outstanding reader. Game consumed only its original native copy.
            session.AcknowledgeSnapshotConsumption(batch); session.AcknowledgeConsumption(batch);
            Reject(() => session.AcknowledgeConsumption(batch), "duplicate batch acknowledgement");
            Require(!session.HasPendingBatch, "Completed actor batch retained its writer reservation."); Conserved();
            evidence.Add(new { Kind = "paired-completed-actor-batch", Passed = true, publication.Snapshot.Revision,
                publication.Snapshot.CompletedFrame, NativeBytes = publication.NativeSnapshot.CapturedBytes,
                NativeSnapshotConsumption = "private channel equivalence read; no copied event replay into Game", Receipts = batch.Receipts.Select(receipt => new
                { receipt.Owner.OwnerId, Handle = receipt.Owner.Container, receipt.Revision, receipt.Before, receipt.After,
                    TicketSession = receipt.Ticket.Session, TicketSequence = receipt.Ticket.Sequence }).ToArray(),
                BeforeEventsA = priorA, BeforeEventsB = priorB, EventsA = eventsA, EventsB = eventsB });
        }

        private void ValidateCompleted(string label, Storage storage, FluidContainerHandle handle)
        {
            var snapshot = state.CaptureSnapshot(); var allocation = snapshot.SampleContainer(handle);
            var frame = MixturePresentationRuntime.AcquireFrame();
            Require(frame != null && frame.Revision == snapshot.Revision && frame.TryResolveInventory(handle, out var reading) &&
                ReferenceEquals(reading, session!.Current!.PreparedPresentation!.Frame.SampleContainer(handle)), "Runtime does not use the exact completed actor view.");
            if (containersOnly) Require(!MixturePresentationRuntime.HasWorldProvider && !frame!.HasWorld && !frame.TryGetCell(evacuationCell, out _),
                "A containers-only authority replaced the native world with its synthetic vacuum geometry.");
            var expected = (float)allocation.Inventory.TotalMassKg;
            Require(storage.items.Count == 0 && storage.MassStored() == expected && storage.ExactMassStored() == expected &&
                storage.IsEmpty() == (expected == 0) && storage.RemainingCapacity() == (float)(1000 - allocation.Inventory.TotalMassKg),
                "Native storage query or native packet emptiness disagrees with completed custom allocation.");
            var percent = storage.GetComponent<SmartReservoir>().PercentFull;
            var expectedPercent = (double)expected / (double)storage.Capacity();
            var meter = (MeterController)ReservoirMeter.GetValue(storage.GetComponent<Reservoir>());
            var meterPresent = MeterValues.TryGetValue(meter, out var observed);
            var expectedMeter = (double)expected / (double)storage.capacityKg;
            // Unity Mono may retain a wider CLI floating-point intermediate
            // for a float division. Native float return/storage boundaries round
            // to IEEE single. Bound that view conversion to one single ULP;
            // the Core allocation and prepared numeric view remain exact doubles.
            var percentUlp = SingleUlp(expectedPercent); var meterUlp = SingleUlp(expectedMeter);
            evidence.Add(new { Kind = "actual-native-single-precision-ratio-comparison", Label = label, snapshot.Revision, Handle = handle,
                NativePercent = percent, ExpectedPercentDouble = expectedPercent, PercentUlp = percentUlp,
                MeterPresent = meterPresent, ObservedMeter = observed, ExpectedMeterDouble = expectedMeter, MeterUlp = meterUlp });
            Require(!float.IsNaN(percent) && Math.Abs((double)percent - expectedPercent) <= percentUlp,
                "Native smart reservoir fullness does not use completed mass.");
            Require(meterPresent && !float.IsNaN(observed) && Math.Abs((double)observed - expectedMeter) <= meterUlp,
                "Native reservoir meter was not fed the exact completed mass fraction.");
            evidence.Add(new { Kind = "actual-completed-native-queries-and-meter", Label = label, Passed = true,
                snapshot.Revision, Handle = handle, NativePacketCount = storage.items.Count, MassKg = expected, PercentFull = percent,
                MeterInputFraction = observed, LogicOutput = LogicValue(storage), Allocation = allocation });
        }

        private void CheckIdleSaveLease(string label)
        {
            var source = state.CapturePublication(); var checkpoint = state.Save(); var sourceEvents = eventsA; var destinationEvents = eventsB;
            Require(source.Snapshot.Inventories.Count == 0 && source.Snapshot.Containers.Count == 3,
                "Idle lease proof must own containers only, including its empty unbound row.");
            var lease = OwnedGasReservoirBridge.ReserveIdleSaveEligibility(source);
            try
            {
                Require(ReferenceEquals(lease.Source, source) && ReferenceEquals(lease.Snapshot, source.Snapshot) && lease.Owners.Count == 2,
                    "Eligibility did not retain the exact acknowledged source and both native owners.");
                lease.EnsureCurrent(); lease.RevalidateNativeOwners();
                foreach (var observation in lease.Owners)
                {
                    var isSource = observation.Owner.Container.Equals(a);
                    Require(isSource || observation.Owner.Container.Equals(b), "Eligibility observed a different allocation.");
                    var storage = isSource ? storageA : storageB;
                    var rotatable = storage.GetComponent<Rotatable>();
                    Require(observation.Owner.Equals(isSource ? leaseA : leaseB) &&
                        ReferenceEquals(observation.Allocation, source.Snapshot.SampleContainer(observation.Owner.Container)) &&
                        observation.Native.SavedInstanceId == storage.GetComponent<KPrefabID>().InstanceID && observation.Native.PrefabId == GasReservoirConfig.ID &&
                        observation.Native.AnchorCell == objects.Surfaces[isSource ? 0 : 1].Cell &&
                        observation.Native.WorldId == (int)Grid.WorldIdx[observation.Native.AnchorCell] &&
                        observation.Native.Orientation == (rotatable == null ? (int)Orientation.Neutral : (int)rotatable.Orientation) &&
                        observation.Native.ConstructionElementId == (int)storage.GetComponent<PrimaryElement>().ElementID &&
                        observation.Native.ConstructionMassKg == (double)storage.GetComponent<PrimaryElement>().Mass &&
                        observation.Native.CapacityKg == 1000 && observation.VolumeCubicMetres == 10,
                        "Live eligibility observation differs from the exact native instance, construction or allocation.");
                }
                Reject(() => OwnedGasReservoirBridge.ReserveIdleSaveEligibility(source), "nested live save eligibility");
                using (var tx = state.BeginTransaction()) Reject(() => tx.Commit(), "Core publication while live save eligibility is held");
                using (var tx = state.BeginTransaction())
                using (var prepared = tx.PrepareFrame(source.Snapshot.CompletedFrame + 1, source.Snapshot.SimulationTimeSeconds + .2))
                    Reject(prepared.ReserveWriter, "Core prepared writer while live save eligibility is held");
                Reject(() =>
                {
                    using var unexpected = session!.Prepare(Array.Empty<OwnedContainerTransferOperation>(),
                        source.Snapshot.CompletedFrame + 1, source.Snapshot.SimulationTimeSeconds + .2, model);
                }, "actor preparation while live save eligibility is held");
                Reject(() => OwnedGasReservoirBridge.UnbindEmpty(storageB), "native unbind while live save eligibility is held");
                Require(ReferenceEquals(session!.ReadCompleted(leaseA), source.Snapshot.SampleContainer(a)) &&
                    storageA.items.Count == 0 && storageB.items.Count == 0 &&
                    storageA.MassStored() == (float)source.Snapshot.SampleContainer(a).Inventory.TotalMassKg &&
                    storageB.MassStored() == (float)source.Snapshot.SampleContainer(b).Inventory.TotalMassKg,
                    "Eligibility blocked completed readers or changed native-empty ownership.");
                CheckSaveBlocked("live eligibility prepares Core data only: " + label);
                using (var stream = new MemoryStream())
                using (var writer = new BinaryWriter(stream, Encoding.UTF8, true))
                {
                    var serialize = AccessTools.Method(typeof(Storage), nameof(Storage.Serialize), new[] { typeof(BinaryWriter) });
                    Reject(() => serialize.Invoke(storageA, new object[] { writer }), "native Storage.Serialize while live save eligibility is held");
                    Require(stream.Length == 0, "Eligibility accidentally permitted native storage serialization.");
                }
                var copied = lease.SaveCoreCheckpoint();
                Require(copied.SequenceEqual(checkpoint) && ColonyFluidState.Restore(copied, catalog).Save().SequenceEqual(checkpoint),
                    "Eligibility did not preserve exact canonical Core checkpoint bytes.");
                File.WriteAllBytes(Path.Combine(ColonyRuntimeValidation.OutputDirectory, "owned-idle-core-" + label + ".bin"), copied);
                CheckMarkerAndFootprints(lease, label);
                CaptureNativeSimSection(lease, copied, label);
                Require(ReferenceEquals(source, state.CapturePublication()) && checkpoint.SequenceEqual(state.Save()) &&
                    sourceEvents == eventsA && destinationEvents == eventsB && !session.HasPendingBatch && OwnedGasReservoirBridge.BindingCount == 2,
                    "Idle eligibility or a rejected operation changed Core/native ownership or emitted events.");
                evidence.Add(new { Kind = "actual-native-idle-save-eligibility", Label = label, Passed = true, source.Snapshot.Revision,
                    SourceIdentityRetained = true, WorldInventories = source.Snapshot.Inventories.Count, CoreContainers = source.Snapshot.Containers.Count,
                    CheckpointBytes = copied.Length, NativeSaveStillBlocked = OwnedGasReservoirBridge.SaveIsBlocked,
                    Owners = lease.Owners.Select(observation => new { observation.Owner.OwnerId, Handle = observation.Owner.Container,
                        observation.Allocation, observation.Native.SavedInstanceId, observation.Native.PrefabId, observation.Native.WorldId,
                        observation.Native.AnchorCell, observation.Native.Orientation, observation.Native.ConstructionElementId,
                        observation.Native.ConstructionMassKg, observation.Native.CapacityKg, observation.VolumeCubicMetres,
                        Components = observation.Native.Components.Select(component => new { component.Role, component.Enabled, component.IsSpawned }).ToArray() }).ToArray(),
                    Scope = "live eligibility and exact Core preparation only; no persistent owner marker, native save permit or integrated reload" });
            }
            finally { lease.Dispose(); }
            Reject(lease.EnsureCurrent, "disposed live save eligibility");
            using (var successor = OwnedGasReservoirBridge.ReserveIdleSaveEligibility(source))
            {
                lease.Dispose(); successor.EnsureCurrent(); successor.RevalidateNativeOwners();
                Require(checkpoint.SequenceEqual(successor.SaveCoreCheckpoint()) && ReferenceEquals(source, state.CapturePublication()),
                    "Old disposal released a successor eligibility reservation or resampled Core state.");
            }
            evidence.Add(new { Kind = "actual-native-idle-eligibility-successor-safe-disposal", Label = label, Passed = true });
        }

        private void CheckMarkerAndFootprints(OwnedReservoirIdleSaveLease lease, string label)
        {
            Require(OwnedReservoirOwnerMarker.Installed, "The inert completed-prefab marker hook is absent.");
            var definition = Assets.GetBuildingDef(GasReservoirConfig.ID);
            var prefabMarkers = definition.BuildingComplete.GetComponents<OwnedReservoirOwnerMarker>();
            Require(prefabMarkers.Length == 1 && prefabMarkers[0].ReadRecord().State == OwnedReservoirMarkerState.Unassigned &&
                definition.BuildingPreview.GetComponents<OwnedReservoirOwnerMarker>().Length == 0 &&
                definition.BuildingUnderConstruction.GetComponents<OwnedReservoirOwnerMarker>().Length == 0,
                "The marker is not inert and restricted to the completed reservoir prefab.");
            lease.RevalidateNativeOwners();
            var captures = new List<OwnedReservoirFootprintObservation>();
            foreach (var pair in new[] { (storageA, a), (storageB, b) })
            {
                lease.EnsureCurrent();
                var captured = OwnedReservoirFootprintCapture.Capture(pair.Item1, lease.Snapshot, pair.Item2);
                lease.RevalidateNativeOwners();
                Require(ReferenceEquals(captured.Source, lease.Snapshot) &&
                    ReferenceEquals(captured.Allocation, lease.Snapshot.SampleContainer(pair.Item2)) &&
                    captured.Marker.State == OwnedReservoirMarkerState.Unassigned && captured.Identity.Orientation == 0,
                    "Native copied footprint changed source/allocation identity or adopted a marker.");
                // Independent neutral-shape enumeration for the actual pair;
                // do not use the production planner as its own geometry oracle.
                var expected = new HashSet<int>();
                var anchor = Grid.PosToCell(pair.Item1.gameObject);
                for (var y = 0; y < 3; ++y)
                    for (var x = -2; x <= 2; ++x)
                    {
                        var cellX = anchor % Grid.WidthInCells + x; var cellY = anchor / Grid.WidthInCells + y;
                        Require(cellX >= 0 && cellX < Grid.WidthInCells && cellY >= 0 && cellY < Grid.HeightInCells,
                            "Actual reservoir footprint wraps the native grid.");
                        var cell = cellX + cellY * Grid.WidthInCells;
                        Require(ReferenceEquals(Grid.Objects[cell, (int)ObjectLayer.Building], pair.Item1.gameObject) && !Grid.Solid[cell],
                            "Actual native building footprint is foreign, missing or solid.");
                        expected.Add(cell);
                    }
                Require(expected.Count == 15 && expected.SetEquals(captured.Cells.Select(cell => cell.Cell)),
                    "Copied footprint differs from independent actual native cell enumeration.");
                captures.Add(captured);
            }
            var validated = OwnedReservoirFootprintValidation.ValidatePair(captures);
            Require(!validated.ManifestValidated && ReferenceEquals(validated.Owners[0], captures[0]) && ReferenceEquals(validated.Owners[1], captures[1]),
                "Default-marker geometry proof claimed an assigned manifest or replaced its copied inputs.");
            var first = captures[0];
            OwnedReservoirFootprintObservation Copy(IReadOnlyList<OwnedReservoirFootprintCell> cells, IReadOnlyList<OwnedReservoirFootprintPort> ports)
                => new OwnedReservoirFootprintObservation(first.Source, first.Allocation, first.Identity, first.Marker, first.DefinitionOffsets,
                    first.BuildingCells, first.OccupyUnrotatedOffsets, first.OccupyRotatedOffsets, first.OccupyCachedCells, first.OccupyLayers,
                    first.OccupyAppliesToCells, cells, ports, first.Components);
            var badCells = first.Cells.ToArray(); var originalCell = badCells[0];
            badCells[0] = new OwnedReservoirFootprintCell(originalCell.Cell, originalCell.NativeWorldId, originalCell.CoreWorldId,
                originalCell.IsSolid, OwnedReservoirOccupant.Other);
            Reject(() => OwnedReservoirFootprintValidation.ValidatePair(new[] { Copy(badCells, first.Ports), captures[1] }), "copied foreign footprint occupant");
            var badPorts = first.Ports.ToArray(); var originalPort = badPorts[0];
            badPorts[0] = new OwnedReservoirFootprintPort(originalPort.IsInput, originalPort.Cell, originalPort.CachedCell + 1,
                originalPort.ConduitType, originalPort.ConnectionOccupant, originalPort.GasConduitPresent);
            Reject(() => OwnedReservoirFootprintValidation.ValidatePair(new[] { Copy(first.Cells, badPorts), captures[1] }), "copied stale native utility cache");
            lease.RevalidateNativeOwners();
            evidence.Add(new { Kind = "actual-native-marker-and-complete-footprints", Label = label, Passed = true,
                PrefabCompleteMarkers = prefabMarkers.Length, PreviewMarkers = 0, ConstructionMarkers = 0, ManifestValidated = false,
                Owners = captures.Select(value => new { value.Identity.SavedInstanceId, value.Identity.NativeColonyId, value.Identity.PrefabId,
                    value.Identity.DefinitionPrefabId, value.Identity.AnchorCell, value.Identity.WorldId, value.Identity.Orientation,
                    value.Identity.DefinitionWidth, value.Identity.DefinitionHeight, value.Identity.ObjectLayer,
                    value.Identity.ConstructionElementId, value.Identity.ConstructionMassKg, value.Identity.CapacityKg,
                    value.Identity.GlobalTrackerMatches,
                    MarkerState = value.Marker.State.ToString(), Handle = value.Allocation!.Handle,
                    DefinitionOffsets = value.DefinitionOffsets.Select(offset => new { offset.X, offset.Y }).ToArray(),
                    value.BuildingCells, OccupyUnrotatedOffsets = value.OccupyUnrotatedOffsets.Select(offset => new { offset.X, offset.Y }).ToArray(),
                    OccupyRotatedOffsets = value.OccupyRotatedOffsets.Select(offset => new { offset.X, offset.Y }).ToArray(),
                    value.OccupyCachedCells, value.OccupyLayers, value.OccupyAppliesToCells,
                    Cells = value.Cells.Select(cell => new { cell.Cell, cell.NativeWorldId, cell.CoreWorldId, cell.IsSolid,
                        BuildingOccupant = cell.BuildingOccupant.ToString() }).ToArray(),
                    Ports = value.Ports.Select(port => new { port.IsInput, port.Cell, port.CachedCell, port.ConduitType,
                        ConnectionOccupant = port.ConnectionOccupant.ToString(), port.GasConduitPresent }).ToArray(),
                    Components = value.Components.Select(component => new { component.Role, component.Count, component.IsSpawned, component.Enabled }).ToArray() }).ToArray(),
                Scope = "read-only exact-instance capture while eligibility held; no assigned markers, native save permit or load activation" });
            if (label == "seeded-empty-destination")
            {
                CheckNativeMarkerSerialization(storageA.GetComponent<OwnedReservoirOwnerMarker>());
                CheckPrivateFileProtocol();
            }
        }

        private void CheckPrivateFileProtocol()
        {
            var expectedHash = ColonyRuntimeValidation.Settings!.PrivateFileProtocolAssemblySha256;
            if (expectedHash.Length == 0) return;
            var path = Path.Combine(Mod.DirectoryPath, "private-validation", "GameMonoFileProtocolValidation.dll");
            var bytes = File.ReadAllBytes(path);
            using var sha = System.Security.Cryptography.SHA256.Create();
            var actualHash = BitConverter.ToString(sha.ComputeHash(bytes)).Replace("-", "").ToLowerInvariant();
            Require(actualHash == expectedHash, "The private normal-source file protocol assembly does not match its configured SHA256.");
            var assembly = Assembly.Load(bytes);
            var type = assembly.GetType("MixedAtmospheres.Validation.GameMonoFileProtocolValidation", true)!;
            var run = type.GetMethod("Run", BindingFlags.Public | BindingFlags.Static, null, new[] { typeof(string) }, null);
            Require(run != null && run.ReturnType == typeof(string), "The private file protocol entry point differs from the pinned validation contract.");
            var json = (string)run!.Invoke(null, new object[] { ColonyRuntimeValidation.OutputDirectory })!;
            File.WriteAllText(Path.Combine(ColonyRuntimeValidation.OutputDirectory, "private-file-protocol-mono.json"), json);
            var report = Newtonsoft.Json.Linq.JObject.Parse(json);
            Require(report.Value<bool>("Passed") && report.Value<bool>("MonoRuntimeDetected") &&
                report.Value<int>("ChecksPassed") == 9 && report.Value<bool>("ReportFileWriteVerified"),
                "Actual game Mono file protocol validation did not pass its pinned runtime/report contract.");
            evidence.Add(new { Kind = "actual-game-mono-private-file-protocol", Passed = true, AssemblySha256 = actualHash,
                Report = report, Scope = "separate normal-source assembly in explicit private fixture; no native colony save or publication permission" });
        }

        private void CheckNativeMarkerSerialization(OwnedReservoirOwnerMarker actual)
        {
            var firstObject = new GameObject("MA private inactive marker serialization source"); firstObject.SetActive(false);
            var secondObject = new GameObject("MA private inactive marker serialization target"); secondObject.SetActive(false);
            try
            {
                var fixture = firstObject.AddComponent<OwnedReservoirOwnerMarker>();
                var copy = secondObject.AddComponent<OwnedReservoirOwnerMarker>();
                var type = typeof(OwnedReservoirOwnerMarker);
                var template = KSerialization.Manager.GetSerializationTemplate(type);
                Require(template.serializableFields.Count == 7 && template.serializableProperties.Count == 0 && KSerialization.Manager.GetType(type.ToString()) == type,
                    "Actual native marker serialization template/type lookup differs from the primitive contract.");
                using var templateBytes = new MemoryStream();
                using (var writer = new BinaryWriter(templateBytes, Encoding.UTF8, true)) template.SerializeTemplate(writer);
                var templateReader = new FastReader(templateBytes.ToArray());
                var localTemplate = new KSerialization.DeserializationTemplate(type.ToString(), templateReader);
                var mapping = new KSerialization.DeserializationMapping(localTemplate, template);
                Require(templateReader.Position == templateBytes.Length, "Native local marker template has unconsumed bytes.");
                int Roundtrip(OwnedReservoirOwnerMarker source, OwnedReservoirOwnerMarker destination)
                {
                    using var data = new MemoryStream();
                    using (var writer = new BinaryWriter(data, Encoding.UTF8, true)) KSerialization.Serializer.SerializeTypeless(source, writer);
                    var reader = new FastReader(data.ToArray());
                    Require(mapping.Deserialize(destination, reader) && reader.Position == data.Length, "Native local marker mapping did not consume its complete data.");
                    return checked((int)data.Length);
                }
                var defaultBytes = Roundtrip(actual, copy);
                Require(actual.ReadRecord().State == OwnedReservoirMarkerState.Unassigned && copy.ReadRecord().State == OwnedReservoirMarkerState.Unassigned,
                    "Default native marker was assigned or changed by serialization.");
                void Field(string name, object value) => (type.GetField(name, BindingFlags.Instance | BindingFlags.NonPublic)
                    ?? throw new MissingFieldException(type.Name, name)).SetValue(fixture, value);
                var fixtureOwnerToken = Guid.NewGuid(); const long fixtureGeneration = 1L << 40;
                Field("schemaVersion", 1); Field("nativeColonyId", SaveLoader.Instance.GameInfo.colonyGuid.ToString("N"));
                Field("persistentOwnerToken", fixtureOwnerToken.ToString("N")); Field("coreColonyId", a.ColonyId.ToString("N"));
                Field("containerId", a.Id); Field("generation", fixtureGeneration); Field("allocationToken", a.AllocationToken.ToString("N"));
                var assignedBytes = Roundtrip(fixture, copy); var assigned = copy.ReadRecord();
                Require(assigned.State == OwnedReservoirMarkerState.Assigned && assigned.Generation == fixtureGeneration &&
                    assigned.NativeColonyId == SaveLoader.Instance.GameInfo.colonyGuid && assigned.PersistentOwnerToken == fixtureOwnerToken &&
                    assigned.CoreColonyId == a.ColonyId && assigned.ContainerId == a.Id && assigned.AllocationToken == a.AllocationToken && !assigned.Matches(a),
                    "Private native marker roundtrip lost identities, Int64 precision or stale-generation rejection.");
                Field("schemaVersion", 91); var invalidBytes = Roundtrip(fixture, copy);
                Require(copy.ReadRecord().State == OwnedReservoirMarkerState.Invalid && actual.ReadRecord().State == OwnedReservoirMarkerState.Unassigned &&
                    storageB.GetComponent<OwnedReservoirOwnerMarker>().ReadRecord().State == OwnedReservoirMarkerState.Unassigned &&
                    !firstObject.activeInHierarchy && !secondObject.activeInHierarchy && OwnedGasReservoirBridge.SaveIsBlocked,
                    "Private marker serialization repaired invalid data, changed native owners or permitted saving.");
                evidence.Add(new { Kind = "actual-native-local-marker-serialization", Passed = true, Fields = 7,
                    TemplateBytes = templateBytes.Length, DefaultBytes = defaultBytes, AssignedFixtureBytes = assignedBytes, InvalidFixtureBytes = invalidBytes,
                    AssignedFixtureGeneration = assigned.Generation, AssignedFixtureToken = fixtureOwnerToken,
                    ActualNativeOwnersRemainUnassigned = true, GlobalDirectoryReset = false,
                    Scope = "local native template/mapping and two inactive private marker-only objects; no assigned world owner or native .sav" });
            }
            finally { UnityEngine.Object.Destroy(firstObject); UnityEngine.Object.Destroy(secondObject); }
        }

        private void CaptureNativeSimSection(OwnedReservoirIdleSaveLease lease, byte[] coreBytes, string label)
        {
            // Private framing proof only. This calls the same native world
            // serializer as SaveLoader, without serializing any Storage,
            // writing a .sav, permitting public Save, or inventing owner tokens.
            Require(ProbeScope.World == null && !NativeContractCapture.IsActive, "Native-section evidence requires the actual unscoped native serializer.");
            lease.RevalidateNativeOwners();
            using var stream = new MemoryStream();
            using (var writer = new BinaryWriter(stream, Encoding.UTF8, true)) Sim.Save(writer, 0, 0);
            lease.RevalidateNativeOwners();
            stream.Position = 0;
            using var reader = new BinaryReader(stream, Encoding.UTF8, true);
            var length = reader.ReadInt32();
            Require(length > 0 && length <= ColonySaveEnvelope.MaximumPayloadBytes && stream.Length == (long)length + 4,
                "Actual native Sim.Save framing is outside the bounded inspected contract.");
            var nativeBytes = reader.ReadBytes(length);
            Require(nativeBytes.Length == length && stream.Position == stream.Length, "Actual native Sim.Save section is incomplete.");
            File.WriteAllBytes(Path.Combine(ColonyRuntimeValidation.OutputDirectory, "owned-idle-native-sim-" + label + ".bin"), nativeBytes);
            var fingerprint = catalog.Fingerprint;
            var catalogHash = Enumerable.Range(0, 32).Select(i => Convert.ToByte(fingerprint.Substring(i * 2, 2), 16)).ToArray();
            var snapshot = lease.Snapshot;
            const int envelopeMaterialModelVersion = 1;
            var completedFrame = checked((ulong)snapshot.CompletedFrame);
            var envelope = ColonySaveEnvelope.Create(744825, envelopeMaterialModelVersion, completedFrame,
                catalogHash, nativeBytes, coreBytes);
            var encoded = envelope.CopyEncodedData();
            var parsed = ColonySaveEnvelope.Parse(encoded, 744825, envelopeMaterialModelVersion, catalogHash);
            Require(parsed.NativeData.SequenceEqual(nativeBytes) && parsed.CustomData.SequenceEqual(coreBytes) &&
                parsed.CompletedFrame == completedFrame && OwnedGasReservoirBridge.SaveIsBlocked,
                "Native-section envelope changed bytes/frame identity or permitted ordinary saving.");
            File.WriteAllBytes(Path.Combine(ColonyRuntimeValidation.OutputDirectory, "owned-idle-section-envelope-" + label + ".bin"), encoded);
            evidence.Add(new { Kind = "actual-native-sim-section-envelope", Label = label, Passed = true, snapshot.Revision,
                NativeBytes = nativeBytes.Length, CoreBytes = coreBytes.Length, EnvelopeBytes = encoded.Length,
                snapshot.CompletedFrame, snapshot.MaterialModelVersion, snapshot.MaterialModelFingerprint, EnvelopeMaterialModelVersion = envelopeMaterialModelVersion,
                NativeSaveStillBlocked = OwnedGasReservoirBridge.SaveIsBlocked, UnpatchedNativeLoadTested = false,
                Scope = "in-memory native Sim section plus exact Core bytes only; no complete .sav, persistent owner manifest, Storage serialization permit or integrated reload" });
        }

        private void CheckFences()
        {
            var before = state.Save(); var mass = storageA.MassStored(); var construction = storageA.GetComponent<PrimaryElement>();
            var constructionMass = construction.Mass; var constructionElement = construction.ElementID;
            foreach (var name in new[] { "AddGasChunk", "DropAll", "ConsumeAllIgnoringDisease" })
            {
                var target = OwnedGasReservoirBridge.GuardContracts().Select(pair => pair.target)
                    .First(method => method.DeclaringType == typeof(Storage) && method.Name == name);
                Reject(() => target.Invoke(storageA, DefaultArguments(target)), "actual Storage." + name);
            }
            var portCount = OwnedGasReservoirBridge.BlockedPortCount;
            var consume = AccessTools.Method(typeof(ConduitConsumer), "Consume", new[] { typeof(float), typeof(ConduitFlow) });
            var dispense = AccessTools.Method(typeof(ConduitDispenser), "Dispense", new[] { typeof(float) });
            consume.Invoke(storageA.GetComponent<ConduitConsumer>(), new object?[] { .2f, null });
            dispense.Invoke(storageA.GetComponent<ConduitDispenser>(), new object[] { .2f });
            Require(OwnedGasReservoirBridge.BlockedPortCount == portCount + 2, "Native ports reached their bodies.");
            var destroy = AccessTools.Method(typeof(Deconstructable), nameof(Deconstructable.ForceDestroyAndGetMaterials));
            var materials = destroy.Invoke(storageA.GetComponent<Deconstructable>(), Array.Empty<object>()) as List<GameObject>;
            Require(materials != null && materials.Count == 0, "Blocked forced deconstruction did not return an empty material list.");
            Require(storageA != null && construction.Mass == constructionMass && construction.ElementID == constructionElement &&
                storageA.items.Count == 0 && storageA.MassStored() == mass && before.SequenceEqual(state.Save()), "Native fence test changed construction, gas or Core state.");
            evidence.Add(new { Kind = "actual-native-mutation-and-port-fences", Passed = true, NativeItems = storageA.items.Count,
                OwnedGasReservoirBridge.RejectedMutationCount, OwnedGasReservoirBridge.BlockedPortCount, constructionMass, constructionElement });
        }

        private void CheckSaveBlocked(string reason)
        {
            Require(OwnedGasReservoirBridge.SaveIsBlocked, "Lossy native saving was enabled: " + reason);
            Reject(() => SaveLoader.Instance.Save(rejectedSave, false, false), "actual public native save: " + reason);
            var count = OwnedGasReservoirBridge.BlockedSaveCount;
            Game.Instance.StartDelayedSave(rejectedSave, false, false);
            Require(OwnedGasReservoirBridge.BlockedSaveCount == count + 1 && !File.Exists(rejectedSave), "Delayed native saving reached its coroutine or wrote a file.");
            evidence.Add(new { Kind = "actual-save-blocked-before-side-effects", Reason = reason, Passed = true,
                FileExists = File.Exists(rejectedSave), OwnedGasReservoirBridge.BlockedSaveCount });
        }

        private void CheckStaleStoreSource()
        {
            // A stale native item reference must be rejected even though no
            // native packet is admitted to the owned source's actual item list.
            // The target is the normally unbound, empty first reservoir.
            var before = state.Save(); var sourceEvents = eventsB; var targetEvents = eventsA;
            var item = ElementLoader.FindElementByHash(SimHashes.Oxygen).substance.SpawnResource(
                Grid.CellToPosCCC(objects.Surfaces[0].Cell, Grid.SceneLayer.Ore), .01f, 293.15f, byte.MaxValue, 0, prevent_merge: true);
            var pickupable = item.GetComponent<Pickupable>(); var primary = item.GetComponent<PrimaryElement>();
            var parent = item.transform.parent; var position = item.transform.position;
            var mass = primary.Mass; var temperature = primary.Temperature; var diseaseIndex = primary.DiseaseIdx; var diseaseCount = primary.DiseaseCount;
            Require(pickupable.storage == null && storageA.items.Count == 0 && storageB.items.Count == 0, "Stale-source guard fixture was not initially empty.");
            try
            {
                pickupable.storage = storageB;
                var method = AccessTools.Method(typeof(Storage), nameof(Storage.Store)); var arguments = DefaultArguments(method);
                arguments[0] = item;
                Reject(() => method.Invoke(storageA, arguments), "actual Store stale owned source before mutation");
                Require(pickupable.storage == storageB && item.transform.parent == parent && item.transform.position == position &&
                    primary.Mass == mass && primary.Temperature == temperature && primary.DiseaseIdx == diseaseIndex && primary.DiseaseCount == diseaseCount &&
                    storageA.items.Count == 0 && storageB.items.Count == 0 && eventsA == targetEvents && eventsB == sourceEvents && before.SequenceEqual(state.Save()),
                    "Native Store mutated a stale owned source before rejecting it.");
                evidence.Add(new { Kind = "actual-stale-owned-source-store-rejected", Passed = true, Source = b, NativeSourceItems = storageB.items.Count,
                    NativeDestinationItems = storageA.items.Count, MassKg = mass, TemperatureKelvin = temperature, DiseaseIndex = diseaseIndex, DiseaseCount = diseaseCount });
            }
            finally { pickupable.storage = null; Util.KDestroyGameObject(item); }
        }

        private static object?[] DefaultArguments(MethodInfo method) => method.GetParameters().Select(parameter =>
            parameter.HasDefaultValue ? parameter.DefaultValue : parameter.ParameterType.IsValueType ? Activator.CreateInstance(parameter.ParameterType) : null).ToArray();
        private void Reject(System.Action action, string label)
        {
            try { action(); }
            catch (TargetInvocationException error) when (error.InnerException is OwnedFluidOperationRejectedException || error.InnerException is OwnedFluidSaveBlockedException)
            { rejections.Add(label + ": " + error.InnerException.Message); return; }
            catch (InvalidOperationException error) { rejections.Add(label + ": " + error.Message); return; }
            catch (OwnedFluidSaveBlockedException error) { rejections.Add(label + ": " + error.Message); return; }
            throw new InvalidOperationException("Expected guarded rejection: " + label);
        }
        private void SetLogicThresholds(Storage storage, int lowerOnPercent, int upperOffPercent)
        {
            var smart = storage.GetComponent<SmartReservoir>();
            foreach (var setting in new[] { (name: "ActivateValue", value: upperOffPercent), (name: "DeactivateValue", value: lowerOnPercent) })
            {
                // Native public names are inverted relative to the private
                // activation state: ActivateValue is its upper/OFF bound.
                var property = typeof(SmartReservoir).GetProperty(setting.name)!;
                property.SetValue(smart, Convert.ChangeType(setting.value, property.PropertyType, System.Globalization.CultureInfo.InvariantCulture));
                ObserveLogic(storage, "after " + setting.name + "=" + setting.value);
            }
            smart.Sim200ms(0);
            ObserveLogic(storage, "after explicit native logic tick");
        }
        private void ObserveLogic(Storage storage, string stage)
        {
            var handle = ReferenceEquals(storage, storageA) ? a : b; var snapshot = state.CaptureSnapshot();
            var smart = storage.GetComponent<SmartReservoir>();
            evidence.Add(new { Kind = "actual-native-reservoir-logic-observation", Stage = stage, snapshot.Revision, Handle = handle,
                CoreMassKg = snapshot.SampleContainer(handle).Inventory.TotalMassKg, NativeMassKg = storage.MassStored(), NativeCapacityKg = storage.Capacity(),
                smart.PercentFull, Percent = smart.PercentFull * 100f, PublicUpperOffValue = typeof(SmartReservoir).GetProperty("ActivateValue")!.GetValue(smart),
                PublicLowerOnValue = typeof(SmartReservoir).GetProperty("DeactivateValue")!.GetValue(smart),
                Activated = AccessTools.Field(typeof(SmartReservoir), "activated").GetValue(smart), LogicOutput = LogicValue(storage) });
        }
        private static int LogicValue(Storage storage) => storage.GetComponent<LogicPorts>().GetOutputValue(SmartReservoir.PORT_ID);
        private static double SingleUlp(double value)
        {
            Require(!double.IsNaN(value) && !double.IsInfinity(value) && value >= 0 && value <= 1, "Native visual ratio lies outside its declared range.");
            var rounded = (float)value;
            return (double)BitConverter.Int32BitsToSingle(BitConverter.SingleToInt32Bits(rounded) + 1) - (double)rounded;
        }
        private void Conserved()
        {
            var snapshot = state.CaptureSnapshot(); var actual = snapshot.Totals(); var expected = baseline.Totals();
            void Close(double lhs, double rhs) => Require(Math.Abs(lhs - rhs) <= Math.Max(double.Epsilon, 2e-12 * Math.Max(Math.Abs(lhs), Math.Abs(rhs))), "Owned reservoir conservation failed.");
            Require(actual.MassesKg.Count == expected.MassesKg.Count && actual.DiseaseCounts.Count == expected.DiseaseCounts.Count, "A constituent or metadata identity was lost.");
            foreach (var row in expected.MassesKg) { Require(actual.MassesKg.ContainsKey(row.Key), "Missing conserved constituent."); Close(actual.MassesKg[row.Key], row.Value); }
            foreach (var row in expected.DiseaseCounts) { Require(actual.DiseaseCounts.ContainsKey(row.Key), "Missing conserved disease identity."); Close(actual.DiseaseCounts[row.Key], row.Value); }
            Close(actual.TotalEnergyJoules, expected.TotalEnergyJoules); Close(actual.MomentumX, expected.MomentumX); Close(actual.MomentumY, expected.MomentumY);
            Close(actual.RadiationQuantity, expected.RadiationQuantity);
            bool Exact(FluidInventory lhs, FluidInventory rhs) => lhs.MassesKg.SequenceEqual(rhs.MassesKg) && lhs.DiseaseCounts.SequenceEqual(rhs.DiseaseCounts) &&
                lhs.TotalEnergyJoules == rhs.TotalEnergyJoules && lhs.MomentumX == rhs.MomentumX && lhs.MomentumY == rhs.MomentumY && lhs.RadiationQuantity == rhs.RadiationQuantity;
            Require(Exact(snapshot.SuppliedInventory, baseline.SuppliedInventory) && Exact(snapshot.RemovedInventory, baseline.RemovedInventory) &&
                snapshot.ExternalHeatJoules == baseline.ExternalHeatJoules, "An internal reservoir move changed an external source, sink or heat ledger.");
            evidence.Add(new { Kind = "whole-owned-state-conservation-and-exact-external-ledgers", Passed = true, snapshot.Revision,
                TotalMassKg = actual.TotalMassKg, EnergyResidualJoules = actual.TotalEnergyJoules - expected.TotalEnergyJoules,
                snapshot.SuppliedInventory, snapshot.RemovedInventory, snapshot.ExternalHeatJoules });
        }
        private void Capture(string name) { ColonyRuntimeValidation.CaptureScreenshot(name); evidence.Add(new { Screenshot = name, UnityFrame = Time.frameCount }); }
        private void WriteReport() => File.WriteAllText(Path.Combine(ColonyRuntimeValidation.OutputDirectory, "owned-native-reservoir-validation.json"), ReportJson.Serialize(new
        {
            Kind = "actual-Unity-isolated-native-empty-reservoir-ownership-proof", Passed = complete, FirstPlayableColonyPassed = false,
            Scope = "two actual isolated GasReservoir instances; Core conserved operations and shared views; private native snapshot equivalence; no pipe flow, native world projection, heat, power or integrated saves",
            ContainersOnlyCoreDomain = containersOnly,
            Phase = phase, Evidence = evidence, Rejections = rejections, Objects = objects.Surfaces, SelectedObjectEvidence = objects.Evidence,
            OwnedGasReservoirBridge.Installed, OwnedGasReservoirBridge.BindingCount, OwnedGasReservoirBridge.SaveIsBlocked,
            OwnedGasReservoirBridge.RejectedMutationCount, OwnedGasReservoirBridge.BlockedPortCount, OwnedGasReservoirBridge.BlockedSaveCount,
            SavePolicy = "native saving remains blocked for unpersisted owned world or orphan allocations, including after teardown; recovery checkpoint retained in private evidence"
        }));
        private static void Require([DoesNotReturnIf(false)] bool value, string message) { if (!value) throw new InvalidOperationException(message); }
    }
}
