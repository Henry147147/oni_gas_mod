using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using HarmonyLib;

namespace MixedAtmospheres.Oni
{
    // Element consumers and emitters on the owned asteroid
    // (docs/colony-owner.md#element-consumers-and-emitters). ONI's native
    // actors read and write one element per cell, the owner's projection, so a
    // carbon dioxide consumer in an oxygen-dominated mixed room would find
    // none. The owner runs every actor. While it runs, a registration stops
    // here and the owner answers it with its own handle; the actor's rate,
    // modify and remove messages never reach ONI. Registrations from before
    // the owner started (a new world's, or a loaded save's buildings) are
    // watched from the game's start: at the owner's first frame their native
    // registrations are removed and the owner runs them under their native
    // handles.
    //
    // Each positive frame a consumer queues rate × dt as an owner extraction
    // within its radius: its element, or the whole gas or liquid mixture for an
    // any-gas or any-liquid consumer (pumps). The receipts return as ONI's
    // consumed-mass events for the handle. An emitter injects its rate × dt at
    // its cell (ONI's emitMass is the output element's kg/s generation rate;
    // its germs are counted per emission interval) unless the cell holds its
    // maximum pressure (kg), and answers the blocked and unblocked callbacks
    // on each change.
    internal static unsafe class ColonyActors
    {
        private const int AddConsumer = 2024405073, SetConsumer = 1575539738, RemoveConsumer = 894417742;
        private const int AddEmitter = -505471181, ModifyEmitter = 403589164, RemoveEmitter = -1524118282;
        // The owner's handles: a version byte that native handles (index in
        // the low 24 bits) do not reach in one session.
        private const int OwnedHandles = 0x7e000000;

        private sealed class Actor
        {
            public bool Emitter;
            public int Cell = -1;
            public uint Kind, NativeIndex, Radius; public float Rate; // consumer
            public float Interval, Mass, Temperature, MaximumKg; public uint Disease; public int Germs; // emitter
            public int OnBlocked = -1, OnUnblocked = -1; public bool Blocked;
            public string? Source; // what stands at the emitter's cell (a geyser, a building)
        }
        // External source ledger (docs/colony-owner.md#element-consumers-and-emitters):
        // mass every emitter source kind added to the owner, per element.
        internal static readonly SortedDictionary<string, SortedDictionary<string, double>> SourceKg = new SortedDictionary<string, SortedDictionary<string, double>>();
        private static string SourceOf(int cell)
        {
            var building = Grid.IsValidCell(cell) ? Grid.Objects[cell, (int)ObjectLayer.Building] : null;
            return building != null ? building.PrefabID().Name : "unattributed";
        }
        private static readonly Dictionary<int, Actor> registering = new Dictionary<int, Actor>(); // sent native, by callback
        private static readonly Dictionary<int, Actor> native = new Dictionary<int, Actor>();      // native handle, not yet taken
        private static readonly Dictionary<int, Actor> owned = new Dictionary<int, Actor>();       // run by the owner
        private static readonly List<Sim.ComponentStateChangedMessage> replies = new List<Sim.ComponentStateChangedMessage>();
        private static readonly List<Sim.CallbackInfo> callbacks = new List<Sim.CallbackInfo>();
        private static int issued;
        internal static int Consumers { get; private set; }
        internal static int Emitters { get; private set; }
        internal static int Native => native.Count + registering.Count;
        internal static long Queued { get; private set; }
        internal static long Delivered { get; private set; }
        internal static double DeliveredKg { get; private set; }
        internal static void Reset()
        {
            registering.Clear(); native.Clear(); owned.Clear(); replies.Clear(); callbacks.Clear();
            issued = 0; Consumers = Emitters = 0; Queued = Delivered = 0; DeliveredKg = 0; SourceKg.Clear();
        }

        // The message layouts, from the game's own message structs.
        private sealed class Layout
        {
            public readonly int Size; private readonly Type type;
            public Layout(string name)
            {
                type = AccessTools.Inner(typeof(SimMessages), name) ?? throw new MissingMemberException(nameof(SimMessages), name);
                Size = Marshal.SizeOf(type);
            }
            public int this[string field] => (int)Marshal.OffsetOf(type, field);
        }
        private static Layout? addConsumer, setConsumer, removeConsumer, addEmitter, modifyEmitter, removeEmitter;
        private static int addCell, addCallback, addRadius, addConfiguration, addElement, setHandle, setCell, setRate, removeHandle, removeCallback;
        private static int emitterPressure, emitterCallback, emitterBlocked, emitterUnblocked;
        private static int modifyHandle, modifyCell, modifyInterval, modifyMass, modifyTemperature, modifyPressure, modifyGerms, modifyElement, modifyDepth, modifyDisease;
        private static void Describe()
        {
            if (addConsumer != null) return;
            var ac = new Layout("AddElementConsumerMessage"); var sc = new Layout("SetElementConsumerDataMessage"); var rc = new Layout("RemoveElementConsumerMessage");
            var ae = new Layout("AddElementEmitterMessage"); var me = new Layout("ModifyElementEmitterMessage"); var re = new Layout("RemoveElementEmitterMessage");
            addCell = ac["cellIdx"]; addCallback = ac["callbackIdx"]; addRadius = ac["radius"]; addConfiguration = ac["configuration"]; addElement = ac["elementIdx"];
            setHandle = sc["handle"]; setCell = sc["cell"]; setRate = sc["consumptionRate"];
            removeHandle = rc["handle"]; removeCallback = rc["callbackIdx"];
            if (re["handle"] != removeHandle || re["callbackIdx"] != removeCallback || re.Size != rc.Size) throw new NotSupportedException("ONI's actor removal messages differ.");
            emitterPressure = ae["maxPressure"]; emitterCallback = ae["callbackIdx"]; emitterBlocked = ae["onBlockedCB"]; emitterUnblocked = ae["onUnblockedCB"];
            modifyHandle = me["handle"]; modifyCell = me["cellIdx"]; modifyInterval = me["emitInterval"]; modifyMass = me["emitMass"]; modifyTemperature = me["emitTemperature"];
            modifyPressure = me["maxPressure"]; modifyGerms = me["diseaseCount"]; modifyElement = me["elementIdx"]; modifyDepth = me["maxDepth"]; modifyDisease = me["diseaseIdx"];
            (setConsumer, removeConsumer, addEmitter, modifyEmitter, removeEmitter, addConsumer) = (sc, rc, ae, me, re, ac);
        }
        private static int Int(byte* p, int offset) => BinaryPrimitives.ReadInt32LittleEndian(new ReadOnlySpan<byte>(p + offset, 4));
        private static float Float(byte* p, int offset) => BitConverter.Int32BitsToSingle(Int(p, offset));
        private static ushort UShort(byte* p, int offset) => BinaryPrimitives.ReadUInt16LittleEndian(new ReadOnlySpan<byte>(p + offset, 2));
        private static int Length(int message)
        {
            switch (message)
            {
                case AddConsumer: case SetConsumer: case RemoveConsumer: case AddEmitter: case ModifyEmitter: case RemoveEmitter: break;
                default: return -1;
            }
            Describe();
            return message switch
            {
                AddConsumer => addConsumer!.Size, SetConsumer => setConsumer!.Size, RemoveConsumer => removeConsumer!.Size,
                AddEmitter => addEmitter!.Size, ModifyEmitter => modifyEmitter!.Size, _ => removeEmitter!.Size
            };
        }

        // Every actor message, from the game's start. Returns true when the
        // owner took the message.
        internal static bool TryHandle(int message, int length, int count, byte* payload, bool running)
        {
            var expected = Length(message);
            if (expected < 0) return false;
            if (expected != length || count != 1 || payload == null) return false; // not ONI's own single messages
            switch (message)
            {
                case AddConsumer:
                case AddEmitter:
                {
                    var callback = Int(payload, message == AddConsumer ? addCallback : emitterCallback); if (callback == -1) return false;
                    var actor = message == AddConsumer
                        ? new Actor { Cell = Int(payload, addCell), Kind = Configuration(payload[addConfiguration], UShort(payload, addElement)), Radius = payload[addRadius], NativeIndex = UShort(payload, addElement) }
                        : new Actor { Emitter = true, MaximumKg = Float(payload, emitterPressure), OnBlocked = Int(payload, emitterBlocked), OnUnblocked = Int(payload, emitterUnblocked) };
                    if (!running) { registering[callback] = actor; return false; }
                    var handle = OwnedHandles + ++issued;
                    Own(handle, actor);
                    replies.Add(new Sim.ComponentStateChangedMessage { callbackIdx = callback, simHandle = handle });
                    return true;
                }
                case SetConsumer:
                case ModifyEmitter:
                {
                    var consumer = message == SetConsumer;
                    var handle = Int(payload, consumer ? setHandle : modifyHandle);
                    var mine = owned.TryGetValue(handle, out var actor);
                    if (!mine && !native.TryGetValue(handle, out actor)) return false;
                    actor.Cell = Int(payload, consumer ? setCell : modifyCell); actor.Source = null;
                    if (consumer) actor.Rate = Float(payload, setRate);
                    else
                    {
                        actor.Interval = Float(payload, modifyInterval); actor.Mass = Float(payload, modifyMass); actor.Temperature = Float(payload, modifyTemperature);
                        actor.MaximumKg = Float(payload, modifyPressure); actor.Germs = Int(payload, modifyGerms); actor.NativeIndex = UShort(payload, modifyElement);
                        actor.Disease = actor.Germs > 0 ? OniNativeColony.Germ(payload[modifyDisease]) : 0; actor.Radius = payload[modifyDepth];
                    }
                    return mine;
                }
                default: // RemoveConsumer, RemoveEmitter
                {
                    var handle = Int(payload, removeHandle);
                    if (owned.TryGetValue(handle, out var actor)) { owned.Remove(handle); if (actor.Emitter) --Emitters; else --Consumers; return true; }
                    native.Remove(handle); return false;
                }
            }
        }
        // An any-gas or any-liquid consumer that names an element (a mask
        // station's oxygen and polluted-oxygen consumers) takes only that
        // element: ONI stores a receipt only of its element and would delete
        // the rest. A pump names none (vacuum) and takes the mixture.
        private static uint Configuration(byte value, ushort element) =>
            element < ElementLoader.elements.Count && !ElementLoader.elements[element].IsVacuum ? OniNativeColony.Extract : (ElementConsumer.Configuration)value switch
            {
                ElementConsumer.Configuration.AllGas => OniNativeColony.ExtractGas,
                ElementConsumer.Configuration.AllLiquid => OniNativeColony.ExtractLiquid,
                _ => OniNativeColony.Extract
            };
        private static void Own(int handle, Actor actor) { owned[handle] = actor; if (actor.Emitter) ++Emitters; else ++Consumers; }

        // Native registration answers, in every native frame's data.
        internal static void Observe(Sim.GameDataUpdate* source)
        {
            if (registering.Count == 0) return;
            for (var i = 0; i < source->numComponentStateChangedMessages; ++i)
            {
                var answer = source->componentStateChangedMessages[i];
                if (!registering.TryGetValue(answer.callbackIdx, out var actor)) continue;
                registering.Remove(answer.callbackIdx);
                if (Sim.IsValidHandle(answer.simHandle)) native[answer.simHandle] = actor;
            }
        }

        // Before the owner's frame: native registrations leave ONI (direct
        // native calls, past the mod's message boundary) and the owner runs them.
        private static void TakeNative()
        {
            if (native.Count == 0) return;
            var size = removeConsumer!.Size; var remove = stackalloc byte[size];
            foreach (var entry in native)
            {
                new Span<byte>(remove, size).Clear();
                BinaryPrimitives.WriteInt32LittleEndian(new Span<byte>(remove + removeHandle, 4), entry.Key);
                BinaryPrimitives.WriteInt32LittleEndian(new Span<byte>(remove + removeCallback, 4), -1);
                Sim.SIM_HandleMessage(entry.Value.Emitter ? RemoveEmitter : RemoveConsumer, size, remove);
                Own(entry.Key, entry.Value);
            }
            native.Clear();
        }

        // At each positive native frame: this frame's consumption and
        // emission, committed at the owner's next step.
        internal static void QueueFrame(float seconds)
        {
            TakeNative();
            if (!(seconds > 0) || owned.Count == 0) return;
            using var pipeBatch = ColonyPipePumps.BeginActorBatch();
            var list = new List<OniNativeColony.Transfer>(owned.Count);
            foreach (var entry in owned)
            {
                var a = entry.Value;
                if (!a.Emitter)
                {
                    if (!(a.Rate > 0) || !Grid.IsValidCell(a.Cell)) continue;
                    if (pipeBatch != null && ColonyPipePumps.QueueConsumer(entry.Key, a.Cell, a.Kind, a.Radius, a.Rate * seconds, list, pipeBatch)) continue;
                    list.Add(new OniNativeColony.Transfer { PublicCell = (uint)a.Cell, NativeIndex = a.Kind == OniNativeColony.Extract ? a.NativeIndex : 0,
                        Kind = a.Kind | OniNativeColony.ConsumerFlag, Radius = a.Radius, Callback = entry.Key, MassKg = a.Rate * seconds });
                    continue;
                }
                if (!(a.Mass > 0) || !Grid.IsValidCell(a.Cell) || a.NativeIndex >= ElementLoader.elements.Count) continue;
                var element = ElementLoader.elements[(int)a.NativeIndex];
                if (!(element.IsGas || element.IsLiquid) || !ColonyOwner.TryReadView(a.Cell, out var view)) continue;
                var blocked = view.MassKg >= a.MaximumKg;
                if (blocked != a.Blocked)
                {
                    a.Blocked = blocked;
                    var callback = blocked ? a.OnBlocked : a.OnUnblocked;
                    if (callback != -1) callbacks.Add(new Sim.CallbackInfo { callbackIdx = callback });
                }
                if (blocked) continue;
                var germs = a.Disease != 0 && a.Interval > 0 ? a.Germs * seconds / a.Interval : 0;
                a.Source ??= SourceOf(a.Cell);
                if (!SourceKg.TryGetValue(a.Source, out var byElement)) SourceKg[a.Source] = byElement = new SortedDictionary<string, double>();
                byElement[element.id.ToString()] = (byElement.TryGetValue(element.id.ToString(), out var kg) ? kg : 0) + a.Mass * seconds;
                list.Add(new OniNativeColony.Transfer { PublicCell = (uint)a.Cell, NativeIndex = a.NativeIndex, Kind = OniNativeColony.Inject, Callback = -1,
                    MassKg = a.Mass * seconds, TemperatureK = a.Temperature, Disease = germs > 0 ? a.Disease : 0, DiseaseCount = germs });
            }
            if (list.Count == 0) return;
            if (pipeBatch == null) OniNativeColony.Check(OniNativeColony.QueueTransfers(list.ToArray(), (uint)list.Count));
            else pipeBatch.Commit(list);
            Queued += list.Count;
        }

        // Registration answers and emitter callbacks go out with the frame.
        internal static void Publish(OwnedGameDataSnapshot copy)
        {
            if (replies.Count != 0) { copy.AppendEvents<Sim.ComponentStateChangedMessage>(SnapshotEventChannel.ComponentStateChangedMessages, replies.ToArray()); replies.Clear(); }
            if (callbacks.Count != 0) { copy.AppendEvents<Sim.CallbackInfo>(SnapshotEventChannel.CallbackInfo, callbacks.ToArray()); callbacks.Clear(); }
        }

        internal static bool IsConsumerReceipt(in OniNativeColony.Receipt receipt) => (receipt.Kind & OniNativeColony.ConsumerFlag) != 0;
        internal static Sim.ConsumedMassInfo Event(in OniNativeColony.Receipt receipt)
        {
            ++Delivered; DeliveredKg += receipt.MassKg;
            return new Sim.ConsumedMassInfo { simHandle = receipt.Callback, removedElemIdx = (ushort)receipt.NativeIndex, mass = receipt.MassKg,
                temperature = receipt.MassKg > 0 ? receipt.TemperatureK : 0, diseaseIdx = OniNativeColony.DiseaseIdx(receipt.Disease), diseaseCount = receipt.DiseaseCount };
        }
    }
}
