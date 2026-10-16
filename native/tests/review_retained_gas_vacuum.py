#!/usr/bin/env python3
"""Review exported analytic samples without advancing or importing native code."""
import argparse
import csv
from decimal import Decimal, localcontext
import hashlib
import json
from pathlib import Path

EPSILON = Decimal(2) ** -52


def binary(value):
    return Decimal.from_float(float(value))


def integrate(left, right, gamma, sound, time):
    """Integrate the ideal-gas simple wave in x, using c/c0 as coordinate.

    From the Euler Riemann invariants, u=2c0(1-lambda)/(gamma-1),
    rho=lambda**(2/(gamma-1)), p=100000*lambda**(2*gamma/(gamma-1)).
    Source: https://www.clawpack.org/riemann_book/html/Euler.html
    """
    head = Decimal(2) - sound * time
    speed = 2 * sound / (gamma - 1)
    front = Decimal(2) + speed * time
    uniform = max(Decimal(0), min(right, head) - left)
    mass, momentum, pressure, kinetic = uniform, Decimal(0), Decimal(100000) * uniform, Decimal(0)
    a, b = max(left, head), min(right, front)
    if b <= a:
        return mass, momentum, pressure, kinetic
    exponent = 2 / (gamma - 1)
    width = front - head
    first, last = (front - a) / width, (front - b) / width

    def power_integral(power):
        return width * (first ** (power + 1) - last ** (power + 1)) / (power + 1)

    i0, i1, i2 = (power_integral(exponent + n) for n in range(3))
    mass += i0
    momentum += speed * (i0 - i1)
    pressure += 100000 * power_integral(exponent * gamma)
    kinetic += speed * speed * (i0 - 2 * i1 + i2) / 2
    return mass, momentum, pressure, kinetic


def review_rows(rows, context):
    count = len(rows)
    if count < 16 or count % 4:
        raise ValueError("Invalid fixed-grid sample count")
    gamma, printed_sound, time = (binary(context[key]) for key in ("gamma", "c", "t"))
    sound = (gamma * 100000).sqrt()
    if abs(sound - printed_sound) > EPSILON * sound:
        raise ValueError("Printed sound speed disagrees with the independent ideal-gas root")
    dx = Decimal(8) / count
    maxima = {"rho_exact": Decimal(0), "p_exact": Decimal(0), "u_exact": Decimal(0)}
    mass_sum = Decimal(0)
    for index, row in enumerate(rows):
        left, right = index * dx, (index + 1) * dx
        if binary(row["x"]) != (left + right) / 2:
            raise ValueError("Exported probe geometry differs")
        mass, momentum, pressure, _ = integrate(left, right, gamma, sound, time)
        exact = {"rho_exact": mass / dx, "p_exact": pressure / dx, "u_exact": momentum / mass if mass else Decimal(0)}
        scales = {"rho_exact": Decimal(1), "p_exact": Decimal(100000), "u_exact": sound}
        for field, expected in exact.items():
            error = abs(binary(row[field]) - expected) / scales[field]
            if error > 64 * EPSILON:
                raise ValueError(f"Analytic sample disagrees at row {index}, field {field}")
            maxima[field] = max(maxima[field], error)
        density, occupancy = binary(row["rho"]), binary(row["occupied_fraction"])
        if density < 0 or occupancy < 0 or occupancy > 1 + 8 * EPSILON:
            raise ValueError("Invalid exported material sample")
        mass_sum += density * dx
    if abs(mass_sum - 2) > 64 * EPSILON * 2:
        raise ValueError("Read-only Cartesian sampling lost initial mass")
    if any(binary(rows[-1][key]) != 0 for key in ("rho", "u", "p", "occupied_fraction")):
        raise ValueError("Far exterior sample is not empty; u=0 is only the CSV empty sentinel")
    total = integrate(Decimal(0), Decimal(8), gamma, sound, time)
    if abs(total[0] - 2) > Decimal("1e-60") or abs(total[1] - 100000 * time) > Decimal("1e-60"):
        raise ValueError("Independent analytic mass/momentum closure failed")
    initial_thermal = Decimal(200000) / (gamma - 1)
    energy = total[2] / (gamma - 1) + total[3]
    # The reference derives c independently, rather than requiring a rounded
    # printed sqrt to satisfy an exact high-precision c^2=gamma*p identity.
    if abs(energy - initial_thermal) > 64 * EPSILON * initial_thermal:
        raise ValueError("Independent analytic thermal/kinetic closure failed")
    return {"Rows": count, "MaximumNormalizedAnalyticErrors": {key: str(value) for key, value in maxima.items()},
            "SampledMassErrorKg": str(mass_sum - 2), "AnalyticEnergyErrorJoules": str(energy - initial_thermal)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", type=Path, required=True)
    parser.add_argument("--stdout", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    lines = args.stdout.read_text().splitlines()
    if not lines or not lines[0].startswith("ORACLE "):
        raise ValueError("Missing source numeric context")
    context = dict(item.split("=", 1) for item in lines[0].split()[1:])
    with args.profile.open(newline="") as stream:
        rows = list(csv.DictReader(stream))
    with localcontext() as arithmetic:
        arithmetic.prec = 80
        result = review_rows(rows, context)
        corrupted = [dict(row) for row in rows]
        corrupted[len(rows) // 2]["rho_exact"] = str(binary(corrupted[len(rows) // 2]["rho_exact"]) + Decimal(".01"))
        try:
            review_rows(corrupted, context)
        except ValueError:
            result["CorruptedAnalyticDataRejected"] = True
        else:
            raise ValueError("Analytic reviewer accepted corrupted data")
    digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    result.update(ProfileSha256=digest(args.profile), NativeStdoutSha256=digest(args.stdout),
                  ReviewerSha256=digest(Path(__file__)), DecimalPrecision=80, NativeExecution=False,
                  IndependentAgentReview=False, FrontGatePassed=False)
    with args.output.open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(result, stream, indent=2)
        stream.write("\n")
    print(json.dumps(result))


if __name__ == "__main__":
    main()
