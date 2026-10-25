#!/usr/bin/env python3
"""Independent dense Maxwell-Stefan oracle; no production numerical code imported.

Uses the exported transport data and caloric model so closure/model error is
separate from property differences. Ordinary-air envelope: 300 K, about 1 bar,
N2/O2 with up to 0.1% CO2 by mass. Like production, the driving force includes
pressure diffusion, d_k = grad x_k + (x_k - w_k) grad ln p; no Soret effect.
The reference cases are not exactly isobaric (equal cell masses, different
compositions), so the term is exercised; it vanishes at uniform pressure.
"""
import argparse
import json
import math
from pathlib import Path
import subprocess

R = 8.31446261815324


def solve(matrix, rhs):
    a = [list(row) + [b] for row, b in zip(matrix, rhs)]
    n = len(rhs)
    for k in range(n):
        pivot = max(range(k, n), key=lambda i: abs(a[i][k]))
        a[k], a[pivot] = a[pivot], a[k]
        divisor = a[k][k]
        if not divisor:
            raise ValueError("Singular Maxwell-Stefan system")
        a[k] = [v / divisor for v in a[k]]
        for i in range(n):
            if i != k:
                factor = a[i][k]
                a[i] = [x - factor * y for x, y in zip(a[i], a[k])]
    return [row[-1] for row in a]


def flux(state, species, pairs):
    readings = []
    for cell in state:
        masses, energy = cell[:-1], cell[-1]
        moles = [m / s["weight"] for m, s in zip(masses, species)]
        total = math.fsum(moles)
        temperature = (energy - math.fsum(m * s["offset"] for m, s in zip(masses, species))) / math.fsum(m * s["cv"] for m, s in zip(masses, species))
        mass = math.fsum(masses)
        readings.append(([v / total for v in moles], temperature, total, [m / mass for m in masses]))
    x0, t0, n0, y0 = readings[0]
    x1, t1, n1, y1 = readings[1]
    temperature, concentration = (t0 + t1) / 2, (n0 + n1) / 2
    pressure = (n0 * R * t0 + n1 * R * t1) / 2
    log_pressure_step = math.log((n1 * t1) / (n0 * t0))
    active = [i for i in range(len(species)) if state[0][i] or state[1][i]]
    x = [(x0[i] + x1[i]) / 2 for i in active]
    matrix = [[0.0] * len(active) for _ in active]
    rhs = []
    for k, i in enumerate(active):
        drive = (x1[i] - x0[i]) + ((x0[i] + x1[i]) / 2 - (y0[i] + y1[i]) / 2) * log_pressure_step
        rhs.append(-concentration * drive)
        for j, other in enumerate(active):
            if k == j:
                continue
            a, b, c = pairs[tuple(sorted((i, other)))]
            d = 1e-4 * math.exp(a + b / temperature + c * math.log(temperature)) * 101325 / pressure
            matrix[k][k] += x[j] / (species[i]["weight"] * d)
            matrix[k][j] -= x[k] / (species[other]["weight"] * d)
    matrix[-1] = [1.0] * len(active)
    rhs[-1] = 0.0
    values = solve(matrix, rhs)
    result = [0.0] * len(species)
    for i, value in zip(active, values):
        result[i] = value
    heat = math.fsum(value * (s["formation"] + s["cp"] * (temperature - 298.15)) for value, s in zip(result, species))
    return result + [heat]


def advance(state, dt, species, pairs):
    first = flux(state, species, pairs)
    predictor = [[v + sign * dt * j for v, j in zip(cell, first)] for cell, sign in zip(state, (-1, 1))]
    second = flux(predictor, species, pairs)
    return [[v + sign * dt * (a + b) / 2 for v, a, b in zip(cell, first, second)] for cell, sign in zip(state, (-1, 1))]


def mixing_time(case, species, pairs, dt):
    state = case["initial"]
    def span():
        return abs(state[0][1] / math.fsum(state[0][:-1]) - state[1][1] / math.fsum(state[1][:-1]))
    target = .05 * span()
    elapsed = 0
    while span() > target and elapsed < 200000:
        state = advance(state, dt, species, pairs)
        if min(min(c[:-1]) for c in state) < 0:
            raise ValueError("Reference depleted a constituent")
        elapsed += dt
    if elapsed >= 200000:
        raise ValueError("Reference did not mix")
    return elapsed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--emulator",action="append",default=[],help="Optional cross-build executable launcher argument (repeatable)")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    data = json.loads(subprocess.check_output([*args.emulator,str(args.executable.resolve()), "--reference"], text=True))
    pairs = {tuple(sorted((i, j))): (a, b, c) for i, j, a, b, c in data["pairs"]}
    cases = []
    for case in data["cases"]:
        coarse = mixing_time(case, data["species"], pairs, 10)
        fine = mixing_time(case, data["species"], pairs, 5)
        difference = abs(case["time_to_95_s"] / fine - 1)
        production_numerical = abs(case["coarse_time_to_95_s"] / case["time_to_95_s"] - 1)
        oracle_numerical = abs(coarse / fine - 1)
        cases.append(dict(name=case["name"], mixture_averaged_seconds=case["time_to_95_s"], maxwell_stefan_seconds=fine,
                          production_refinement_error=production_numerical, oracle_refinement_error=oracle_numerical,
                          model_mixing_time_error=difference, passed=difference <= .05 and production_numerical <= .01 and oracle_numerical <= .01))
    report = dict(kind="independent-maxwell-stefan-gas-diffusion-review", cases=cases, passed=all(c["passed"] for c in cases))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
