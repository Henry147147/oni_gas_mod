using System;
using System.Collections.Generic;
using System.Reflection;
using System.Runtime.CompilerServices;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // Other mods' components that read or change gases themselves
    // (docs/colony-owner.md#other-mods-rephysicalized). Rephysicalized
    // (Workshop item 3609531831) gives every fuel burner, crop and, with its
    // RealBreathing option, critter an oxygen intake of its own,
    // Rephysicalized.DualGasElementConsumer. Its ConsumeBreathableGases reads
    // Grid.Element and Grid.Mass of each cell within its radius and, when the
    // cell's shown element is an oxidizer (oxygen, polluted oxygen), asks for
    // min(rate x dt, Grid.Mass, room) with no callback and stores that amount
    // at once. Under the colony owner a cell shows its most massive gas and
    // all its gas as its mass, so oxygen under carbon dioxide was invisible
    // to it (burners stopped, crops wilted in air with oxygen to spare), and a
    // cell holding less oxygen than its total mass gave the storage oxygen
    // that never left the world.
    //
    // While the owner runs, a prefix replaces that method: for each owned
    // cell it reads the cell's composition and asks for each oxidizer
    // constituent, most massive first, at most what that constituent holds,
    // from that cell alone (radius 0, so the amount asked is what the cell's
    // composition justified), with a consumed-mass callback; the storage is
    // credited what the owner's receipt reports (mass, temperature, germs).
    // Requests in flight count against the storage's room. A cell the owner
    // does not hold keeps the original logic. When the owner is not active
    // the original runs unchanged.
    //
    // With its RealBreathing option a suffocating critter looks for a cell
    // with oxidizer (AnimalSuffocatingStates.OxidizerCellQuery.HasOxidizer:
    // the shown element is an oxidizer gas and Grid.Mass > 0). A postfix
    // also counts an owned cell whose composition holds any oxidizer gas.
    //
    // MA_OTHER_MOD_COMPAT=off (read once, a control run only) installs
    // neither, so the original intake can be measured on the owner.
    //
    // Nothing references Rephysicalized's assembly: its type, method and
    // fields are found by name once every mod is loaded (Db.Initialize), and
    // anything missing installs nothing and says so once in Player.log.
    // Patches on other mods' code use their own Harmony owner, so that
    // GameBuildCheck.RequireVerifiedPatches and UnverifiedPatches, which
    // require this mod's owner to patch only verified game methods, keep
    // their meaning.
    internal static class RephysicalizedCompat
    {
        internal const string HarmonyId = "MixedAtmospheres.OtherMods";
        internal const string ConsumerTypeName = "Rephysicalized.DualGasElementConsumer", TagsTypeName = "Rephysicalized.ModTags";
        private static bool attempted;
        internal static bool Present { get; private set; }
        internal static bool Installed { get; private set; }
        internal static bool SuffocationInstalled { get; private set; }
        // MA_OTHER_MOD_COMPAT=off: the control run that leaves Rephysicalized's components as they are.
        internal static bool SwitchedOff { get; private set; }
        internal const string QueryTypeName = "Rephysicalized.Content.System_Patches.RealBreathing.AnimalSuffocatingStates+OxidizerCellQuery";
        internal static string Problem { get; private set; } = "";
        private static Tag oxidizer;
        // A request that waits longer than this (game seconds) for its receipt no longer holds room.
        private const double ExpireSeconds = 5;

        internal static void TryInstall()
        {
            if (attempted) return;
            attempted = true;
            try
            {
                var type = FindType(ConsumerTypeName);
                if (type == null) return; // Rephysicalized is not loaded.
                Present = true; ConsumerType = type;
                if (string.Equals(Environment.GetEnvironmentVariable("MA_OTHER_MOD_COMPAT"), "off", StringComparison.OrdinalIgnoreCase))
                {
                    SwitchedOff = true; Problem = "switched off by MA_OTHER_MOD_COMPAT=off (a control run)";
                    Debug.LogWarning("[Mixed Atmospheres] Other-mod compatibility is switched off (MA_OTHER_MOD_COMPAT=off): Rephysicalized's own oxygen intake " +
                        "and critter oxidizer search run unchanged. A control run only.");
                    return;
                }
                var missing = Missing(type, FindType(TagsTypeName), out var method, out var tagField);
                if (missing.Length != 0)
                {
                    Problem = missing;
                    Debug.LogWarning("[Mixed Atmospheres] Other-mod compatibility: Rephysicalized is loaded but its oxygen intake is not the one this version knows (" +
                        missing + "); its own intake runs, which under the colony owner sees only each cell's most massive gas.");
                    return;
                }
                oxidizer = ReadTag(tagField!);
                new Harmony(HarmonyId).Patch(method, prefix: new HarmonyMethod(typeof(RephysicalizedCompat).GetMethod(nameof(Prefix), BindingFlags.NonPublic | BindingFlags.Static)));
                Installed = true;
                Debug.Log("[Mixed Atmospheres] Other-mod compatibility: Rephysicalized oxygen intake reads the owner's mixture (" + ConsumerTypeName +
                    ".ConsumeBreathableGases, Harmony owner " + HarmonyId + ").");
                InstallSuffocation();
            }
            catch (Exception error)
            {
                Problem = error.GetType().Name + ": " + error.Message;
                Debug.LogWarning("[Mixed Atmospheres] Other-mod compatibility: the Rephysicalized oxygen intake was not adapted: " + Problem);
            }
        }

        // Separate, so that a different critter search leaves the intake installed.
        private static void InstallSuffocation()
        {
            try
            {
                var query = FindType(QueryTypeName);
                var method = query?.GetMethod("HasOxidizer", BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic, null, new[] { typeof(int) }, null);
                if (method == null || method.ReturnType != typeof(bool))
                {
                    Debug.LogWarning("[Mixed Atmospheres] Other-mod compatibility: Rephysicalized's critter oxidizer search is not the one this version knows (" +
                        QueryTypeName + ".HasOxidizer(int) bool); it keeps reading each cell's most massive gas.");
                    return;
                }
                new Harmony(HarmonyId).Patch(method, postfix: new HarmonyMethod(typeof(RephysicalizedCompat).GetMethod(nameof(HasOxidizerPostfix), BindingFlags.NonPublic | BindingFlags.Static)));
                SuffocationInstalled = true;
                Debug.Log("[Mixed Atmospheres] Other-mod compatibility: Rephysicalized's suffocating critters find oxygen in the owner's mixture (" + QueryTypeName + ".HasOxidizer).");
            }
            catch (Exception error)
            {
                Debug.LogWarning("[Mixed Atmospheres] Other-mod compatibility: Rephysicalized's critter oxidizer search was not adapted: " + error.GetType().Name + ": " + error.Message);
            }
        }

        // The original: valid cell, shown element a gas with the oxidizer tag, Grid.Mass > 0.
        // An owned cell also counts when any oxidizer gas of its composition has mass.
        private static void HasOxidizerPostfix(int __0, ref bool __result)
        {
            if (__result || !ColonyOwner.Active || !Grid.IsValidCell(__0) || !ColonyOwner.OwnsFluidCell(__0)) return;
            try
            {
                var count = ColonyOwner.Composition(__0, out var indices, out var masses);
                for (var k = 0; k < count; ++k)
                {
                    if (!(masses[k] > 0)) continue;
                    var e = ElementLoader.elements[(int)indices[k]];
                    if (e.IsGas && e.HasTag(oxidizer)) { __result = true; return; }
                }
            }
            catch (Exception error)
            {
                if (!failureLogged) { failureLogged = true; Debug.LogWarning("[Mixed Atmospheres] Other-mod compatibility: the critter oxidizer search failed once: " + error); }
            }
        }

        internal static Type? FindType(string name)
        {
            foreach (var assembly in AppDomain.CurrentDomain.GetAssemblies())
            {
                Type? found = null;
                try { found = assembly.GetType(name, false); } catch (Exception) { }
                if (found != null) return found;
            }
            return null;
        }

        // What of the known form is missing, or "" (method and every field
        // the prefix uses, with their types; the oxidizer tag). No game type
        // is named here, so the game contract records none for these lookups.
        private static string Missing(Type consumer, Type? tags, out MethodInfo? method, out FieldInfo? tagField)
        {
            var problems = new List<string>();
            const BindingFlags instance = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic;
            method = consumer.GetMethod("ConsumeBreathableGases", instance, null, new[] { typeof(float) }, null);
            if (method == null || method.ReturnType != typeof(void)) problems.Add("method ConsumeBreathableGases(float)");
            FieldInfo? Need(string name, string typeName)
            {
                var field = consumer.GetField(name, instance);
                if (field == null || field.FieldType.FullName != typeName) { problems.Add("field " + name + " (" + typeName + ")"); return null; }
                return field;
            }
            Need("consumptionRate", "System.Single"); Need("capacityKG", "System.Single"); Need("consumptionRadius", "System.Byte");
            Need("sampleCellOffset", "UnityEngine.Vector3"); Need("storeOnConsume", "System.Boolean"); isRequired = Need("isRequired", "System.Boolean");
            Need("storage", "Storage");
            tagField = tags?.GetField("OxidizerGas", BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic);
            if (tagField == null || tagField.FieldType.FullName != "Tag") problems.Add(TagsTypeName + ".OxidizerGas (Tag)");
            return string.Join(", ", problems);
        }
        private static Tag ReadTag(FieldInfo field) => (Tag)field.GetValue(null);

        // For the scenario (ColonyScenarios.Rephysicalized.cs): the
        // component's type, its isRequired switch and a property of another
        // Rephysicalized component, read by name.
        internal static Type? ConsumerType { get; private set; }
        private static FieldInfo? isRequired;
        internal static bool SetRequired(object consumer, bool value)
        {
            if (isRequired == null || !isRequired.DeclaringType!.IsInstanceOfType(consumer)) return false;
            isRequired.SetValue(consumer, value); return true;
        }
        internal static object? Property(object target, string name) =>
            target.GetType().GetProperty(name, BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic)?.GetValue(target);

        // ---- the intake

        private sealed class Request
        {
            internal KMonoBehaviour? Consumer; internal Storage? Storage; internal IntakeLedger? Ledger;
            internal int Cell; internal float Amount;
        }
        private static readonly Stack<Request> pool = new Stack<Request>();
        private static readonly ConditionalWeakTable<KMonoBehaviour, IntakeLedger> ledgers = new ConditionalWeakTable<KMonoBehaviour, IntakeLedger>();
        private static readonly Action<Sim.MassConsumedCallback, object> consumed = Consumed;
        private static readonly List<int> cells = new List<int>();
        private static readonly bool[] taken = new bool[128];
        private static bool failureLogged;
        // Totals since the game started, for the scenario and Player.log.
        internal static double RequestedKg, CreditedKg, ReturnedKg;

        // The ledger of one consumer (the scenario reads what it was credited).
        internal static IntakeLedger? LedgerOf(Component? consumer) =>
            consumer is KMonoBehaviour behaviour && ledgers.TryGetValue(behaviour, out var ledger) ? ledger : null;

        private static bool Prefix(KMonoBehaviour __instance, float __0, float ___consumptionRate, float ___capacityKG, byte ___consumptionRadius,
            Vector3 ___sampleCellOffset, bool ___storeOnConsume, Storage ___storage)
        {
            if (!ColonyOwner.Active) return true;
            var sent = false;
            try
            {
                Consume(__instance, __0, ___consumptionRate, ___capacityKG, ___consumptionRadius, ___sampleCellOffset, ___storeOnConsume, ___storage, ref sent);
            }
            catch (Exception error)
            {
                if (!failureLogged) { failureLogged = true; Debug.LogWarning("[Mixed Atmospheres] Other-mod compatibility: the Rephysicalized oxygen intake failed once (" + error + "); " +
                    (sent ? "the rest of that tick was skipped." : "its own intake ran for that tick.")); }
                return !sent;
            }
            return false;
        }

        private static void Consume(KMonoBehaviour consumer, float dt, float rate, float capacity, byte radius, Vector3 offset, bool store, Storage storage, ref bool sent)
        {
            if (dt <= 0f || rate <= 0f) return;
            var keep = store && storage != null;
            var ledger = ledgers.GetValue(consumer, _ => new IntakeLedger());
            var now = GameClock.Instance != null ? (double)GameClock.Instance.GetTime() : 0;
            if (ledger.Expire(now, ExpireSeconds))
                Debug.LogWarning("[Mixed Atmospheres] Other-mod compatibility: an oxygen intake's requests got no answer for " + ExpireSeconds + " s; its room is freed.");
            var center = Grid.PosToCell(consumer.transform.GetPosition() + offset);
            RadialCells(center, radius, cells);
            var stored = storage != null ? storage.GetMassAvailable(oxidizer) : 0f;
            var room = IntakeLedger.Room(capacity, stored, keep ? ledger.InFlight : 0f);
            var remaining = rate * dt;
            for (var c = 0; c < cells.Count; ++c)
            {
                if (remaining <= 0f || room <= 0f) break;
                var cell = cells[c];
                if (!ColonyOwner.OwnsFluidCell(cell))
                {
                    // The original logic, for a cell the owner does not hold.
                    var element = Grid.Element[cell];
                    if (element == null || !element.HasTag(oxidizer) || !(Grid.Mass[cell] > 0f)) continue;
                    var amount = Mathf.Min(remaining, Mathf.Min(Grid.Mass[cell], room));
                    if (!(amount > 0f)) continue;
                    sent = true;
                    SimMessages.ConsumeMass(cell, element.id, amount, radius, -1);
                    if (keep) storage!.AddGasChunk(element.id, amount, Grid.Temperature[cell], byte.MaxValue, 0, true, true);
                    remaining -= amount; room -= amount;
                    continue;
                }
                var count = ColonyOwner.Composition(cell, out var indices, out var masses);
                for (var k = 0; k < count; ++k) taken[k] = false;
                while (remaining > 0f && room > 0f)
                {
                    // The most massive oxidizer constituent not yet asked for.
                    var best = -1;
                    for (var k = 0; k < count; ++k)
                    {
                        if (taken[k] || !(masses[k] > 0) || (best >= 0 && masses[k] <= masses[best])) continue;
                        var e = ElementLoader.elements[(int)indices[k]];
                        if (e.IsGas && e.HasTag(oxidizer)) best = k;
                    }
                    if (best < 0) break;
                    taken[best] = true;
                    var amount = IntakeLedger.Take(masses[best], remaining, room);
                    if (amount <= 0f) continue;
                    var id = ElementLoader.elements[(int)indices[best]].id;
                    sent = true;
                    if (keep)
                    {
                        var request = pool.Count != 0 ? pool.Pop() : new Request();
                        request.Consumer = consumer; request.Storage = storage; request.Ledger = ledger; request.Cell = cell; request.Amount = amount;
                        ledger.Request(amount, now); RequestedKg += amount;
                        var handle = Game.Instance.massConsumedCallbackManager.Add(consumed, request, "MixedAtmospheres.Rephysicalized");
                        SimMessages.ConsumeMass(cell, id, amount, 0, handle.index);
                    }
                    else SimMessages.ConsumeMass(cell, id, amount, 0, -1); // the original stores nothing either
                    remaining -= amount; room -= amount;
                }
            }
        }

        // The owner's receipt: the storage gets what was taken. A consumer or
        // storage gone by then gives the gas back to the world at its cell.
        private static void Consumed(Sim.MassConsumedCallback info, object data)
        {
            if (!(data is Request request)) return;
            try
            {
                var credit = request.Consumer != null && request.Storage != null;
                request.Ledger?.Settle(request.Amount, info.mass, credit);
                if (info.mass > 0f)
                {
                    var id = ElementLoader.elements[info.elemIdx].id;
                    if (credit)
                    {
                        request.Storage!.AddGasChunk(id, info.mass, info.temperature, info.diseaseIdx, info.diseaseCount, true, true);
                        CreditedKg += info.mass;
                    }
                    else
                    {
                        SimMessages.AddRemoveSubstance(request.Cell, id, CellEventLogger.Instance.ElementConsumerSimUpdate, info.mass, info.temperature, info.diseaseIdx, info.diseaseCount);
                        ReturnedKg += info.mass;
                    }
                }
            }
            catch (Exception error)
            {
                if (!failureLogged) { failureLogged = true; Debug.LogWarning("[Mixed Atmospheres] Other-mod compatibility: an oxygen intake's receipt failed once: " + error); }
            }
            finally
            {
                request.Consumer = null; request.Storage = null; request.Ledger = null;
                pool.Push(request);
            }
        }

        // Rephysicalized.GridUtil.GetRadialCells: the square of cells within
        // the radius, column by column, the valid ones only.
        private static void RadialCells(int center, int radius, List<int> result)
        {
            result.Clear();
            Grid.CellToXY(center, out var cx, out var cy);
            for (var i = -radius; i <= radius; ++i)
                for (var j = -radius; j <= radius; ++j)
                {
                    var cell = Grid.XYToCell(cx + i, cy + j);
                    if (Grid.IsValidCell(cell)) result.Add(cell);
                }
        }
    }

    // Every mod's assemblies are loaded by the time the game builds its database.
    [HarmonyPatch(typeof(Db), nameof(Db.Initialize))]
    internal static class OtherModCompatInstall
    {
        private static void Postfix() => RephysicalizedCompat.TryInstall();
    }
}
