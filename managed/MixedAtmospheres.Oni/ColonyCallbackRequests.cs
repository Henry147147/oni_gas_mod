using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // Callback-owned requests across a save (docs/colony-owner.md#saves).
    // A breath, a consumer's draw or an emitter's answered output queued for
    // the owner's next step is in flight at a save. Owner wrapper 13 keeps it
    // in its queue place under an ordinal; MACOLP03 section 4 (MACCBR01)
    // names, per ordinal, the persistent actor that answers it. A load
    // rebinds each request to that actor's callback in the new session
    // before positive time, so the restored colony commits it exactly as the
    // uninterrupted one would. A request whose actor has no persistent
    // identity is released as an older save did (an injection keeps its mass
    // unanswered, an extraction is withdrawn) and counted.
    internal static class ColonyCallbackRequests
    {
        internal const byte Released = ColonyCallbackRequestImage.Released, Consumed = ColonyCallbackRequestImage.Consumed,
            Emitted = ColonyCallbackRequestImage.Emitted, Actor = ColonyCallbackRequestImage.Actor;
        private const int MaximumText = ColonyCallbackRequestImage.MaximumText;

        private static List<ColonyCallbackEntry>? loaded;
        internal static int SavedRequests { get; private set; }
        internal static int RestoredRequests { get; private set; }
        internal static int Rebound { get; private set; }
        internal static int ReleasedRequests { get; private set; }
        internal static int HeldTicks { get; private set; }
        internal static string LastDetail { get; private set; } = "";
        internal static bool Pending => loaded != null;
        internal static void Reset() { loaded = null; SavedRequests = RestoredRequests = Rebound = ReleasedRequests = HeldTicks = 0; LastDetail = ""; }
        internal static object Report()
        {
            uint awaiting = 0; ulong released = 0;
            if (ColonyOwner.Active) OniNativeColony.Check(OniNativeColony.CallbackRebindState(out awaiting, out released));
            return new { SavedRequests, RestoredRequests, Rebound, Released = ReleasedRequests, HeldTicks, Awaiting = awaiting, NativeReleased = released, Pending, LastDetail };
        }

        internal static ColonyCallbackIdentity? Identify(object? value)
        {
            GameObject? go; byte kind; int ordinal = 0;
            if (value is Component component && component != null)
            {
                go = component.gameObject; kind = 1;
                var peers = go.GetComponents(component.GetType()).Where(peer => peer.GetType() == component.GetType()).ToArray();
                ordinal = Array.FindIndex(peers, peer => ReferenceEquals(peer, component));
                if (ordinal < 0) return null;
            }
            else if (value is StateMachine.Instance smi) { go = smi.gameObject; kind = 2; }
            else return null;
            if (go == null) return null;
            var prefab = go.GetComponent<KPrefabID>();
            if (prefab == null || !ReferenceEquals(KPrefabIDTracker.Get().GetInstance(prefab.InstanceID), prefab)) return null;
            var type = value!.GetType().FullName ?? "";
            if (type.Length == 0 || type.Length > MaximumText) return null;
            return new ColonyCallbackIdentity { Kind = kind, Prefab = prefab.InstanceID, Ordinal = ordinal, Type = type };
        }
        private static readonly MethodInfo? GetSmi = AccessTools.Method(typeof(StateMachineController), "GetSMI", new[] { typeof(Type) });
        internal static object? Resolve(ColonyCallbackIdentity identity)
        {
            var prefab = KPrefabIDTracker.Get().GetInstance(identity.Prefab);
            if (prefab == null) return null;
            var type = AccessTools.TypeByName(identity.Type);
            if (type == null) return null;
            if (identity.Kind == 1)
            {
                var peers = prefab.gameObject.GetComponents(type).Where(peer => peer.GetType() == type).ToArray();
                return identity.Ordinal < peers.Length ? peers[identity.Ordinal] : null;
            }
            var controller = prefab.GetComponent<StateMachineController>();
            if (controller == null || GetSmi == null) return null;
            var smi = GetSmi.Invoke(controller, new object[] { type });
            return smi != null && smi.GetType() == type ? smi : null;
        }

        // A one-shot (consumed) or persistent (emitted) game callback item.
        private static ColonyCallbackEntry Describe(byte kind, Delegate? callback, object? data, string? debug)
        {
            var entry = new ColonyCallbackEntry { Kind = Released, Debug = debug ?? "" };
            if (callback == null || callback.GetInvocationList().Length != 1 || entry.Debug.Length > MaximumText) return entry;
            var method = callback.Method; var declaring = method.DeclaringType?.FullName ?? "";
            if (declaring.Length == 0 || declaring.Length > MaximumText || method.Name.Length > MaximumText) return entry;
            var target = Identify(data);
            if (target == null) return entry;
            byte binding;
            if (callback.Target == null) binding = 0;
            else if (ReferenceEquals(callback.Target, data)) binding = 1;
            else if (ClosureSingleton(callback.Target.GetType()) is object singleton && ReferenceEquals(singleton, callback.Target)) binding = 2;
            else return entry;
            entry.Kind = kind; entry.Target = target; entry.Delegate = binding; entry.DeclaringType = declaring; entry.Method = method.Name;
            return entry;
        }
        private static object? ClosureSingleton(Type type) =>
            type.Name == "<>c" ? type.GetField("<>9", BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static)?.GetValue(null) : null;
        private static Delegate? Rebuild(ColonyCallbackEntry entry, Type delegateType, object data)
        {
            var declaring = AccessTools.TypeByName(entry.DeclaringType);
            var invoke = delegateType.GetMethod("Invoke")!;
            var parameters = invoke.GetParameters().Select(p => p.ParameterType).ToArray();
            var method = declaring == null ? null : AccessTools.Method(declaring, entry.Method, parameters);
            if (method == null) return null;
            object? target = entry.Delegate switch { 0 => null, 1 => data, 2 => ClosureSingleton(declaring!), _ => null };
            if (entry.Delegate != 0 && target == null) return null;
            if (method.IsStatic != (entry.Delegate == 0)) return null;
            return target == null ? System.Delegate.CreateDelegate(delegateType, method, false) : System.Delegate.CreateDelegate(delegateType, target, method, false);
        }

        private static OniNativeColony.CallbackRecord[] Records()
        {
            OniNativeColony.Check(OniNativeColony.CopyCheckpointCallbacks(null, 0, out var count));
            if (count == 0) return Array.Empty<OniNativeColony.CallbackRecord>();
            var records = new OniNativeColony.CallbackRecord[count];
            OniNativeColony.Check(OniNativeColony.CopyCheckpointCallbacks(records, count, out var copied));
            if (copied != count) throw new InvalidOperationException("The owner's callback-owned requests changed while being read.");
            return records;
        }
        private static ColonyCallbackEntry Identify(OniNativeColony.CallbackRecord record)
        {
            if (record.Awaiting != 0)
            {
                // Restored in this session and not yet rebound: keep its saved actor.
                if (loaded == null || record.Callback < 0 || record.Callback >= loaded.Count)
                    throw new InvalidOperationException("A restored callback-owned request has no saved actor.");
                return loaded[record.Callback];
            }
            if ((record.Kind & OniNativeColony.ConsumerFlag) != 0)
            {
                var actor = ColonyActors.IdentityOf(record.Callback);
                return actor == null ? new ColonyCallbackEntry { Kind = Released } : new ColonyCallbackEntry { Kind = Actor, Target = actor };
            }
            if (record.Callback < 0) return new ColonyCallbackEntry { Kind = Released };
            try
            {
                if ((record.Kind & ~OniNativeColony.ConsumerFlag) == OniNativeColony.Inject)
                {
                    var item = Game.Instance.massEmitCallbackManager.GetItem(new HandleVector<Game.ComplexCallbackInfo<Sim.MassEmittedCallback>>.Handle { index = record.Callback });
                    return Describe(Emitted, item.cb, item.callbackData, item.debugInfo);
                }
                var consumed = Game.Instance.massConsumedCallbackManager.GetItem(new HandleVector<Game.ComplexCallbackInfo<Sim.MassConsumedCallback>>.Handle { index = record.Callback });
                return Describe(Consumed, consumed.cb, consumed.callbackData, consumed.debugInfo);
            }
            catch (Exception error) when (error is ArgumentException || error is IndexOutOfRangeException || error is NullReferenceException)
            { return new ColonyCallbackEntry { Kind = Released }; }
        }

        // Inside the save, immediately after the owner checkpoint.
        internal static byte[]? Capture()
        {
            var records = Records();
            if (records.Length == 0) return null;
            var entries = records.Select(Identify).ToList();
            SavedRequests = entries.Count;
            LastDetail = string.Join(", ", entries.GroupBy(e => e.Kind).Select(g => $"{KindName(g.Key)} {g.Count()}"));
            return ColonyCallbackRequestImage.Encode(entries);
        }
        private static string KindName(byte kind) => kind switch { Consumed => "consumed", Emitted => "emitted", Actor => "actor", _ => "released" };

        internal static void Load(byte[] bytes) { loaded = ColonyCallbackRequestImage.Decode(bytes); RestoredRequests = loaded.Count; }
        internal static void Unload() { loaded = null; }

        // After the owner restore, before positive time. Returns false while
        // an actor is still registering; final=true releases what remains.
        internal static bool TryRebind(bool final)
        {
            if (!ColonyOwner.Active) return true;
            OniNativeColony.Check(OniNativeColony.CallbackRebindState(out var awaiting, out _));
            if (awaiting == 0) { loaded = null; return true; }
            if (loaded == null || loaded.Count != awaiting)
                throw new InvalidDataException("The colony's callback-owned requests do not match their saved actors.");
            var values = new OniNativeColony.CallbackRebind[loaded.Count];
            var bound = 0; var released = 0;
            for (var k = 0; k < loaded.Count; ++k)
            {
                var entry = loaded[k]; int? callback = null;
                if (entry.Kind == Actor && entry.Target != null)
                {
                    callback = ColonyActors.HandleOf(entry.Target);
                    if (callback == null && !final && ColonyActors.Registering(entry.Target)) return false;
                }
                else if ((entry.Kind == Consumed || entry.Kind == Emitted) && entry.Target != null && Resolve(entry.Target) is object data)
                    callback = entry.Kind == Consumed ? AddConsumed(entry, data) : FindEmitted(entry, data);
                values[k] = callback is int value && value >= 0 ? new OniNativeColony.CallbackRebind { Callback = value, Bind = 1 } :
                    new OniNativeColony.CallbackRebind { Callback = 0, Bind = 0 };
                if (values[k].Bind == 1) ++bound; else ++released;
            }
            OniNativeColony.Check(OniNativeColony.RebindCallbacks(values, (uint)values.Length));
            Rebound += bound; ReleasedRequests += released; loaded = null;
            if (released != 0) Debug.LogWarning($"[Mixed Atmospheres] {released} in-flight request(s) from the save had no actor to answer and were released.");
            return true;
        }
        // Game.SimEveryTick: a positive tick waits, as a zero-time tick, until
        // every restored request is rebound (registration answers arrive with
        // zero-time native frames). After 600 such ticks the rest is released.
        internal static bool ReadyForPositiveTime()
        {
            if (loaded == null || ColonyLoadRefusal.Refused || !ColonyOwner.Active) return true;
            try { if (TryRebind(HeldTicks >= 600)) return true; }
            catch (Exception error) { ColonyLoadRefusal.Refuse(error); return false; }
            ++HeldTicks; return false;
        }
        private static int? AddConsumed(ColonyCallbackEntry entry, object data)
        {
            var callback = Rebuild(entry, typeof(Action<Sim.MassConsumedCallback, object>), data) as Action<Sim.MassConsumedCallback, object>;
            if (callback == null) return null;
            return Game.Instance.massConsumedCallbackManager.Add(callback, data, entry.Debug).index;
        }
        // Emitted-mass callbacks are persistent registrations made by their
        // component; the restored component already holds its own handle.
        private static int? FindEmitted(ColonyCallbackEntry entry, object data)
        {
            var handleType = typeof(HandleVector<Game.ComplexCallbackInfo<Sim.MassEmittedCallback>>.Handle);
            int? found = null;
            foreach (var field in AccessTools.GetDeclaredFields(data.GetType()).Where(f => f.FieldType == handleType && !f.IsStatic))
            {
                var handle = (HandleVector<Game.ComplexCallbackInfo<Sim.MassEmittedCallback>>.Handle)field.GetValue(data);
                if (!handle.IsValid()) continue;
                Game.ComplexCallbackInfo<Sim.MassEmittedCallback> item;
                try { item = Game.Instance.massEmitCallbackManager.GetItem(handle); }
                catch (Exception error) when (error is ArgumentException || error is IndexOutOfRangeException) { continue; }
                if (!ReferenceEquals(item.callbackData, data) || item.cb == null || item.cb.Method.Name != entry.Method ||
                    item.cb.Method.DeclaringType?.FullName != entry.DeclaringType) continue;
                if (found != null) return null; // ambiguous
                found = handle.index;
            }
            return found;
        }

    }
}
