using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Linq;
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
    // its cell (ONI's emitMass and germ count are per emission interval: an
    // ElementEmitter's interval is one second, a building emitter's 0.2 s)
    // unless the cell holds its
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
            // ONI's emitted-mass table: what the emitter put out since the game's
            // last positive step, and the record of the step before it, which
            // the game's components are reading.
            public float EmittedKg, EmittedGerms; public Sim.EmittedMassInfo Record; public int Slot;
            public string? Source; // what stands at the emitter's cell (a geyser, a building)
            // The registering component: a stable identity across saves.
            public SimComponent? Component; public ColonyCallbackIdentity? Identity;
            public uint[] ActivityCells = Array.Empty<uint>();
        }
        // Owned actors run in one canonical order, their persistent identity,
        // so a loaded colony queues its consumers and emitters exactly as the
        // uninterrupted one whatever order they registered in.
        private static List<KeyValuePair<int,Actor>>? ordered;
        private static List<KeyValuePair<int,Actor>> Ordered()
        {
            if(ordered!=null)return ordered;
            var complete=true;
            foreach(var actor in owned.Values)complete&=(actor.Identity??=ColonyCallbackRequests.Identify(actor.Component))!=null||actor.Component==null;
            var result=owned.ToList();
            result.Sort((a,b)=>
            {
                var x=a.Value.Identity;var y=b.Value.Identity;
                if(x==null||y==null)return x!=null?-1:y!=null?1:a.Key.CompareTo(b.Key);
                var order=x.CompareTo(y);return order!=0?order:a.Key.CompareTo(b.Key);
            });
            // An identity still unresolved (a component not yet tracked) is
            // looked up again next frame rather than fixing its order.
            if(complete)ordered=result;
            return result;
        }
        // The persistent actor answering a handle, if it has one.
        internal static ColonyCallbackIdentity? IdentityOf(int handle)
        {
            if(!owned.TryGetValue(handle,out var actor)&&!native.TryGetValue(handle,out actor))return null;
            return actor.Identity??=ColonyCallbackRequests.Identify(actor.Component);
        }
        // The handle of a running actor with this identity; null while it is
        // still registering (its native answer has not arrived).
        internal static int? HandleOf(ColonyCallbackIdentity identity)
        {
            foreach(var map in new[]{owned,native})
                foreach(var entry in map)
                {
                    entry.Value.Identity??=ColonyCallbackRequests.Identify(entry.Value.Component);
                    if(entry.Value.Identity!=null&&entry.Value.Identity.CompareTo(identity)==0)return entry.Key;
                }
            return null;
        }
        internal static bool Registering(ColonyCallbackIdentity identity)
        {
            foreach(var actor in registering.Values)
            {
                actor.Identity??=ColonyCallbackRequests.Identify(actor.Component);
                if(actor.Identity!=null&&actor.Identity.CompareTo(identity)==0)return true;
            }
            return false;
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
        // The owned emitters, for the emitted-mass table (null after a change of membership).
        private static List<Actor>? emitterTable; private static int emitterSlots; private static long emittedStep = -1;
        internal static int Consumers { get; private set; }
        internal static int Emitters { get; private set; }
        internal static int Native => native.Count + registering.Count;
        // Advances whenever an actor's activity footprint can change: a
        // registration, its cell or radius, its removal, or its move between
        // the maps (which changes its census identity).
        internal static long ActivityInputs { get; private set; }
        internal static long Queued { get; private set; }
        internal static long Delivered { get; private set; }
        internal static double DeliveredKg { get; private set; }
        internal static void Reset()
        {
            registering.Clear(); native.Clear(); owned.Clear(); replies.Clear(); callbacks.Clear(); ordered=null; emitterTable=null; ++ActivityInputs; frameTransfers.Clear(); queueing = false;
            issued = 0; emittedStep = -1; Consumers = Emitters = 0; Queued = Delivered = 0; DeliveredKg = 0; SourceKg.Clear();
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
            ++ActivityInputs;
            switch (message)
            {
                case AddConsumer:
                case AddEmitter:
                {
                    var callback = Int(payload, message == AddConsumer ? addCallback : emitterCallback); if (callback == -1) return false;
                    var actor = message == AddConsumer
                        ? new Actor { Cell = Int(payload, addCell), Kind = Configuration(payload[addConfiguration], UShort(payload, addElement)), Radius = payload[addRadius], NativeIndex = UShort(payload, addElement) }
                        : new Actor { Emitter = true, MaximumKg = Float(payload, emitterPressure), OnBlocked = Int(payload, emitterBlocked), OnUnblocked = Int(payload, emitterUnblocked) };
                    actor.Component = RegisteringComponent(callback);
                    if (!running) { registering[callback] = actor; return false; }
                    var handle = OwnedHandles + NextIndex(message == AddEmitter);
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
                    if (owned.TryGetValue(handle, out var actor)) { owned.Remove(handle); ordered = null; emitterTable = null; if (actor.Emitter) --Emitters; else --Consumers; return true; }
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
        // Harness check (docs/colony-owner.md#element-consumers-and-emitters): every consumer and emitter
        // component of the game that holds a simulation handle is run by the owner, as what it is. ONI's
        // native simulation keeps consumers and emitters in separate registries, so a loaded colony's
        // native handles are told apart by kind as well as by number.
        internal sealed class CensusReport
        {
            public int GameConsumers, GameEmitters, GameBuildingEmitters, OwnedConsumers, OwnedEmitters, Native, Unowned, WrongKind, WrongComponent, SharedHandles;
            public List<string> Problems = new List<string>();
            // The lowest and highest handle of each kind (0x7e...... are the owner's own; others came from ONI at load).
            public string ConsumerHandles = "", EmitterHandles = "";
        }
        private static readonly System.Reflection.FieldInfo ComponentHandle = HarmonyLib.AccessTools.Field(typeof(SimComponent), "simHandle");
        private static readonly System.Reflection.FieldInfo BuildingHandle = HarmonyLib.AccessTools.Field(typeof(BuildingElementEmitter), "simHandle");
        internal static CensusReport GameCensus()
        {
            var report = new CensusReport { OwnedConsumers = Consumers, OwnedEmitters = Emitters, Native = Native };
            var seen = new Dictionary<int, string>();
            var range = new[] { (low: int.MaxValue, high: int.MinValue), (low: int.MaxValue, high: int.MinValue) };
            void One(UnityEngine.Component component, int handle, bool emitter)
            {
                if (!Sim.IsValidHandle(handle)) return;
                var kind = emitter ? 1 : 0; range[kind] = (Math.Min(range[kind].low, handle), Math.Max(range[kind].high, handle));
                var name = component.GetType().Name + " of " + component.gameObject.name + $" (handle 0x{handle:x8})";
                if (seen.TryGetValue(handle, out var other)) { ++report.SharedHandles; if (report.Problems.Count < 24) report.Problems.Add($"shared handle: {name} and {other}"); }
                else seen[handle] = name;
                if (!owned.TryGetValue(handle, out var actor))
                {
                    if (native.ContainsKey(handle)) return; // not yet taken
                    ++report.Unowned; if (report.Problems.Count < 24) report.Problems.Add("not run by the owner: " + name); return;
                }
                if (actor.Emitter != emitter) { ++report.WrongKind; if (report.Problems.Count < 24) report.Problems.Add($"run as {(actor.Emitter ? "an emitter" : "a consumer")}: {name}"); }
                else if (actor.Component != null && component is SimComponent && !ReferenceEquals(actor.Component, component))
                { ++report.WrongComponent; if (report.Problems.Count < 24) report.Problems.Add($"run for {actor.Component.GetType().Name} of {(actor.Component != null ? actor.Component.gameObject.name : "a destroyed object")}: {name}"); }
            }
            foreach (var c in UnityEngine.Object.FindObjectsByType<ElementConsumer>(UnityEngine.FindObjectsInactive.Exclude, UnityEngine.FindObjectsSortMode.None)) { ++report.GameConsumers; One(c, (int)ComponentHandle.GetValue(c), false); }
            foreach (var e in UnityEngine.Object.FindObjectsByType<ElementEmitter>(UnityEngine.FindObjectsInactive.Exclude, UnityEngine.FindObjectsSortMode.None)) { ++report.GameEmitters; One(e, (int)ComponentHandle.GetValue(e), true); }
            foreach (var b in UnityEngine.Object.FindObjectsByType<BuildingElementEmitter>(UnityEngine.FindObjectsInactive.Exclude, UnityEngine.FindObjectsSortMode.None)) { ++report.GameBuildingEmitters; One(b, (int)BuildingHandle.GetValue(b), true); }
            string Span((int low, int high) r) => r.low > r.high ? "none" : $"0x{r.low:x8}..0x{r.high:x8}";
            report.ConsumerHandles = Span(range[0]); report.EmitterHandles = Span(range[1]);
            return report;
        }
        private static void Own(int handle, Actor actor) { owned[handle] = actor; ordered = null; emitterTable = null; if (actor.Emitter) ++Emitters; else ++Consumers; }
        // ONI finds an emitter's record in the emitted-mass table by its
        // handle's low 24 bits (Sim.GetHandleIndex). An emitter the owner took
        // from ONI keeps its native handle, so the owner gives no emitter of
        // its own an index one of those still uses.
        private const int HandleIndexMask = 0xffffff;
        private static int NextIndex(bool emitter)
        {
            ++issued;
            if (!emitter) return issued;
            for (var searching = true; searching;)
            {
                searching = false;
                foreach (var map in new[] { owned, native })
                    foreach (var entry in map)
                        if (entry.Value.Emitter && (entry.Key & ~HandleIndexMask) != OwnedHandles && (entry.Key & HandleIndexMask) == issued) { ++issued; searching = true; }
            }
            return issued;
        }
        // A SimComponent registers through simComponentCallbackManager; its
        // callback names the component while the registration message is sent.
        private static SimComponent? RegisteringComponent(int callback)
        {
            try
            {
                var item = Game.Instance.simComponentCallbackManager.GetItem(new HandleVector<Game.ComplexCallbackInfo<int>>.Handle { index = callback });
                return item.callbackData as SimComponent;
            }
            catch (Exception error) when (error is ArgumentException || error is IndexOutOfRangeException || error is NullReferenceException) { return null; }
        }

        // Native registration answers, in every native frame's data.
        internal static void Observe(Sim.GameDataUpdate* source)
        {
            if (registering.Count == 0) return;
            for (var i = 0; i < source->numComponentStateChangedMessages; ++i)
            {
                var answer = source->componentStateChangedMessages[i];
                if (!registering.TryGetValue(answer.callbackIdx, out var actor)) continue;
                registering.Remove(answer.callbackIdx); ++ActivityInputs;
                if (Sim.IsValidHandle(answer.simHandle)) native[answer.simHandle] = actor;
            }
        }

        // Before the owner's frame: native registrations leave ONI (direct
        // native calls, past the mod's message boundary) and the owner runs them.
        internal static int TakeNativeForReplay()
        { var migrated = native.Count; TakeNative(); return migrated; }
        private static void TakeNative()
        {
            if (native.Count == 0) return;
            ++ActivityInputs;
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
        internal static void RegisterActivityComponents()
        {
            foreach (var actor in registering.Values) ColonyActivity.Register(actor.Component, true);
            foreach (var actor in native.Values) ColonyActivity.Register(actor.Component, true);
            foreach (var actor in owned.Values) ColonyActivity.Register(actor.Component, true);
        }
        internal static void CaptureActivity(ColonyActivityCells census, ColonyActivityFootprint footprint)
        {
            CaptureMap(registering, 1); CaptureMap(native, 2); CaptureMap(owned, 3);
            void CaptureMap(Dictionary<int, Actor> map, long domain)
            {
                foreach (var pair in map)
                {
                    var actor = pair.Value;
                    ColonyActivity.BeginFootprint(footprint);
                    footprint.Radius(actor.Cell, checked((int)actor.Radius));
                    actor.ActivityCells = footprint.Finish(actor.ActivityCells);
                    census.Observe((domain << 32) | (uint)pair.Key, actor.ActivityCells);
                }
            }
        }
        // Per-frame scratch (main thread): the frame's transfers, the array
        // handed to the native queue (it copies them), and element ids by
        // index for the source ledger (Enum.ToString per emitter per frame).
        private static readonly List<OniNativeColony.Transfer> frameTransfers = new List<OniNativeColony.Transfer>();
        private static OniNativeColony.Transfer[] transferArray = new OniNativeColony.Transfer[64];
        private static bool queueing;
        private static string?[] elementIds = Array.Empty<string?>();
        private static string ElementId(Element element)
        {
            if (element.idx >= elementIds.Length) Array.Resize(ref elementIds, Math.Max(element.idx + 1, ElementLoader.elements.Count));
            return elementIds[element.idx] ??= element.id.ToString();
        }
        internal static void QueueFrame(float seconds)
        {
            TakeNative();
            if (!(seconds > 0) || owned.Count == 0) return;
            using var pipeBatch = ColonyPipePumps.BeginActorBatch();
            // A reentrant frame (never expected) uses its own list.
            var reused = !queueing; queueing = true;
            try
            {
            var list = reused ? frameTransfers : new List<OniNativeColony.Transfer>(owned.Count);
            list.Clear();
            foreach (var entry in Ordered())
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
                // A gas emitter under a shown film is held back by the cell's gas, not by the film.
                var blocked = (element.IsGas && ColonyAtmosphereReaders.Film(a.Cell, out _, out var air) ? air : view.MassKg) >= a.MaximumKg;
                if (blocked != a.Blocked)
                {
                    a.Blocked = blocked;
                    var callback = blocked ? a.OnBlocked : a.OnUnblocked;
                    if (callback != -1) callbacks.Add(new Sim.CallbackInfo { callbackIdx = callback });
                }
                if (blocked) continue;
                // ONI's emitter puts out its mass and its germs once per
                // interval (a Fertilizer Synthesizer: 2 g every 0.2 s; an
                // ElementEmitter's interval is one second).
                var germs = a.Disease != 0 && a.Interval > 0 ? a.Germs * seconds / a.Interval : 0;
                var emitted = a.Interval > 0 ? a.Mass * seconds / a.Interval : a.Mass * seconds;
                a.Source ??= SourceOf(a.Cell);
                if (!SourceKg.TryGetValue(a.Source, out var byElement)) SourceKg[a.Source] = byElement = new SortedDictionary<string, double>();
                var id = ElementId(element);
                byElement[id] = (byElement.TryGetValue(id, out var kg) ? kg : 0) + emitted;
                list.Add(new OniNativeColony.Transfer { PublicCell = (uint)a.Cell, NativeIndex = a.NativeIndex, Kind = OniNativeColony.Inject, Callback = -1,
                    MassKg = emitted, TemperatureK = a.Temperature, Disease = germs > 0 ? a.Disease : 0, DiseaseCount = germs });
                a.EmittedKg += emitted; if (germs > 0) a.EmittedGerms += germs;
            }
            if (list.Count == 0) return;
            if (pipeBatch == null)
            {
                if (transferArray.Length < list.Count) transferArray = new OniNativeColony.Transfer[Math.Max(list.Count, transferArray.Length * 2)];
                list.CopyTo(transferArray);
                OniNativeColony.Check(OniNativeColony.QueueTransfers(transferArray, (uint)list.Count));
            }
            else pipeBatch.Commit(list);
            Queued += list.Count;
            }
            finally { if (reused) { frameTransfers.Clear(); queueing = false; } }
        }

        // Registration answers and emitter callbacks go out with the frame.
        internal static void Publish(OwnedGameDataSnapshot copy)
        {
            if (replies.Count != 0) { copy.AppendEvents<Sim.ComponentStateChangedMessage>(SnapshotEventChannel.ComponentStateChangedMessages, replies.ToArray()); replies.Clear(); }
            if (callbacks.Count != 0) { copy.AppendEvents<Sim.CallbackInfo>(SnapshotEventChannel.CallbackInfo, callbacks.ToArray()); callbacks.Clear(); }
            PublishEmitted(copy);
        }
        // ONI's emitted-mass table: one record per emitter handle index, what
        // the emitter put out in the last frame ONI processed. Buildings that
        // emit a gas directly (the Fertilizer Synthesizer's natural gas, the
        // Blastshot Maker's carbon dioxide) read their record once per 200 ms
        // tick, in one of the sub ticks after the step, for their emission
        // rate and the daily report. ONI's own table has no record of an
        // emitter the owner runs: it is empty, a null address, when ONI runs
        // none. Every publication carries the owned emitters' records. They
        // change at the first publication of each positive game step, to what
        // the emitters queued since the one before, and stay until the next.
        private static void PublishEmitted(OwnedGameDataSnapshot copy)
        {
            if (Emitters == 0) return;
            if (emitterTable == null)
            {
                emitterTable = new List<Actor>(Emitters); emitterSlots = 0;
                foreach (var entry in owned)
                {
                    if (!entry.Value.Emitter) continue;
                    entry.Value.Slot = entry.Key & HandleIndexMask; emitterSlots = Math.Max(emitterSlots, entry.Value.Slot + 1);
                    emitterTable.Add(entry.Value);
                }
            }
            var table = copy.StatusTable<Sim.EmittedMassInfo>(SnapshotEventChannel.EmittedMassEntries, emitterSlots);
            var next = emittedStep != ColonySaveBarrier.PositiveSteps; emittedStep = ColonySaveBarrier.PositiveSteps;
            foreach (var a in emitterTable)
            {
                if (next)
                {
                    var germs = a.EmittedGerms >= int.MaxValue ? int.MaxValue : (int)a.EmittedGerms;
                    a.Record = new Sim.EmittedMassInfo { elemIdx = (ushort)Math.Min(a.NativeIndex, ushort.MaxValue), mass = a.EmittedKg, temperature = a.EmittedKg > 0 ? a.Temperature : 0,
                        diseaseIdx = germs > 0 ? OniNativeColony.DiseaseIdx(a.Disease) : byte.MaxValue, diseaseCount = germs };
                    a.EmittedKg = 0; a.EmittedGerms = 0;
                }
                table[a.Slot] = a.Record;
            }
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
