using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Reflection.Emit;
using HarmonyLib;

namespace MixedAtmospheres.Oni
{
    // The game's readers of a tile's air (docs/colony-owner.md#the-shown-phase).
    //
    // Since 1.13.0 the grid shows a cell's liquid whenever it holds any, so a
    // film of water under a room's air is a Water tile whose mass is the film's.
    // ONI's own components read Grid.Element and Grid.Mass and would take that
    // tile for a tile without air: a plant would find the wrong atmosphere, a
    // gas pump no gas, a gas vent and an electrolyzer the film's few grams
    // instead of the room's pressure. The components listed here mean the air.
    // In a cell that holds gas and less than half a cell of liquid (the cells
    // that showed their gas until 1.13.0) they read its gas, as the owner's
    // breathing read does: the gas with the most mass, and the mass of all its
    // gas. Every other cell, and every other reader, sees what the grid shows.
    internal static class ColonyAtmosphereReaders
    {
        // The share of a cell's volume from which its liquid is the cell to
        // these readers too (oni_colony.cpp tile_freezing_fraction).
        internal const float LiquidCell = .5f;

        // The readers, by type: every method of the type and of its nested
        // (compiler-generated) types that reads the grid's element or mass.
        private static readonly (Type Type, string? Method)[] Readers =
        {
            (typeof(PressureVulnerable), null), (typeof(TemperatureVulnerable), null),            // plants
            (typeof(Vent), "IsValidOutputCell"), (typeof(Electrolyzer), null), (typeof(RustDeoxidizer), null),
            (typeof(OilRefinery), null), (typeof(GameUtil), nameof(GameUtil.IsEmissionBlocked)), // over-pressure
            (typeof(SpaceHeater), "MonitorHeating"), (typeof(LiquidCooledFan), null), (typeof(IceCooledFan), null),
            (typeof(SteamTurbine), null), (typeof(MaskStation), null),
            (typeof(StructureTemperatureComponents), nameof(StructureTemperatureComponents.ExhaustHeat)),
            (typeof(Rottable), null),                                                            // the air food keeps in
            (typeof(PressureMonitor), null), (typeof(ExternalTemperatureMonitor), null), (typeof(GasLiquidExposureMonitor), null), // duplicants
            (typeof(ElementDropperMonitor), null), (typeof(DiseaseDropper), null), (typeof(FartChore), null),
            (typeof(GasAndLiquidConsumerMonitor), null),                                         // critters
        };

        private static readonly FieldInfo Elements = AccessTools.Field(typeof(Grid), nameof(Grid.Element));
        private static readonly MethodInfo MassOf = AccessTools.DeclaredIndexerGetter(typeof(Grid.MassIndexer), new[] { typeof(int) });
        private static readonly MethodInfo IndexOf = AccessTools.DeclaredIndexerGetter(typeof(Grid.ElementIdxIndexer), new[] { typeof(int) });
        private static readonly MethodInfo GridIsGas = AccessTools.Method(typeof(Grid), nameof(Grid.IsGas), new[] { typeof(int) });
        private static readonly MethodInfo GridIsLiquid = AccessTools.Method(typeof(Grid), nameof(Grid.IsLiquid), new[] { typeof(int) });
        private static readonly MethodInfo AirElement = AccessTools.Method(typeof(ColonyAtmosphereReaders), nameof(Element));
        private static readonly MethodInfo AirMass = AccessTools.Method(typeof(ColonyAtmosphereReaders), nameof(Mass));
        private static readonly MethodInfo AirIndex = AccessTools.Method(typeof(ColonyAtmosphereReaders), nameof(ElementIdx));
        private static readonly MethodInfo AirIsGas = AccessTools.Method(typeof(ColonyAtmosphereReaders), nameof(IsGas));
        private static readonly MethodInfo AirIsLiquid = AccessTools.Method(typeof(ColonyAtmosphereReaders), nameof(IsLiquid));

        // ------------------------------------------------------------ reads

        private static int cachedCell = -1, cachedFrame = -1; private static Element? cachedGas; private static float cachedMass;
        private static long filmReads;
        internal static long FilmReads => System.Threading.Interlocked.Read(ref filmReads);

        // Whether the cell shows a liquid over gas that these readers mean:
        // then its leading gas and the mass of all its gas.
        internal static bool Film(int cell, out Element gas, out float mass)
        {
            gas = null!; mass = 0;
            try
            {
                if (!ColonyOwner.TryReadView(cell, out var view)) return false;
                const uint both = OniNativeColony.ViewGas | OniNativeColony.ViewLiquid;
                if ((view.Flags & both) != both || !(view.LiquidFraction < LiquidCell) || !ElementLoader.elements[(int)view.ElementIndex].IsLiquid) return false;
                var frame = UnityEngine.Time.frameCount;
                if (cell != cachedCell || frame != cachedFrame || cachedGas == null)
                {
                    var count = ColonyOwner.Composition(cell, out var indices, out var masses);
                    Element? best = null; double most = 0, total = 0;
                    for (var k = 0; k < count; ++k)
                    {
                        var e = ElementLoader.elements[(int)indices[k]]; if (!e.IsGas) continue;
                        total += masses[k]; if (masses[k] > most) { most = masses[k]; best = e; }
                    }
                    cachedGas = best; cachedMass = (float)total; cachedCell = best != null ? cell : -1; cachedFrame = frame;
                    if (best == null) return false;
                }
                gas = cachedGas!; mass = cachedMass; System.Threading.Interlocked.Increment(ref filmReads);
                return true;
            }
            catch (Exception error) { ColonyFault.Raise("atmosphere reader", error); return false; }
        }

        // Grid.Element[cell] of a reader of the air.
        public static Element Element(Element[] shown, int cell)
        {
            var element = shown[cell];
            return element.IsLiquid && Film(cell, out var gas, out _) ? gas : element;
        }
        // Grid.Mass[cell] of a reader of the air.
        public static float Mass(ref Grid.MassIndexer shown, int cell) =>
            Grid.Element[cell].IsLiquid && Film(cell, out _, out var mass) ? mass : shown[cell];
        // Grid.ElementIdx[cell] of a reader of the air.
        public static ushort ElementIdx(ref Grid.ElementIdxIndexer shown, int cell) =>
            Grid.Element[cell].IsLiquid && Film(cell, out var gas, out _) ? gas.idx : shown[cell];
        public static bool IsGas(int cell) => Grid.IsGas(cell) || Grid.Element[cell].IsLiquid && Film(cell, out _, out _);
        public static bool IsLiquid(int cell) => Grid.IsLiquid(cell) && !Film(cell, out _, out _);

        // --------------------------------------------------------- patching

        private static bool Reads(MethodBase method, GameContractCanon.Instruction instruction)
        {
            if (instruction.Code.OperandType != OperandType.InlineField && instruction.Code.OperandType != OperandType.InlineMethod) return false;
            MemberInfo? member;
            try
            {
                var owner = method.DeclaringType;
                member = method.Module.ResolveMember(instruction.Token, owner != null && owner.IsGenericType ? owner.GetGenericArguments() : null,
                    method is MethodInfo && method.IsGenericMethod ? method.GetGenericArguments() : null);
            }
            catch (ArgumentException) { return false; }
            return member != null && (Same(member, Elements) && instruction.Code == OpCodes.Ldsfld || Same(member, MassOf) || Same(member, IndexOf) || Same(member, GridIsGas) || Same(member, GridIsLiquid));
        }
        private static bool Same(MemberInfo a, MemberInfo b) => a.Module == b.Module && a.MetadataToken == b.MetadataToken;

        private static IEnumerable<Type> WithNested(Type type)
        {
            yield return type;
            foreach (var nested in type.GetNestedTypes(BindingFlags.Public | BindingFlags.NonPublic)) foreach (var inner in WithNested(nested)) yield return inner;
        }

        // The methods the rewrite applies to, found from the game's own code
        // so compiler-generated names need not be known. Also the contract's
        // list (GameContractScope.ManualTargets): each is verified before it
        // is patched.
        internal static IEnumerable<MethodBase> ContractTargets()
        {
            foreach (var (type, name) in Readers)
            {
                var found = 0;
                foreach (var holder in name == null ? WithNested(type) : new[] { type })
                    foreach (var method in holder.GetMethods(GameContractCanon.Declared).Cast<MethodBase>().Concat(holder.GetConstructors(GameContractCanon.Declared)))
                    {
                        if (name != null && method.Name != name || method.IsGenericMethodDefinition || holder.IsGenericTypeDefinition) continue;
                        var il = method.GetMethodBody()?.GetILAsByteArray();
                        if (il == null || !GameContractCanon.Decode(il).Any(i => Reads(method, i))) continue;
                        ++found; yield return method;
                    }
                if (found == 0) throw new NotSupportedException("The atmosphere reader " + type.Name + (name != null ? "." + name : "") + " no longer reads the grid.");
            }
            yield return PumpTarget();
        }
        private static MethodBase PumpTarget() => AccessTools.Method(typeof(Pump), "IsPumpable", new[] { typeof(Element.State), typeof(int) })
            ?? throw new NotSupportedException("Pump.IsPumpable is missing.");

        internal static int Rewritten { get; private set; }

        internal static void Install(Harmony harmony)
        {
            var rewrite = new HarmonyMethod(typeof(ColonyAtmosphereReaders), nameof(Rewrite));
            var pump = PumpTarget(); var methods = 0;
            foreach (var target in ContractTargets())
            {
                if (target == pump) continue;
                harmony.Patch(target, transpiler: rewrite); ++methods;
            }
            harmony.Patch(pump, postfix: new HarmonyMethod(typeof(ColonyAtmosphereReaders), nameof(GasUnderFilm)));
            UnityEngine.Debug.Log($"[Mixed Atmospheres] Readers of the air: {Rewritten} grid reads in {methods} game methods read a cell's gas under a shown liquid.");
        }

        // A gas pump finds the gas of a cell that shows a film.
        private static void GasUnderFilm(Pump __instance, Element.State expected_state, ref bool __result)
        {
            if (__result || expected_state != global::Element.State.Gas || !ColonyOwner.Active) return;
            try
            {
                var consumer = __instance.GetComponent<ElementConsumer>(); if (consumer == null) return;
                var origin = Grid.PosToCell(__instance.transform.GetPosition());
                for (var i = 0; i < consumer.consumptionRadius; ++i) for (var j = 0; j < consumer.consumptionRadius; ++j)
                {
                    var cell = origin + j + Grid.WidthInCells * i;
                    if (Grid.IsValidCell(cell) && Film(cell, out _, out _)) { __result = true; return; }
                }
            }
            catch (Exception error) { ColonyFault.Raise("gas pump reader", error); }
        }

        private static (int Pop, int Push)? Effect(CodeInstruction instruction)
        {
            var code = instruction.opcode;
            if (code.FlowControl == FlowControl.Branch || code.FlowControl == FlowControl.Cond_Branch || code.FlowControl == FlowControl.Return ||
                code.FlowControl == FlowControl.Throw || code == OpCodes.Calli) return null;
            if (instruction.operand is MethodBase called && (code == OpCodes.Call || code == OpCodes.Callvirt || code == OpCodes.Newobj))
                return (called.GetParameters().Length + (called.IsStatic || code == OpCodes.Newobj ? 0 : 1),
                    code == OpCodes.Newobj || called is MethodInfo info && info.ReturnType != typeof(void) ? 1 : 0);
            int pop, push;
            switch (code.StackBehaviourPop)
            {
                case StackBehaviour.Pop0: pop = 0; break;
                case StackBehaviour.Pop1: case StackBehaviour.Popi: case StackBehaviour.Popref: pop = 1; break;
                case StackBehaviour.Pop1_pop1: case StackBehaviour.Popi_pop1: case StackBehaviour.Popi_popi: case StackBehaviour.Popi_popi8:
                case StackBehaviour.Popi_popr4: case StackBehaviour.Popi_popr8: case StackBehaviour.Popref_pop1: case StackBehaviour.Popref_popi: pop = 2; break;
                case StackBehaviour.Popi_popi_popi: case StackBehaviour.Popref_popi_popi: case StackBehaviour.Popref_popi_popi8: case StackBehaviour.Popref_popi_popr4:
                case StackBehaviour.Popref_popi_popr8: case StackBehaviour.Popref_popi_popref: case StackBehaviour.Popref_popi_pop1: pop = 3; break;
                default: return null;
            }
            switch (code.StackBehaviourPush)
            {
                case StackBehaviour.Push0: push = 0; break;
                case StackBehaviour.Push1: case StackBehaviour.Pushi: case StackBehaviour.Pushi8: case StackBehaviour.Pushr4: case StackBehaviour.Pushr8: case StackBehaviour.Pushref: push = 1; break;
                case StackBehaviour.Push1_push1: push = 2; break;
                default: return null;
            }
            return (pop, push);
        }

        // "Grid.Element[cell]", "Grid.Mass[cell]", "Grid.ElementIdx[cell]",
        // "Grid.IsGas(cell)" and "Grid.IsLiquid(cell)" become the reads above.
        // The array read is the ldelem.ref that takes the loaded array: found
        // by following the evaluation stack over the index expression.
        private static IEnumerable<CodeInstruction> Rewrite(IEnumerable<CodeInstruction> instructions, MethodBase original)
        {
            var list = new List<CodeInstruction>(instructions); var missed = 0;
            for (var i = 0; i < list.Count; ++i)
            {
                var c = list[i];
                if (c.opcode == OpCodes.Ldsfld && Equals(c.operand, Elements))
                {
                    var depth = 1; var taker = -1;
                    for (var k = i + 1; k < list.Count; ++k)
                    {
                        var effect = Effect(list[k]); if (effect == null) break;
                        if (depth - effect.Value.Pop < 1) { if (list[k].opcode == OpCodes.Ldelem_Ref && depth == 2) taker = k; break; }
                        depth += effect.Value.Push - effect.Value.Pop;
                    }
                    if (taker < 0) { ++missed; continue; }
                    list[taker].opcode = OpCodes.Call; list[taker].operand = AirElement; ++Rewritten;
                }
                else if (c.opcode != OpCodes.Call && c.opcode != OpCodes.Callvirt) continue;
                else if (Equals(c.operand, MassOf)) { c.opcode = OpCodes.Call; c.operand = AirMass; ++Rewritten; }
                else if (Equals(c.operand, IndexOf)) { c.opcode = OpCodes.Call; c.operand = AirIndex; ++Rewritten; }
                else if (Equals(c.operand, GridIsGas)) { c.operand = AirIsGas; ++Rewritten; }
                else if (Equals(c.operand, GridIsLiquid)) { c.operand = AirIsLiquid; ++Rewritten; }
            }
            if (missed != 0)
                UnityEngine.Debug.LogWarning($"[Mixed Atmospheres] {missed} element read(s) of {original.DeclaringType?.Name}.{original.Name} kept the shown element.");
            return list;
        }
    }
}
