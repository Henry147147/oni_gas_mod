"""Compile production culture completion and save admission against observed endpoints."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location("production_method_reader", args.root / "tools/ColonyReceiptPumpTests/extract.py")
    reader = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(reader)
    source = args.root / "managed/MixedAtmospheres.Oni"
    storage = (source / "ColonyOwnedStorage.cs").read_text()
    biology = (source / "ColonyOwnedStorage.Biology.cs").read_text()
    bodies = [reader.method(storage, name, "void") for name in ("RefreshCultures", "RefreshCulture")]
    guarded = storage.replace("internal static void Reject(PrimaryElement item,string operation)", "private static void Reject(PrimaryElement item,string operation)")
    bodies.append(reader.method(guarded, "Reject", "void").replace("private static", "internal static", 1))
    quiescent = storage.replace("internal static void RequireQuiescent()", "private static void RequireQuiescent()")
    bodies.append(reader.method(quiescent, "RequireQuiescent", "void"))
    finish = biology.replace("internal void Finish(bool success)", "private static void Finish(bool success)")
    finish = reader.method(finish, "Finish", "void").replace("private static void Finish", "internal void Finish", 1)
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / "Production.cs").write_text("using System;using System.Collections.Generic;using System.Linq;using UnityEngine;namespace MixedAtmospheres.Oni {internal static partial class ColonyOwnedStorage {\n" + "\n".join(bodies) + "\ninternal sealed partial class BiologyScope {\n" + finish + "\n}}}\n")
    heat = (source / "ColonyScenarios.StoredHeat.cs").read_text()
    due = reader.method(heat, "StoredHeatFinalDue", "bool")
    may_tick = reader.method(heat.replace("internal static bool StoredHeatMayTick(", "private static bool StoredHeatMayTick("), "StoredHeatMayTick", "bool")
    valid_start = heat.index("        private static bool StoredHeatWindowValid(")
    valid_end = heat.index(";", valid_start) + 1
    valid = heat[valid_start:valid_end]
    (args.out / "HeatClock.cs").write_text("using System;namespace MixedAtmospheres.Oni {internal static partial class ColonyScenarios {\n" + due + "\n" + valid + "\n" + may_tick + "\n}}\n")
    (args.out / "source-manifest.json").write_text(json.dumps({name: hashlib.sha256((source / name).read_bytes()).hexdigest() for name in ("ColonyOwnedStorage.cs", "ColonyOwnedStorage.Biology.cs", "ColonyScenarios.StoredHeat.cs")}, indent=2) + "\n")


if __name__ == "__main__":
    main()
