using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Reflection.Emit;
using System.Runtime.CompilerServices;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // Native identities and presentation assets only. These nominal pure-phase
    // definitions do not supply the mixture EOS, latent heat or world ownership.
    internal static class OniNitrogenElements
    {
        internal const string GasId = "MixedAtmospheresNitrogen";
        internal const string LiquidId = "MixedAtmospheresLiquidNitrogen";
        internal const string SolidId = "MixedAtmospheresSolidNitrogen";
        internal const string ModelVersion = "ma-native-nitrogen-reference-v1";
        // ONI loads the mod's elements/ folder as layerable files even when
        // this DLL fails or its patches are removed, and ElementLoader would
        // then manifest an empty Substance (no material/animation: the ground
        // renderer errors on the solid phase), untranslated names and integer
        // tags. The YAML therefore declares this private content key instead of
        // the base-game "". ElementLoader.Load skips an entry whose key has no
        // substance table, so the phases exist only while NitrogenContentGate
        // supplies the key; afterwards each element carries VANILLA_ID again.
        internal const string ContentGateKey = "MixedAtmospheres.AdapterLoaded";
        internal static readonly SimHashes Gas = (SimHashes)Hash.SDBMLower(GasId);
        internal static readonly SimHashes Liquid = (SimHashes)Hash.SDBMLower(LiquidId);
        internal static readonly SimHashes Solid = (SimHashes)Hash.SDBMLower(SolidId);

        private sealed class Definition
        {
            internal readonly string Id, Name, Description;
            internal readonly SimHashes Hash, Template;
            internal readonly Color32 Color;
            internal Definition(string id, string name, string description, SimHashes template, Color32 color)
            { Id = id; Name = name; Description = description; Hash = (SimHashes)global::Hash.SDBMLower(id); Template = template; Color = color; }
        }
        private static readonly Definition[] Definitions =
        {
            new Definition(GasId, "Nitrogen", "An inert atmospheric diluent. Nitrogen contributes gas pressure but supplies no breathable oxygen. Inspect the mixture's oxygen partial pressure and contaminant warnings.", SimHashes.Oxygen, new Color32(120, 145, 150, 255)),
            new Definition(LiquidId, "Liquid nitrogen", "A cryogenic liquid that evaporates into nitrogen gas when warmed. It supplies no breathable oxygen.", SimHashes.LiquidOxygen, new Color32(89, 130, 161, 255)),
            new Definition(SolidId, "Solid nitrogen", "Frozen nitrogen. Warming melts it into liquid nitrogen.", SimHashes.SolidOxygen, new Color32(163, 199, 219, 255))
        };
        private sealed class TemplateWitness
        {
            internal Substance Source = null!;
            internal Substance Clone = null!;
            internal Color32 Color, Ui, Conduit;
            internal Gradient Gradient = null!;
            internal GradientColorKey[] Colors = Array.Empty<GradientColorKey>();
            internal GradientAlphaKey[] Alphas = Array.Empty<GradientAlphaKey>();
        }
        private static readonly Dictionary<SimHashes, TemplateWitness> Templates = new Dictionary<SimHashes, TemplateWitness>();

        internal static string Identity(SimHashes id) => id == Gas ? GasId : id == Liquid ? LiquidId : id == Solid ? SolidId : id.ToString();
        internal static bool TryIdentity(SimHashes id, out string identity)
        {
            if (id == Gas) identity = GasId;
            else if (id == Liquid) identity = LiquidId;
            else if (id == Solid) identity = SolidId;
            else { identity = ""; return false; }
            return true;
        }
        internal static void InitializeStrings()
        {
            foreach (var item in Definitions)
            {
                var prefix = "STRINGS.ELEMENTS." + item.Id.ToUpperInvariant();
                Strings.Add(prefix + ".NAME", item.Name);
                Strings.Add(prefix + ".DESC", item.Description);
            }
        }

        internal static void Prepare(Hashtable substances, Dictionary<string, SubstanceTable> tables)
        {
            if (substances == null || tables == null) throw new ArgumentNullException("Native substance database");
            if (!tables.TryGetValue(DlcManager.VANILLA_ID, out var table) || table == null)
                throw new InvalidOperationException("The base-game substance table is unavailable for nitrogen registration.");
            var list = table.GetList();
            var cloneMethod = AccessTools.Method(typeof(object), "MemberwiseClone") ?? throw new MissingMethodException("MemberwiseClone");
            var gradientField = AccessTools.Field(typeof(Substance), "gradient") ?? throw new MissingFieldException("Substance.gradient");
            var propertyBlockField = AccessTools.Field(typeof(Substance), "propertyBlock") ?? throw new MissingFieldException("Substance.propertyBlock");
            foreach (var item in Definitions)
            {
                // A duplicate is an explicit incompatibility, never an arbitrary
                // override dependent on mod ordering. Reusing our exact previous
                // table clone during database reload is supported.
                var existing = list.SingleOrDefault(value => value.elementID == item.Hash);
                if (existing != null)
                {
                    if (!Templates.TryGetValue(item.Hash, out var previous) || !ReferenceEquals(previous.Clone, existing))
                        throw new InvalidOperationException("Nitrogen element identity collision: " + item.Id);
                    if (substances.ContainsKey(item.Hash) && !ReferenceEquals(substances[item.Hash], existing))
                        throw new InvalidOperationException("Conflicting nitrogen substance identity: " + item.Id);
                    continue;
                }
                if (substances.ContainsKey(item.Hash)) throw new InvalidOperationException("Nitrogen substance identity collision: " + item.Id);
                var source = substances[item.Template] as Substance ?? table.GetSubstance(item.Template);
                if (source == null)
                    throw new InvalidOperationException("Native nitrogen rendering template unavailable: " + item.Template);
                var original = source.Gradient;
                var witness = new TemplateWitness { Source = source, Color = source.colour, Ui = source.uiColour, Conduit = source.conduitColour,
                    Gradient = original, Colors = original.colorKeys, Alphas = original.alphaKeys };
                var colors = original.colorKeys;
                for (var i = 0; i < colors.Length; ++i)
                    colors[i] = new GradientColorKey((Color)item.Color, colors[i].time);
                var gradient = new Gradient { mode = original.mode };
                gradient.SetKeys(colors, original.alphaKeys);
                var clone = (Substance)cloneMethod.Invoke(source, null)!;
                witness.Clone = clone;
                clone.elementID = item.Hash; clone.name = item.Name;
                clone.colour = item.Color; clone.uiColour = item.Color; clone.conduitColour = item.Color;
                gradientField.SetValue(clone, gradient);
                // Materials/animations are shared, read-only references to the
                // licensed installation. Mutable per-substance state is private.
                propertyBlockField.SetValue(clone, null);
                list.Add(clone); Templates[item.Hash] = witness;
            }
        }

        internal static void OpenContentGate(Dictionary<string, SubstanceTable> tables)
        {
            if (tables == null) throw new ArgumentNullException("Native substance tables");
            // Prepare (an earlier prefix) already required a live table; this
            // reference check keeps the gate engine-independent for host tests.
            if (!tables.TryGetValue(DlcManager.VANILLA_ID, out var table) || table is null)
                throw new InvalidOperationException("The base-game substance table is unavailable for nitrogen registration.");
            if (tables.TryGetValue(ContentGateKey, out var existing) && !ReferenceEquals(existing, table))
                throw new InvalidOperationException("Nitrogen content gate collision: " + ContentGateKey);
            // The phases resolve exactly the base-game table Prepare cloned into.
            tables[ContentGateKey] = table;
        }

        internal static void CloseContentGate(Dictionary<string, SubstanceTable> tables)
        {
            tables?.Remove(ContentGateKey);
            foreach (var item in Definitions)
            {
                var element = ElementLoader.FindElementByHash(item.Hash);
                if (element != null && element.dlcId == ContentGateKey) element.dlcId = DlcManager.VANILLA_ID;
            }
        }

        internal static void ValidateLoaded()
        {
            foreach (var item in Definitions)
            {
                var element = ElementLoader.FindElementByHash(item.Hash) ?? throw new InvalidDataException("Nitrogen YAML was not loaded: " + item.Id);
                if (element.tag.ToString() != item.Id || element.name != item.Name || element.dlcId != DlcManager.VANILLA_ID ||
                    element.idx >= ElementLoader.elements.Count ||
                    !ReferenceEquals(ElementLoader.elements[element.idx], element) || element.substance == null ||
                    !SameColor(element.substance.colour, item.Color))
                    throw new InvalidDataException("Invalid native nitrogen element: " + item.Id);
                if (!Templates.TryGetValue(item.Hash, out var witness) || ReferenceEquals(element.substance, witness.Source) ||
                    !ReferenceEquals(element.substance.material, witness.Source.material) || !ReferenceEquals(element.substance.anim, witness.Source.anim) ||
                    ReferenceEquals(element.substance.Gradient, witness.Gradient) || !SameColor(witness.Source.colour, witness.Color) ||
                    !SameColor(witness.Source.uiColour, witness.Ui) || !SameColor(witness.Source.conduitColour, witness.Conduit) ||
                    !witness.Colors.SequenceEqual(witness.Source.Gradient.colorKeys) || !witness.Alphas.SequenceEqual(witness.Source.Gradient.alphaKeys))
                    throw new InvalidDataException("Native nitrogen registration changed its rendering template: " + item.Id);
            }
            var gas = ElementLoader.FindElementByHash(Gas); var liquid = ElementLoader.FindElementByHash(Liquid); var solid = ElementLoader.FindElementByHash(Solid);
            if (!gas.IsGas || !liquid.IsLiquid || !solid.IsSolid || gas.lowTempTransition != liquid || liquid.highTempTransition != gas ||
                liquid.lowTempTransition != solid || solid.highTempTransition != liquid || gas.materialCategory == GameTags.Breathable ||
                gas.lowTemp != 77.355f || liquid.lowTemp != 63.15f || liquid.highTemp != 77.355f || solid.highTemp != 63.15f)
                throw new InvalidDataException("Native nitrogen phase links or breathing category are invalid.");
        }
        private static bool SameColor(Color32 left, Color32 right) => left.r == right.r && left.g == right.g && left.b == right.b && left.a == right.a;
    }

    [HarmonyPatch(typeof(ElementLoader), nameof(ElementLoader.Load))]
    internal static class NitrogenElementLoadPatch
    {
        private static void Prefix(Hashtable substanceList, Dictionary<string, SubstanceTable> substanceTablesByDlc)
            => OniNitrogenElements.Prepare(substanceList, substanceTablesByDlc);
        private static void Postfix() => OniNitrogenElements.ValidateLoaded();
    }

    // Opens the private YAML content key only around the native element load:
    // the last prefix and first postfix, so other mods' prefixes/postfixes see
    // neither the extra table key nor the private dlcId. Without this adapter
    // (failed OnLoad, patches removed) ONI skips the three YAML phases.
    [HarmonyPatch(typeof(ElementLoader), nameof(ElementLoader.Load))]
    internal static class NitrogenContentGate
    {
        [HarmonyPriority(Priority.Last)]
        private static void Prefix(Dictionary<string, SubstanceTable> substanceTablesByDlc)
            => OniNitrogenElements.OpenContentGate(substanceTablesByDlc);
        [HarmonyPriority(Priority.First)]
        private static void Postfix(Dictionary<string, SubstanceTable> substanceTablesByDlc)
            => OniNitrogenElements.CloseContentGate(substanceTablesByDlc);
    }

    // SimHashes is the shipped enum and cannot acquire new members. Native tag
    // helpers otherwise stringify custom values as integers, breaking prefab
    // lookup and SpawnResource. Replace only enum stringification, keeping
    // the game's Tag construction and struct-return path. Native identities
    // retain their original Enum.ToString() result.
    [HarmonyPatch]
    internal static class NitrogenElementTagPatch
    {
        internal static bool Verified { get; private set; }
        // The shipped Harmony assembly binds Label to mscorlib, while this
        // adapter targets netstandard. Keep that runtime list untyped, as in
        // the other native-IL adapters, so labels retain their actual identity.
        private static IList Labels(CodeInstruction code) => (IList)(typeof(CodeInstruction).GetField("labels")!.GetValue(code)
            ?? throw new InvalidDataException("Harmony instruction label list is unavailable."));
        private static IEnumerable<MethodBase> TargetMethods()
        {
            yield return AccessTools.Method(typeof(GameTagExtensions), nameof(GameTagExtensions.Create));
            yield return AccessTools.Method(typeof(GameTagExtensions), nameof(GameTagExtensions.CreateTag));
        }
        internal static IEnumerable<CodeInstruction> Transpiler(IEnumerable<CodeInstruction> instructions)
        {
            var code=instructions.ToList();var replacements=0;
            for(var i=0;i<code.Count;++i)
            {
                if(i+2<code.Count && (code[i].opcode==OpCodes.Ldarga || code[i].opcode==OpCodes.Ldarga_S) &&
                    code[i+1].opcode==OpCodes.Constrained && Equals(code[i+1].operand,typeof(SimHashes)) &&
                    code[i+2].opcode==OpCodes.Callvirt && Equals(code[i+2].operand,AccessTools.Method(typeof(object),nameof(ToString),Type.EmptyTypes)))
                {
                    if(Labels(code[i+1]).Count!=0 || Labels(code[i+2]).Count!=0 ||
                        code[i].blocks.Count!=0 || code[i+1].blocks.Count!=0 || code[i+2].blocks.Count!=0)
                        throw new InvalidDataException("Unexpected branch/exception boundary in the native enum tag helper.");
                    var load=new CodeInstruction(OpCodes.Ldarg_0);
                    foreach(var label in Labels(code[i]))Labels(load).Add(label);
                    yield return load;
                    yield return new CodeInstruction(OpCodes.Call,AccessTools.Method(typeof(OniNitrogenElements),nameof(OniNitrogenElements.Identity)));
                    ++replacements;i+=2;
                }
                else yield return code[i];
            }
            if(replacements!=1)throw new InvalidDataException("The native enum tag helper no longer has exactly one inspected stringification.");
        }
        [MethodImpl(MethodImplOptions.NoInlining)]
        internal static void WarmNativeCalls()
        {
            Verified=false;
            // The real WindowsPlayer/Proton control fails at a cold CreateTag
            // entry after composition. Pre-invocation makes its direct runtime
            // checks pass; standalone Mono does not reproduce the Unity fault.
            foreach(var name in new[]{"Create","CreateTag"})
            {
                var method=AccessTools.Method(typeof(GameTagExtensions),name,new[]{typeof(SimHashes)})
                    ?? throw new MissingMethodException("Native tag helper is unavailable: "+name);
                if(!(method.Invoke(null,new object[]{SimHashes.Oxygen}) is Tag tag) || tag!=TagManager.Create("Oxygen"))
                    throw new InvalidDataException("Native tag helper differs before patching: "+name);
            }
        }
        [MethodImpl(MethodImplOptions.NoInlining)]
        internal static void VerifyCalls()
        {
            Verified=false;
            foreach(var id in new[]{SimHashes.Oxygen,SimHashes.Water,SimHashes.Unobtanium,SimHashes.Vacuum,
                OniNitrogenElements.Gas,OniNitrogenElements.Liquid,OniNitrogenElements.Solid,(SimHashes)int.MaxValue})
            {
                var expected=TagManager.Create(OniNitrogenElements.Identity(id));
                if(GameTagExtensions.Create(id)!=expected || id.CreateTag()!=expected)
                    throw new InvalidDataException("Patched native tag helper changed its identity or return contract: "+(int)id);
            }
            Verified=true;
        }
    }
}
