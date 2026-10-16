#!/usr/bin/env python3
"""Check the bounded three-grid front trend using an independent analytic root."""
import argparse
from decimal import Decimal, localcontext
import hashlib
import json
import math
from pathlib import Path

EPS = Decimal(2) ** -52


def values(line):
    return dict(item.split("=", 1) for item in line.split()[1:])


def number(text):
    value = float(text)
    if not math.isfinite(value):
        raise ValueError("Nonfinite exported front quantity")
    return Decimal.from_float(value)


def review(context, rows):
    gamma, c, time, temperature = (number(context[key]) for key in ("gamma", "c", "t", "T"))
    sound = (gamma * 100000).sqrt()
    if gamma <= 1 or abs(c - sound) > EPS * sound or abs(time - Decimal(".5") / sound) > 2 * EPS * time:
        raise ValueError("Front context differs from the specified ideal-gas problem")
    front = 2 + 2 * sound * time / (gamma - 1)
    exact_speed_ratio = 2 / (gamma - 1)
    capacity = Decimal(200000) / ((gamma - 1) * temperature)
    if [int(row["N"]) for row in rows] != [512, 1024, 2048]:
        raise ValueError("Missing or changed front refinement grids")
    output = []
    for row in rows:
        for value in row.values():
            number(value)
        n = int(row["N"])
        if number(row["C"]) != number(".025") or not 0 < int(row["steps"]) <= math.ceil(n * 2.5) + 4:
            raise ValueError("Front discretization or step cap changed")
        if abs(number(row["front_exact"]) - front) > 64 * EPS * abs(front):
            raise ValueError("Printed front oracle disagrees with the independent root")
        position_error = abs(number(row["front"]) - front)
        speed_error = abs(number(row["recent_speed/c"]) - exact_speed_ratio)
        if abs(position_error - number(row["front_error"])) > 64 * EPS * abs(front):
            raise ValueError("Exported front-position error is inconsistent")
        if abs(speed_error - number(row["speed_error/c"])) > 64 * EPS * exact_speed_ratio:
            raise ValueError("Exported recent-speed error is inconsistent")
        if number(row["mass_rel"]) >= Decimal("2e-11") or abs(number(row["energy_rel"])) >= Decimal("2e-10"):
            raise ValueError("Front case failed the recorded conserved-quantity bound")
        if number(row["entropy_change"]) < -512 * EPS * capacity:
            raise ValueError("Front case failed the recorded entropy bound")
        if abs(number(row["wall_momentum_residual"])) >= Decimal("2e-10") * 100000 * time:
            raise ValueError("Front case failed wall reaction balance")
        if output and (position_error >= output[-1]["PositionError"] or speed_error >= output[-1]["SpeedErrorRatio"]):
            raise ValueError("Both front-position and recent-speed errors must strictly decrease")
        output.append(dict(N=n, PositionError=position_error, SpeedErrorRatio=speed_error,
                           ActualFront=number(row["front"]), ActualSpeedRatio=number(row["recent_speed/c"])))
    return output, front, exact_speed_ratio


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stdout", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    lines = args.stdout.read_text().splitlines()
    if len(lines) != 8 or not lines[0].startswith("ORACLE ") or not lines[-1].startswith("PASS retained-material front "):
        raise ValueError("Incomplete retained front run")
    context = values(lines[0])
    rows = [values(line) for line in lines if line.startswith("N=")]
    # N is the first token in a result row, unlike the other tagged records.
    for row, line in zip(rows, (line for line in lines if line.startswith("N="))):
        row["N"] = line.split()[0].split("=", 1)[1]
    with localcontext() as arithmetic:
        arithmetic.prec = 80
        result, front, speed = review(context, rows)
        corrupted = [dict(row) for row in rows]
        corrupted[-1]["recent_speed/c"] = "0"
        corrupted[-1]["speed_error/c"] = str(speed)
        try:
            review(context, corrupted)
        except ValueError:
            corrupt_rejected = True
        else:
            raise ValueError("Front reviewer accepted a nonrefining recent-speed record")
    digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    output = dict(Grids=[row["N"] for row in result], BothErrorsStrictlyDecrease=True,
                  Results=[{key: str(value) if isinstance(value, Decimal) else value for key, value in row.items()} for row in result],
                  AnalyticFrontMetres=str(front), AnalyticFrontSpeedOverC=str(speed),
                  CorruptedSpeedTrendRejected=corrupt_rejected, SourceStdoutSha256=digest(args.stdout), ReviewerSha256=digest(Path(__file__)),
                  NativeExecution=False, DecimalPrecision=80, IndependentAgentReview=False,
                  AccuracyTargetSatisfied=False, EulerianRemapAccepted=False, TwoDimensionalAccepted=False, ProductionAccepted=False)
    with args.output.open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(output, stream, indent=2)
        stream.write("\n")
    print(json.dumps({key: output[key] for key in ("Grids", "BothErrorsStrictlyDecrease", "CorruptedSpeedTrendRejected", "SourceStdoutSha256", "AccuracyTargetSatisfied")}))


if __name__ == "__main__":
    main()
