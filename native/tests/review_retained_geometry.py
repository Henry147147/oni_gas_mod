#!/usr/bin/env python3
"""Check exported coordinate/volume/gravity operands with exact rational arithmetic."""
import argparse
from fractions import Fraction
import hashlib
import json
from pathlib import Path

EPS = Fraction(1, 2**52)


def number(value):
    return Fraction.from_float(float(value))


def fields(line):
    return dict(item.split("=", 1) for item in line.split()[1:])


def width(row):
    return number(row["right_hi"]) + number(row["right_lo"]) - number(row["left_hi"]) - number(row["left_lo"])


def review_geometry(rows):
    maximum = Fraction(0)
    scalar_loss_seen = False
    for index, row in enumerate(rows):
        if int(row["cell"]) != index:
            raise ValueError("Coordinate rows are missing or out of order")
        exact = width(row)
        stored = number(row["volume"])
        if exact <= 0 or abs(stored - exact) > EPS * exact:
            raise ValueError("Stored volume disagrees with complete coordinate parts")
        maximum = max(maximum, abs(stored - exact) / (EPS * exact))
        scalar = number(row["right_hi"]) - number(row["left_hi"])
        scalar_loss_seen |= abs(scalar - exact) > 32 * EPS * exact
        if index and (row["left_hi"], row["left_lo"]) != (rows[index - 1]["right_hi"], rows[index - 1]["right_lo"]):
            raise ValueError("Adjacent material coordinates do not share the same complete node")
    if len(rows) != 128 or not scalar_loss_seen:
        raise ValueError("Control does not reproduce scalar coordinate loss")
    return maximum


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stdout", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    lines = args.stdout.read_text().splitlines()
    if sum(line.startswith("PASS ") for line in lines) != 3 or any(line.startswith("FAIL ") for line in lines):
        raise ValueError("Native geometry controls did not all complete")
    rows = [fields(line) for line in lines if line.startswith("GEOMETRY ")]
    maximum = review_geometry(rows)
    bad = [dict(row) for row in rows]
    bad[-1]["right_lo"] = str(float(bad[-1]["right_lo"]) + .001)
    try:
        review_geometry(bad)
    except ValueError:
        corrupt_rejected = True
    else:
        raise ValueError("Rational coordinate reviewer accepted an altered low component")
    rebase = next(fields(line) for line in lines if line.startswith("REBASE "))
    if number(rebase["left_hi"]) != number(rebase["right_hi"]) or width(rebase) != number(rebase["volume"]) or width(rebase) <= 0:
        raise ValueError("Short rebased cell lost its exact coordinate width")
    nodes = [fields(line) for line in lines if line.startswith("GRAVITY_NODE ")]
    gravity = next(fields(line) for line in lines if line.startswith("GRAVITY "))
    if [int(node["node"]) for node in nodes] != list(range(9)):
        raise ValueError("Missing whole-state gravity coordinate operands")
    origin_delta = number(gravity["origin_next_hi"]) + number(gravity["origin_next_lo"]) - number(gravity["origin_old_hi"]) - number(gravity["origin_old_lo"])
    changes = [number(node["next_hi"]) + number(node["next_lo"]) - number(node["old_hi"]) - number(node["old_lo"]) for node in nodes]
    g = number(gravity["g"])
    terms = [number(nodes[index]["mass_right"]) * g * (origin_delta + (changes[index] + changes[index + 1]) / 2) for index in range(8)]
    potential, scale = sum(terms), sum(abs(term) for term in terms)
    if not scale or origin_delta or changes[0] or changes[-1]:
        raise ValueError("Gravity control lost its nonzero motion or fixed walls/origin")
    for name in ("potential_delta", "reported_potential_delta"):
        if abs(number(gravity[name]) - potential) > 128 * EPS * scale:
            raise ValueError("Gravity work differs from complete coordinate operands")
    digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    result = dict(CoordinateRows=128, AdjacentNodeIdentities=127, RebasedShortCellExact=True,
                  GravityNodes=9, GravityCells=8, MaximumStoredWidthErrorInEps=float(maximum),
                  GravityPotentialErrorJoules=float(number(gravity["reported_potential_delta"]) - potential),
                  CorruptedLowComponentRejected=corrupt_rejected, ExactRationalArithmetic=True,
                  NativeExecution=False, IndependentAgentReview=False, SourceStdoutSha256=digest(args.stdout), ReviewerSha256=digest(Path(__file__)))
    with args.output.open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(result, stream, indent=2)
        stream.write("\n")
    print(json.dumps(result))


if __name__ == "__main__":
    main()
