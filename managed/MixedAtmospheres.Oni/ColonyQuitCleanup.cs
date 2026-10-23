using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection.Emit;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // Save roots can already have been destroyed by another root's cleanup.
    // Unity's managed reference remains non-null, but its native component no
    // longer permits gameObject access. Keep all live-root cleanup unchanged.
    [HarmonyPatch(typeof(Game), "StopBE")]
    internal static class ColonyQuitCleanup
    {
        internal static GameObject? ExistingGameObject(Component component)
            => component == null ? null : component.gameObject;

        internal static IEnumerable<CodeInstruction> Transpiler(IEnumerable<CodeInstruction> instructions)
        {
            var source=instructions.ToList();
            var getter=AccessTools.PropertyGetter(typeof(Component),nameof(Component.gameObject));
            var safe=AccessTools.Method(typeof(ColonyQuitCleanup),nameof(ExistingGameObject));
            if(source.Count(instruction=>instruction.Calls(getter))!=2)
                throw new NotSupportedException("Unsupported Game.StopBE save-root cleanup layout.");
            foreach(var instruction in source)
            {
                if(instruction.Calls(getter)) { instruction.opcode=OpCodes.Call;instruction.operand=safe; }
                yield return instruction;
            }
        }
    }
}
