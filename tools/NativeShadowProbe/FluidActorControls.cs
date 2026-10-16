using System.Runtime.InteropServices;

internal static unsafe partial class Program
{
    private const int ActorEmitterCell = 44 + 40 * Width;
    private const int ConsumerRegistration = 101, EmitterRegistration = 102;
    private readonly record struct ComponentEvent(int Callback, int Handle);
    private readonly record struct RemovedMassEvent(int Handle, ushort Element, byte Disease, float Mass, float Temperature, int DiseaseCount);
    private readonly record struct EmitterMassSlot(ushort Element, byte Disease, float Mass, float Temperature, int DiseaseCount);

    // Inspected public Pack4 GameDataUpdate/event records on ONI 744825 x64.
    // These copies never retain native pointers beyond the synchronous query.
    private static T[] ReadEvents<T>(IntPtr snapshot, int offset, int stride, Func<IntPtr, T> read)
    {
        var count = Marshal.ReadInt32(snapshot, offset);
        var data = Marshal.ReadIntPtr(snapshot, offset + 4);
        Require(count >= 0 && count <= Cells * 4 && (count == 0 || data != IntPtr.Zero), "Invalid native actor event buffer.");
        var result = new T[count];
        for (var i = 0; i < count; ++i) result[i] = read(data + checked(i * stride));
        return result;
    }
    private static float ReadFloat(IntPtr data, int offset) => BitConverter.Int32BitsToSingle(Marshal.ReadInt32(data, offset));
    private static ComponentEvent[] ReadComponentEvents(IntPtr snapshot) => ReadEvents(snapshot, 280, 8,
        data => new ComponentEvent(Marshal.ReadInt32(data), Marshal.ReadInt32(data, 4)));
    private static RemovedMassEvent[] ReadRemovedMass(IntPtr snapshot) => ReadEvents(snapshot, 292, 20,
        data => new RemovedMassEvent(Marshal.ReadInt32(data), unchecked((ushort)Marshal.ReadInt16(data, 4)), Marshal.ReadByte(data, 6),
            ReadFloat(data, 8), ReadFloat(data, 12), Marshal.ReadInt32(data, 16)));
    private static EmitterMassSlot[] ReadEmitterMassSlots(IntPtr snapshot) => ReadEvents(snapshot, 304, 16,
        data => new EmitterMassSlot(unchecked((ushort)Marshal.ReadInt16(data)), Marshal.ReadByte(data, 2),
            ReadFloat(data, 4), ReadFloat(data, 8), Marshal.ReadInt32(data, 12)));

    private static bool IsActorChamberWall(int cell) => new[] { OxygenCell, ActorEmitterCell }.Any(center =>
        cell != center && Math.Abs(cell % Width - center % Width) <= 1 && Math.Abs(cell / Width - center / Width) <= 1);

    private static void VerifyFluidActors(NativeSim sim, Contract contract, Snapshot initial, Dictionary<string, object?> report)
    {
        Stage("register-native-fluid-actors");
        sim.Send(2024405073, WritePayload(writer =>
        { writer.Write(OxygenCell); writer.Write(ConsumerRegistration); writer.Write((byte)1); writer.Write((byte)0); writer.Write((ushort)contract.Oxygen); }));
        sim.Send(-505471181, WritePayload(writer =>
        { writer.Write(10000f); writer.Write(EmitterRegistration); writer.Write(-1); writer.Write(-1); }));
        var registrations = new List<ComponentEvent>();
        var removed = new List<RemovedMassEvent>();
        var emitted = new List<EmitterMassSlot>();
        var trace = new List<Snapshot>();
        report["frames"] = trace;
        report["actorRegistrations"] = registrations;
        report["actorRemovedMass"] = removed;
        report["actorEmittedMass"] = emitted;
        void Observe(Snapshot snapshot)
        {
            registrations.AddRange(snapshot.ComponentEvents);
            removed.AddRange(snapshot.RemovedMass);
        }
        // Component registration events are published by physical UpdateData,
        // not by synchronization-only PrepareGameData queries. This additional
        // timestep precedes rate configuration; it is counted separately.
        sim.Send(-775326397, Frame(0.2f));
        var registrationFrame = sim.PrepareAfterFrame(contract, Observe); trace.Add(registrationFrame);
        Require(registrationFrame.FramesProcessed == 1 && registrations.Count == 2 && removed.Count == 0 &&
            registrationFrame.EmitterMassSlots.All(slot => slot.Mass == 0),
            "Registration timestep did not publish exactly two inactive actor registrations.");
        var consumer = registrations.Single(item => item.Callback == ConsumerRegistration).Handle;
        var emitter = registrations.Single(item => item.Callback == EmitterRegistration).Handle;
        Require(consumer >= 0 && emitter >= 0, "Invalid native actor registration handle.");

        void Configure(float consumption, float emission)
        {
            sim.Send(1575539738, WritePayload(writer =>
            { writer.Write(consumer); writer.Write(OxygenCell); writer.Write(consumption); }));
            sim.Send(403589164, WritePayload(writer =>
            {
                writer.Write(emitter); writer.Write(ActorEmitterCell); writer.Write(0.2f); writer.Write(emission);
                writer.Write(293.15f); writer.Write(10000f); writer.Write(0); writer.Write((ushort)contract.Oxygen);
                writer.Write((byte)1); writer.Write((byte)255);
            }));
        }
        Configure(0.25f, 0.03125f);
        double expectedConsumed = 0, expectedEmitted = 0;
        for (var i = 0; i < 12; ++i)
        {
            if (i == 4) { Stage("modify-native-fluid-actors"); Configure(0.125f, 0.0625f); }
            if (i == 8)
            {
                Stage("remove-native-fluid-actors");
                sim.Send(894417742, WritePayload(writer => { writer.Write(consumer); writer.Write(-1); }));
                sim.Send(-1524118282, WritePayload(writer => { writer.Write(emitter); writer.Write(-1); }));
            }
            var removedBefore = removed.Count; var emittedBefore = emitted.Count;
            sim.Send(-775326397, Frame(0.2f));
            var snapshot = sim.PrepareAfterFrame(contract, Observe); trace.Add(snapshot);
            Require(snapshot.FramesProcessed == 1, "Actor control must complete one physical frame at a time.");
            var active = i < 8;
            if (active)
            {
                // Emitted-mass entries are handle-indexed status slots, not
                // callback events. Read the live handle once per physical
                // publication. Freed/inactive slots may contain placeholders;
                // chamber mass below proves no emission after removal.
                var emitterIndex = emitter & 0x00ffffff;
                Require(emitterIndex < snapshot.EmitterMassSlots.Length, "Missing live emitter mass slot.");
                var sample = snapshot.EmitterMassSlots[emitterIndex];
                emitted.Add(sample);
                expectedConsumed += (i < 4 ? 0.25 : 0.125) * 0.2;
                // In this pinned native build, Modify resets the interval
                // clock. Update tests it before adding dt, so the first step
                // after each modification waits, then emits the configured
                // mass once per interval. Waiting slots are still recorded.
                var expectedEmission = i % 4 == 0 ? 0 : i < 4 ? 0.03125 : 0.0625;
                Require(Math.Abs(sample.Mass - expectedEmission) <= 2e-6, "Native emitter interval/reset cadence differs at frame " + i);
                expectedEmitted += expectedEmission;
            }
            Require(removed.Count - removedBefore == (active ? 1 : 0) && emitted.Count - emittedBefore == (active ? 1 : 0),
                "Actor event count differs from active/removal schedule at frame " + i + ": removed=" + (removed.Count - removedBefore) + ", emitted=" + (emitted.Count - emittedBefore));
            Require(removed.All(item => item.Handle == consumer && item.Element == contract.Oxygen && item.Disease == 255 && item.DiseaseCount == 0 && item.Mass > 0 && float.IsFinite(item.Temperature)),
                "Consumer event identity, mass or metadata differs.");
            Require(emitted.All(item => item.Mass >= 0 && float.IsFinite(item.Mass) && float.IsFinite(item.Temperature) &&
                (item.Mass == 0 || (item.Element == contract.Oxygen && item.Disease == 255 && item.DiseaseCount == 0 && item.Temperature > 0))),
                "Emitter sample identity, mass or metadata differs.");
            Require(Math.Abs(removed.Sum(item => (double)item.Mass) - expectedConsumed) <= 2e-6 &&
                Math.Abs(emitted.Sum(item => (double)item.Mass) - expectedEmitted) <= 2e-6, "Actor rate or modified rate differs.");
            Require(Math.Abs(snapshot.CellData[OxygenCell].Mass - (2 - expectedConsumed)) <= 2e-6 &&
                Math.Abs(snapshot.CellData[ActorEmitterCell].Mass - expectedEmitted) <= 2e-6, "Sealed native actor chamber mass ledger differs.");
            Require(Math.Abs(snapshot.FluidMass - (initial.FluidMass - expectedConsumed + expectedEmitted)) <= 1e-4,
                "Native actor world mass ledger differs.");
        }
        Require(registrations.Count == 2 && registrations.Count(item => item.Callback == ConsumerRegistration) == 1 &&
            registrations.Count(item => item.Callback == EmitterRegistration) == 1, "Actor registration callback was lost or duplicated.");
        Require(trace[^1].ColdTemperature > initial.ColdTemperature && trace[^1].HotTemperature < initial.HotTemperature,
            "Native nonfluid heat stopped during fluid actor control.");
        Require(sim.Diagnostics.Count == 0, "Native simulator reported diagnostic errors.");
        report["fluidActorRegistrationModificationRemovalPassed"] = true;
        report["actorExpectedConsumedMassKg"] = expectedConsumed;
        report["actorExpectedEmittedMassKg"] = expectedEmitted;
        report["actorPositiveEmissionSamples"] = emitted.Count(item => item.Mass > 0);
        report["actorWaitingEmissionSamples"] = emitted.Count(item => item.Mass == 0);
        report["actorInactiveFrames"] = 4;
        report["actorRegistrationFrames"] = 1;
        report["solidHeatTransferObserved"] = true;
    }
}
