#!/usr/bin/env python3
"""Explicit content selections for private ONI compatibility runs.

These are the game's ordinary persisted menu preferences. Steam and ONI still
check content ownership. Requested preferences alone never prove what loaded.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

# Bit order is fixed for matrix identities; the base game is mask zero.
PACKS = (
    ("Spaced Out!", "EXPANSION1_ID"),
    ("Frosty", "DLC2_ID"),
    ("Bionic", "DLC3_ID"),
    ("Prehistoric", "DLC4_ID"),
    ("Aquatic", "DLC5_ID"),
)
DLC_IDS = tuple(identity for _, identity in PACKS)
ALL_MASK = (1 << len(PACKS)) - 1


def ids_for_mask(mask: int) -> list[str]:
    if type(mask) is not int or not 0 <= mask <= ALL_MASK:
        raise ValueError("DLC mask must be an integer from 0 to 31")
    return [identity for bit, identity in enumerate(DLC_IDS) if mask & (1 << bit)]


def mask_for_ids(identities: list[str]) -> int:
    if not isinstance(identities, list) or any(not isinstance(item, str) for item in identities):
        raise ValueError("DLC evidence must contain a list of content IDs")
    if len(set(identities)) != len(identities) or any(item not in (*DLC_IDS, "") for item in identities):
        raise ValueError("Unknown or repeated content ID in DLC evidence")
    return sum(1 << DLC_IDS.index(item) for item in identities if item)


def selection(mask: int) -> dict:
    enabled = ids_for_mask(mask)
    return {"mask": mask, "enabled_ids": enabled,
            "disabled_ids": [item for item in DLC_IDS if item not in enabled],
            "packs": [name for name, identity in PACKS if identity in enabled]}


def preferences(mask: int, original: str | None = None) -> str:
    """Set all five flags, preserving unrelated settings in an existing profile."""
    enabled = ids_for_mask(mask)
    flags = [f"  {identity}.ENABLED: {int(identity in enabled)}\n" for identity in DLC_IDS]
    if original is None:
        return "strings: {}\nints:\n" + "".join(flags) + "floats: {}\n"
    lines = original.splitlines(keepends=True)
    starts = [i for i, line in enumerate(lines) if line.rstrip("\r\n") in ("ints:", "ints: {}")]
    if len(starts) != 1:
        raise ValueError("Expected one block-style ints section in KPlayerPrefs")
    start = starts[0]
    end = start + 1
    while end < len(lines) and (not lines[end].strip() or lines[end][0].isspace() or lines[end].startswith("#")):
        end += 1
    keys = {identity + ".ENABLED" for identity in DLC_IDS}
    retained = []
    for line in lines[start + 1:end]:
        key = line.strip().partition(":")[0]
        if len(key) >= 2 and key[0] == key[-1] and key[0] in ("'", '"'):
            key = key[1:-1]
        if key not in keys:
            retained.append(line if line.endswith(("\r", "\n")) else line + "\n")
    return "".join([*lines[:start], "ints:\n", *retained, *flags, *lines[end:]])


def write_preferences(profile: Path, mask: int) -> None:
    profile.mkdir(parents=True, exist_ok=True)
    # Callers use disposable profiles. Refuse to replace an unrelated profile.
    with (profile / "kplayerprefs.yaml").open("x", encoding="utf-8", newline="\n") as stream:
        stream.write(preferences(mask))


def verify_report(mask: int, report: dict | None, *, saved: bool = False) -> None:
    """Require observations of the runtime and, optionally, saved content IDs."""
    ids_for_mask(mask)
    if not isinstance(report, dict) or type(report.get("RequestedDlcMask")) is not int or report.get("RequestedDlcMask") != mask or \
            report.get("DlcSelectionChecked") is not True or report.get("DlcSelectionMatched") is not True:
        raise ValueError("The game did not verify the requested DLC selection")
    if mask_for_ids(report.get("RuntimeDlcIds")) != mask:
        raise ValueError("Loaded runtime content differs from the requested DLC selection")
    if saved and mask_for_ids(report.get("ActiveDlcIds")) != mask:
        raise ValueError("Saved colony content differs from the requested DLC selection")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mask", type=int, choices=range(32), required=True)
    parser.add_argument("--source", type=Path, help="Existing private KPlayerPrefs to preserve")
    parser.add_argument("--output", type=Path, help="New preferences file; never replaces its source")
    args = parser.parse_args()
    if args.source and not args.output:
        parser.error("--source requires a new --output file")
    try:
        if args.output:
            text = preferences(args.mask, args.source.read_text(encoding="utf-8-sig") if args.source else None)
            with args.output.open("x", encoding="utf-8", newline="\n") as stream:
                stream.write(text)
        else:
            print(json.dumps(selection(args.mask), indent=2))
    except (OSError, ValueError) as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
