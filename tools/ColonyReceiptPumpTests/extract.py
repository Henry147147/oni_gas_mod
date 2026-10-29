"""Compile the real receipt methods against test endpoints and a deterministic clock.

No receipt, admission, acknowledgment or barrier algorithm is copied here. The
state and full save-barrier files are linked directly by the project. Method
boundaries are scanned while ignoring comments and string/character literals;
missing or ambiguous signatures fail the build explicitly.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re


def method(source: str, name: str) -> str:
    pattern = re.compile(r"^[ \t]*private static ulong " + re.escape(name) + r"\([^\n]*\)\s*\{", re.M)
    matches = list(pattern.finditer(source))
    if len(matches) != 1:
        raise ValueError(f"Expected one real {name} method, found {len(matches)}")
    match = matches[0]
    opening = source.index("{", match.start(), match.end())
    depth, index, state = 1, opening + 1, "code"
    while index < len(source):
        char = source[index]
        pair = source[index:index + 2]
        if state == "line":
            if char == "\n": state = "code"
        elif state == "block":
            if pair == "*/": state, index = "code", index + 1
        elif state == "verbatim":
            if pair == '""': index += 1
            elif char == '"': state = "code"
        elif state in ("string", "char"):
            if char == "\\": index += 1
            elif char == ('"' if state == "string" else "'"): state = "code"
        else:
            if pair == "//": state, index = "line", index + 1
            elif pair == "/*": state, index = "block", index + 1
            elif char == '"': state = "verbatim" if source[index - 1] == "@" else "string"
            elif char == "'": state = "char"
            elif char == "{": depth += 1
            elif char == "}":
                depth -= 1
                if depth == 0: return source[match.start():index + 1]
        index += 1
    raise ValueError(f"Unterminated real {name} method")


def clock(source: str) -> str:
    return source.replace("System.Diagnostics.Stopwatch.GetTimestamp()", "Clock.Timestamp").replace("System.Diagnostics.Stopwatch.Frequency", "Clock.Frequency")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    source_dir = args.root.resolve() / "managed/MixedAtmospheres.Oni"
    owner = (source_dir / "ColonyOwner.cs").read_text()
    delivery = method(owner, "DeliverReceipts").replace("private static ulong", "internal static ulong", 1)
    delivery += "\n" + method(owner, "DeliverReceiptsOrThrow")
    if "delivered <= ReceiptBacklogLimit && stepSeconds > 0" not in delivery:
        raise ValueError("Original publication admission policy changed; review its tests explicitly")
    preamble = """using System;using System.Collections.Generic;using UnityEngine;
namespace MixedAtmospheres.Oni {internal static unsafe partial class ColonyOwner {
internal static bool Active=true,publicationLog;internal static long ReceiptBudgetTicks=2000;private const int ReceiptBacklogLimit=1500;
internal static OniNativeColony.Receipt[] receipts=new OniNativeColony.Receipt[4096];internal static OniNativeColony.Status status;internal static long DeferredReceiptPublications;
internal static void InvalidateLifecycleReadings(){}
internal static void SpawnByproduct(in OniNativeColony.Receipt r){TestRuntime.Dispatch(r);}
private static bool InRect(int cell,out int index){index=cell;return Grid.IsValidCell(cell);}
internal static void CommitTestPublication(ulong frame,int unityFrame){status=OniNativeColony.Current;status.CompletedFrames=frame;var epoch=receiptPump.Generation;var copy=new OwnedGameDataSnapshot();var observed=DeliverReceipts(copy);TestRuntime.LastSnapshot=copy;if(!TestRuntime.PublicationFails&&epoch==receiptPump.Generation&&!ColonyFault.Faulted)receiptPump.Published(status.Session,frame,observed,unityFrame);}
internal static ColonyReceiptPumpState TestState=>receiptPump;
"""
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / "OwnerDelivery.cs").write_text(preamble + clock(delivery) + "\n} }\n")
    (args.out / "PumpRuntime.cs").write_text(clock((source_dir / "ColonyReceiptPump.cs").read_text()))
    names = ("ColonyOwner.cs", "ColonyReceiptPump.cs", "ColonyReceiptPumpState.cs", "ColonySaveBarrier.cs")
    (args.out / "source-manifest.json").write_text(json.dumps({name: hashlib.sha256((source_dir / name).read_bytes()).hexdigest() for name in names}, indent=2) + "\n")


if __name__ == "__main__":
    main()
