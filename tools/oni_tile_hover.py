#!/usr/bin/env python3
"""Publish an explicit pixel-review receipt for the actual Unity hover fixture."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import tempfile

NAMES = ("default-all-gases", "exact-threshold", "below-threshold", "ui-scale-125")
ASSERTIONS = ("AllNonTraceRowsVisible", "TraceRowsOmitted", "HazardWarningVisible", "NoSelection")
FIELDS = ("Case", "Cell", "Revision", "PublicationGeneration", "ScreenWidth", "ScreenHeight")


def integer(value, minimum=0, maximum=2**63-1):
    if type(value) is not int or not minimum <= value <= maximum:
        raise ValueError("Invalid held observation integer")
    return value


def png_dimensions(path: Path) -> tuple[int, int]:
    with path.open("rb") as source:
        header = source.read(24)
    if len(header) != 24 or header[:16] != b"\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR":
        raise ValueError("Missing actual Unity PNG capture")
    return struct.unpack(">II", header[16:24])


def receipt(report: dict, case: int, assertions: dict) -> dict:
    integer(case, 0, 3)
    if any(assertions.get(name) is not True for name in ASSERTIONS):
        raise ValueError("Review the image and explicitly assert all four pixel checks")
    if (report.get("Kind") != "actual-native-tile-hover-contents-fixture" or report.get("Status") != "pending"
            or report.get("Captured") is not True or report.get("Error") != "" or report.get("CallbackError") != ""
            or report.get("Case") != case):
        raise ValueError("Supply the held completed observation for the reviewed case")
    held = report["Observation"]
    if (integer(held["Case"], 0, 3) != case or held.get("NoSelection") is not True
            or held.get("ShadowBarSelected") is not False or integer(held["DistinctHoverFrames"], 3) < 3):
        raise ValueError("The reviewed observation is selected, incomplete or stale")
    result = {name: integer(held[name], 1 if name.startswith("Screen") else 0) for name in FIELDS}
    return {**result, **{name: True for name in ASSERTIONS}}


def publish(path: Path, value: dict) -> None:
    """Publish complete bytes atomically without replacing another review."""
    encoded = (json.dumps(value, indent=2) + "\n").encode("utf-8")
    with tempfile.NamedTemporaryFile(prefix=".hover-receipt-", suffix=".tmp", dir=path.parent, delete=False) as output:
        temporary = Path(output.name)
        try:
            output.write(encoded); output.flush(); os.fsync(output.fileno())
        except BaseException:
            temporary.unlink(missing_ok=True)
            raise
    try:
        os.link(temporary, path)  # Complete file becomes visible once, without overwrite.
    finally:
        temporary.unlink(missing_ok=True)


def write_receipt(reviewed_report: Path, directory: Path, case: int, assertions: dict) -> Path:
    raw = reviewed_report.read_bytes()
    if len(raw) > 65536:
        raise ValueError("Held report exceeds its byte limit")
    report = json.loads(raw.decode("utf-8-sig"))
    value = receipt(report, case, assertions)
    screenshot = directory / f"{case+1:02}-tile-hover-{NAMES[case]}.png"
    if png_dimensions(screenshot) != (value["ScreenWidth"], value["ScreenHeight"]):
        raise ValueError("Reviewed screenshot geometry differs from the held observation")
    destination = directory / (NAMES[case] + ".continue.json")
    held_path = directory / (NAMES[case] + ".reviewed-report.json")
    publish(held_path, report)
    review = {"Kind": "explicit-operator-tile-hover-pixel-review", "Assertions": assertions,
              "Receipt": value, "ReviewedReport": held_path.name,
              "ReportSha256": hashlib.sha256(held_path.read_bytes()).hexdigest(),
              "Screenshot": screenshot.name, "ScreenshotSha256": hashlib.sha256(screenshot.read_bytes()).hexdigest()}
    # Keep an immutable record of what the operator actually reviewed before
    # releasing Unity's held case. No image assertion is inferred from JSON.
    publish(directory / (NAMES[case] + ".pixel-review.json"), review)
    publish(destination, value)
    return destination


def tile_hover_passed(report: dict | None, directory: Path, expected_platform="LinuxPlayer") -> bool:
    try:
        if (not isinstance(report, dict) or report.get("Kind") != "actual-native-tile-hover-contents-fixture"
                or report.get("Status") != "passed" or report.get("Passed") is not True
                or report.get("Platform") != expected_platform or report.get("FirstPlayableColonyPassed") is not False
                or report.get("Error") != "" or report.get("CallbackError") != ""
                or report.get("PhysicsOwner") != "native ONI; private immutable display inventories only"
                or report.get("RequiresActualNativeHover") is not True or report.get("RequiresPixelReviewReceipt") is not True):
            return False
        evidence = report["Evidence"]
        if len(evidence) != 4:
            return False
        for case, item in enumerate(evidence):
            name = NAMES[case]; held = item["Observation"]
            if item.get("Name") != name or item.get("Passed") is not True:
                return False
            value = receipt(dict(Kind=report["Kind"], Status="pending", Captured=True, Error="", CallbackError="",
                                 Case=case, Observation=held), case, {name: True for name in ASSERTIONS})
            expected_ids = {"nitrogen", "oxygen", "carbon_dioxide"} | ({"steam"} if case != 2 else set())
            if set(held["NonTraceIds"]) != expected_ids or set(held["TraceIds"]) != ({"hydrogen", "chlorine"} | ({"steam"} if case == 2 else set())):
                return False
            if integer(held["Cell"]) != report["TargetCell"] or not held["SourceId"] or len(held["Contents"]) < len(expected_ids):
                return False
            image = directory / f"{case+1:02}-tile-hover-{name}.png"
            review = json.loads((directory / (name + ".pixel-review.json")).read_bytes())
            held_path = directory / (name + ".reviewed-report.json")
            if (item["Screenshot"] != image.name or png_dimensions(image) != (value["ScreenWidth"], value["ScreenHeight"])
                    or json.loads((directory / (name + ".continue.json")).read_bytes()) != value
                    or review.get("Receipt") != value or review.get("Screenshot") != image.name
                    or review.get("Kind") != "explicit-operator-tile-hover-pixel-review"
                    or review.get("ReviewedReport") != held_path.name
                    or review.get("ReportSha256") != hashlib.sha256(held_path.read_bytes()).hexdigest()
                    or receipt(json.loads(held_path.read_bytes()), case, review["Assertions"]) != value
                    or review.get("ScreenshotSha256") != hashlib.sha256(image.read_bytes()).hexdigest()
                    or any(review.get("Assertions", {}).get(name) is not True for name in ASSERTIONS)):
                return False
        return True
    except (OSError, ValueError, KeyError, TypeError):
        return False


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reviewed-report", type=Path, required=True)
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument("--expected-case", type=int, choices=range(4), required=True)
    for name in ASSERTIONS:
        option = {"AllNonTraceRowsVisible": "all-non-trace-rows-visible", "TraceRowsOmitted": "trace-rows-omitted",
                  "HazardWarningVisible": "hazard-warning-visible", "NoSelection": "no-selection"}[name]
        parser.add_argument("--" + option, dest=name, action="store_true")
    args = parser.parse_args()
    print(write_receipt(args.reviewed_report, args.output_directory, args.expected_case,
                        {name: getattr(args, name) for name in ASSERTIONS}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
