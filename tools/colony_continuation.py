#!/usr/bin/env python3
"""Compare uninterrupted/restored observations of the same saved colony.

This checks a bounded owner/pipe/particle prefix. With both branches' owner
input journals (MA_COLONY_INPUT_JOURNAL_DIR) it also finds the first frame
whose committed inputs differ: a state may differ only from that frame on.
Other native/managed actor state remains a separate release requirement.
"""
import argparse
import json
import re
from pathlib import Path


def digest(value):
    return isinstance(value, str) and re.fullmatch(r"[0-9a-f]{64}", value) is not None


def observation(result, origin):
    mode="colony-owner" if origin=="uninterrupted" else "colony-owner-load"
    if not isinstance(result, dict) or result.get("mode")!=mode or result.get("status")!="passed-"+mode or type(result.get("exit_code")) is not int or result["exit_code"]!=0:
        raise ValueError("Continuation needs a completed successful game result")
    owner = result.get("colony_owner")
    if not isinstance(owner, dict) or type(owner.get("State")) is not int or owner["State"] != 1 or owner.get("Faulted") is not False:
        raise ValueError("Continuation needs a healthy active fluid owner")
    record = owner.get("Continuation")
    if not isinstance(record, dict) or record.get("Origin") != origin:
        raise ValueError("Continuation origin is missing or wrong")
    count = record.get("Frames")
    baseline = record.get("RestoredFrame")
    if type(count) is not int or not 1 <= count <= 100000 or type(baseline) is not int or baseline < 0:
        raise ValueError("Invalid continuation frame range")
    if record.get("Complete") is not True or type(record.get("Submitted")) is not int or record["Submitted"] != count or type(record.get("Frame")) is not int or record["Frame"] != baseline + count:
        raise ValueError("Continuation did not finish every consecutive frame")
    if record.get("RuntimeImages") is not True or record.get("NativeSchedule") is not True:
        raise ValueError("Owner-only hashes do not cover active pipe continuation")
    for key in ("SaveSha256", "BaselineOwnerSha256", "BaselinePipesSha256"):
        if not digest(record.get(key)):
            raise ValueError("Missing continuation source identity: " + key)
    for key in ("Hashes", "PipeHashes", "ParticleHashes"):
        values = record.get(key)
        if not isinstance(values, list) or len(values) != count or not all(digest(value) for value in values):
            raise ValueError("Incomplete continuation images: " + key)
    seconds = record.get("SecondsBits")
    if not isinstance(seconds, list) or len(seconds) != count or not all(type(v) is int and 0 < v < 0x7F800000 for v in seconds):
        raise ValueError("Invalid continuation timestep operands")
    pipes = owner.get("PipeSave")
    if not isinstance(pipes, dict) or pipes.get("Active") is not True or pipes.get("AuditClosed") is not True or pipes.get("NativeScheduleControl") is not True or pipes.get("NativeBindingsPending") is not False:
        raise ValueError("Continuation lacks a closed active native-schedule pipe runtime")
    key = "CapturedSha256" if origin == "uninterrupted" else "RestoredSha256"
    # A later source save can have a different hash; the baseline itself was
    # retained immediately after the actual control save. On load it must
    # name the section actually restored by the game.
    if origin == "restored":
        if pipes.get(key) != record["BaselinePipesSha256"]:
            raise ValueError("Continuation baseline differs from the restored pipe image")
        restored = owner.get("RestoredOwner")
        if not isinstance(restored, dict) or restored.get("Reencoded") is not True or restored.get("ImageSha256") != record["BaselineOwnerSha256"]:
            raise ValueError("Continuation baseline differs from the restored owner image")
        fixture = result.get("load_save_fixture")
        if not isinstance(fixture, dict) or fixture.get("sha256") != record["SaveSha256"]:
            raise ValueError("Continuation loaded another source save")
    return record


def journal(text):
    """Parse an owner input journal (MA_COLONY_INPUT_JOURNAL_DIR).

    Returns (first completed frame of the session, {frame: [inputs]}) where
    each frame's list holds its committed batches in order: zero-time input
    flushes, then the positive frame. Session-local values are normalized:
    native tickets are dropped and a callback is only "answered" or not.
    """
    lines = text.splitlines()
    if not lines or not lines[0].startswith("session "):
        raise ValueError("Not an owner input journal")
    header = lines[0].split()
    if len(header) != 4 or header[2] != "restored_frames":
        raise ValueError("Invalid owner input journal header")
    start = int(header[3]); frames = {}; current = None
    for number, line in enumerate(lines[1:], 2):
        parts = line.split()
        if not parts:
            continue
        if parts[0] == "frame":
            if len(parts) < 8 or parts[2] != "flush" or parts[4] != "seconds":
                raise ValueError("Invalid journal frame line %d" % number)
            frame = int(parts[1]); current = frames.setdefault(frame, [])
            current.append("batch flush %s seconds %s %s" % (parts[3], parts[5], " ".join(parts[8:])))
            continue
        if current is None or parts[0] not in ("T", "P", "W", "H", "R", "F", "G"):
            raise ValueError("Invalid journal input line %d" % number)
        if parts[0] == "T":
            if len(parts) != 13 or parts[9] != "job" or parts[11] != "cb":
                raise ValueError("Invalid journal transfer line %d" % number)
            parts[12] = "-1" if parts[12] == "-1" else "answered"
        current.append(" ".join(parts))
    return start, frames


def compare_inputs(source_text, loaded_text, baseline, count):
    """First frame whose committed owner inputs differ between the branches."""
    source_start, source = journal(source_text)
    loaded_start, loaded = journal(loaded_text)
    if loaded_start != baseline or source_start > baseline:
        raise ValueError("Input journals do not cover the continuation from its saved frame")
    for frame in range(baseline + 1, baseline + count + 1):
        if frame not in source or frame not in loaded:
            raise ValueError("Input journals miss continuation frame %d" % frame)
        if source[frame] != loaded[frame]:
            only_source = [x for x in source[frame] if x not in loaded[frame]]
            only_loaded = [x for x in loaded[frame] if x not in source[frame]]
            return {"frame": frame, "source_inputs": len(source[frame]), "loaded_inputs": len(loaded[frame]),
                    "only_source": only_source[:12], "only_loaded": only_loaded[:12],
                    "same_multiset": sorted(source[frame]) == sorted(loaded[frame])}
    return None


def compare(source, loaded, source_inputs=None, loaded_inputs=None):
    left, right = observation(source, "uninterrupted"), observation(loaded, "restored")
    # Keep player/OS evidence labels distinct. Proton does not become a
    # same-OS Linux/Windows comparison just because its Unity platform agrees.
    for key in ("host_platform", "game_hashes", "package_manifest"):
        if key not in source or source[key] != loaded.get(key):
            raise ValueError("Continuation used different or unidentified " + key)
    platform_key="platform" if "platform" in source else "target_platform"
    if not source.get(platform_key) or source[platform_key]!=loaded.get(platform_key):
        raise ValueError("Continuation used different or unidentified player targets")
    if source["host_platform"] not in ("linux","windows") or not isinstance(source["game_hashes"],dict) or not source["game_hashes"] or not all(digest(v) for v in source["game_hashes"].values()) or not source["package_manifest"]:
        raise ValueError("Continuation lacks host, package or game binary identities")
    for key in ("Frames", "RestoredFrame", "SaveSha256", "BaselineOwnerSha256", "BaselinePipesSha256"):
        if left[key] != right[key]:
            raise ValueError("Continuation does not start at the same saved state: " + key)
    mismatch = None
    for i in range(left["Frames"]):
        different = [key for key in ("SecondsBits", "Hashes", "PipeHashes", "ParticleHashes") if left[key][i] != right[key][i]]
        if different:
            mismatch = {"frame": left["RestoredFrame"] + i + 1, "different": different}
            break
    result = {"status": "matched" if mismatch is None else "mismatch", "frames": left["Frames"],
              "baseline_frame": left["RestoredFrame"], "source_save_sha256": left["SaveSha256"],
              "first_mismatch": mismatch,
              "scope": "bounded completed owner, mixed-pipe/native-schedule and CO2-particle image observations",
              "identical_inputs_verified": False, "full_colony_continuation_verified": False}
    if (source_inputs is None) != (loaded_inputs is None):
        raise ValueError("Input comparison needs both branches' journals")
    if source_inputs is not None:
        divergence = compare_inputs(source_inputs, loaded_inputs, left["RestoredFrame"], left["Frames"])
        result["first_input_mismatch"] = divergence
        result["identical_inputs_verified"] = divergence is None
        if mismatch is not None:
            # A state can differ only after the inputs that produced it differ.
            result["attributed_to"] = "inputs" if divergence is not None and mismatch["frame"] >= divergence["frame"] else "continuation"
            if result["attributed_to"] == "continuation":
                result["status"] = "continuation-mismatch"
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True, help="Uninterrupted headless result.json")
    parser.add_argument("--loaded", type=Path, required=True, help="Cold-load or menu-load result.json")
    parser.add_argument("--source-inputs", type=Path, help="Uninterrupted branch's owner input journal (inputs-<session>.txt)")
    parser.add_argument("--loaded-inputs", type=Path, help="Restored session's owner input journal")
    args = parser.parse_args()
    try:
        result = compare(json.loads(args.source.read_text()), json.loads(args.loaded.read_text()),
                         args.source_inputs.read_text() if args.source_inputs else None,
                         args.loaded_inputs.read_text() if args.loaded_inputs else None)
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.error(str(error))
    print(json.dumps(result, indent=2))
    return 0 if result["status"] == "matched" else 1


if __name__ == "__main__":
    raise SystemExit(main())
