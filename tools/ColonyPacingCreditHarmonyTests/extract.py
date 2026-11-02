"""Link the actual legacy observer, its Prepare guard and the production
busy-fence prefix (ColonyPacingTickPatch -> ColonyPacing.BeforeTick) into the
flag tests. Only the game's sub-tick fields, busy flag and timers are stubbed."""
import argparse
import importlib.util
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--root", type=Path, required=True)
parser.add_argument("--out", type=Path, required=True)
args = parser.parse_args()
root = args.root.resolve()
source = (root / "managed/MixedAtmospheres.Oni/ColonyPacing.cs").read_text()
begin = "    internal static class ColonyPacingLossLog\n"
end = "    public sealed class ColonyPacingDriver"
if source.count(begin) != 1 or source.count(end) != 1:
    raise ValueError("Legacy observer class boundaries changed; review the actual source extraction")
body = source[source.index(begin):source.index(end)]
if "private static bool Prepare() => ColonyPacingLossLog.Enabled;" not in body or not body.rstrip().endswith("}"):
    raise ValueError("Actual legacy observer/Prepare guard was not extracted")
args.out.mkdir(parents=True, exist_ok=True)
(args.out / "ActualLossObserver.cs").write_text("using System;using HarmonyLib;using UnityEngine;\nnamespace MixedAtmospheres.Oni {\n" + body + "\n}\n")

# The production busy fence and the Harmony prefix that installs it.
spec = importlib.util.spec_from_file_location("production_method_reader", root / "tools/ColonyReceiptPumpTests/extract.py")
reader = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reader)
entry = source.replace("internal static bool BeforeSimEveryTick(", "private static bool BeforeSimEveryTick(")
methods = [reader.method(entry, "BeforeSimEveryTick", "bool").replace("private static", "internal static", 1),
           reader.method(source, "BeforeSimEveryTickTimed", "bool"), reader.method(source, "BeforeTick", "bool")]
busy = "        internal static bool Busy => Enabled && ColonyOwner.Active && OniNativeColony.Busy() != 0;\n"
patch_begin = "    [HarmonyPatch(typeof(Game), \"SimEveryTick\")]\n    internal static class ColonyPacingTickPatch\n"
if source.count(busy) != 1 or source.count(patch_begin) != 1:
    raise ValueError("Production busy flag or tick patch changed; review the actual source extraction")
patch_end = source.index("\n    }\n", source.index(patch_begin)) + len("\n    }\n")
patch = source[source.index(patch_begin):patch_end]
if "ColonyPacing.BeforeSimEveryTick(__instance, ref __0)" not in patch:
    raise ValueError("Production tick patch no longer enters BeforeSimEveryTick")
(args.out / "ActualPacingFence.cs").write_text("using System;using HarmonyLib;using UnityEngine;\nnamespace MixedAtmospheres.Oni {\ninternal static partial class ColonyPacing {\n"
    + busy + "\n".join(methods) + "\n}\n" + patch + "}\n")
