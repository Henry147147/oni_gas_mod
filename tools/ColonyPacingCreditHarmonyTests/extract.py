"""Link the actual legacy observer and its Prepare guard into the flag tests."""
import argparse
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--root", type=Path, required=True)
parser.add_argument("--out", type=Path, required=True)
args = parser.parse_args()
source = (args.root.resolve() / "managed/MixedAtmospheres.Oni/ColonyPacing.cs").read_text()
begin = "    internal static class ColonyPacingLossLog\n"
end = "    public sealed class ColonyPacingDriver"
if source.count(begin) != 1 or source.count(end) != 1:
    raise ValueError("Legacy observer class boundaries changed; review the actual source extraction")
body = source[source.index(begin):source.index(end)]
if "private static bool Prepare() => ColonyPacingLossLog.Enabled;" not in body or not body.rstrip().endswith("}"):
    raise ValueError("Actual legacy observer/Prepare guard was not extracted")
args.out.mkdir(parents=True, exist_ok=True)
(args.out / "ActualLossObserver.cs").write_text("using System;using HarmonyLib;using UnityEngine;\nnamespace MixedAtmospheres.Oni {\n" + body + "\n}\n")
