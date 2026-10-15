#!/usr/bin/env python3
"""Independent bounded nonlinear passive oracle; no native/helper imports.

IEEE bits -> Fraction, exact polynomial primitives/overlaps, enclosing absolute
integrals, independent point/dual/union norms. A numerical failure is preserved
and exits nonzero. This does not accept whole A2 or pressure/front dynamics.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
from decimal import Decimal, localcontext
from fractions import Fraction as F
import hashlib
import json
import math
from pathlib import Path
import struct
import sys

EPS = F(1, 2**52)
R = F.from_float(8.31446261815324)
TREF = F.from_float(298.15)
TIME = F(1, 8)
RHO = (F(1), F(1, 4), F(1, 8))
U = (F(1), F(1, 8), F(1, 16), F(1, 32))
YB = (F(1, 4), F(1, 16), F(1, 32))
YT = (F(1, 2**40), F(1, 2**41), F(1, 2**42))


class GateFailure(RuntimeError):
    def __init__(self, message: str, evidence=None):
        super().__init__(message)
        self.evidence = evidence


def need(condition, message, evidence=None):
    if not condition:
        raise GateFailure(message, evidence)


def bits(value: str) -> F:
    need(isinstance(value, str) and len(value) == 16 and all(c in "0123456789abcdef" for c in value), "Bad IEEE scalar")
    result = struct.unpack(">d", bytes.fromhex(value))[0]
    need(math.isfinite(result), "Nonfinite IEEE scalar")
    return F.from_float(result)


def rounded(x: F) -> F:
    return F.from_float(float(x))


def rounding_preimage(value: F):
    """Closed exact interval rounding to one finite binary64 output.

    Including both midpoint ties is conservative. At the finite range limit,
    use the next lattice value rather than converting an infinity to Fraction.
    This is an output-conversion certificate, not an EOS/volume tolerance.
    """
    scalar = float(value)
    need(math.isfinite(scalar) and F.from_float(scalar) == value,
         "Rounding certificate requires an exact finite binary64 output")
    previous, following = math.nextafter(scalar, -math.inf), math.nextafter(scalar, math.inf)
    left = F.from_float(previous) if math.isfinite(previous) else value-(F.from_float(following)-value)
    right = F.from_float(following) if math.isfinite(following) else value+(value-F.from_float(previous))
    return (left+value)/2, (value+right)/2


def operation_roundoff(magnitude: F):
    """Bound round-to-nearest error for |exact result| <= magnitude.

    Binary64 half-spacing is monotone with magnitude. The larger neighboring
    half-spacing at the nearest value also covers a binade boundary. A zero
    magnitude means an exact zero operation; nonzero subnormal results use
    their actual local format quantum, never a unit/material-sized floor.
    Precision at least binary64 is covered; unresolved range fails closed.
    """
    need(magnitude >= 0 and magnitude <= F.from_float(sys.float_info.max),
         "Gas-volume arithmetic enclosure exceeds its finite supported range")
    if magnitude == 0:
        return F(0)
    nearest = rounded(magnitude)
    low, high = rounding_preimage(nearest)
    return max(nearest-low, high-nearest)


def three_positive_moles(masses, molar):
    """Enclose the three divisions and compensated positive sum independently.

    For three terms the magnitude-ordered FastTwoSum residuals e2/e3 are
    exact, including gradual underflow. Only their correction addition and
    the final value+correction add need further rounding bounds. See the
    separately frozen R4 derivation for the complete forward-error algebra.
    """
    need(len(masses) == len(molar) == 3 and all(x > 0 for x in masses+molar),
         "Gas-volume certificate requires exactly three positive constituents")
    terms = [m/w for m, w in zip(masses, molar)]
    exact = sum(terms, F(0))
    division_error = sum((operation_roundoff(x) for x in terms), F(0))
    upper = exact+division_error
    first_sum_error = operation_roundoff(upper)
    value_error = first_sum_error+operation_roundoff(upper+first_sum_error)
    correction_error = operation_roundoff(value_error)
    get_error = operation_roundoff(upper+correction_error)
    error = division_error+correction_error+get_error
    need(exact > error, "Positive mole sum is unresolved by local arithmetic")
    return exact, error


def gas_volume_certificate(cell, profiles):
    """Authenticate nRT/p and its signed stored-volume residual locally.

    All-gas finish subtracts stored V from its internal gasV. Once both are
    within a factor two, Sterbenz makes that subtraction exact. The SAME
    internal gasV must therefore belong to its independently reconstructed
    interval, its exported gasV rounding preimage, and V plus the exported
    signed-residual rounding preimage. No bulk/global/unit error floor enters.
    """
    need(bits(cell["liquidV"]) == bits(cell["vacuumV"]) == 0 and
         cell["hasgas"] is True and cell["hasvacuum"] is False,
         "Ideal-gas state acquired a false phase/headspace")
    volume, gas, residual, temperature, pressure = [bits(cell[k]) for k in ("V", "gasV", "volume_residual", "T", "p")]
    need(volume > 0 and gas > 0 and temperature > 0 and pressure > 0,
         "Gas-volume certificate lost positive finite volume/temperature/pressure")
    masses = [bits(x) for x in cell["m"]]
    molar = [bits(p["molar"]) for p in profiles]
    moles, mole_error = three_positive_moles(masses, molar)
    low, high = moles-mole_error, moles+mole_error
    # Current R is a declared double promoted to Real. Its full double
    # preimage also encloses a higher-precision literal with the same declared
    # output; T/p are independently exported Real-to-double results.
    for a, b in (rounding_preimage(R), rounding_preimage(temperature)):
        need(a > 0, "Gas-volume multiplier has no resolved positive preimage")
        low, high = low*a, high*b
        error = operation_roundoff(high)
        low, high = low-error, high+error
        need(low > 0, "Positive gas-volume product is unresolved locally")
    a, b = rounding_preimage(pressure)
    need(a > 0, "Gas-volume pressure has no resolved positive preimage")
    low, high = low/b, high/a
    error = operation_roundoff(high)
    low, high = low-error, high+error
    need(low >= volume/2 and high <= 2*volume,
         "Gas-volume closure lies outside its certified close-subtraction domain")
    gas_interval, residual_interval = rounding_preimage(gas), rounding_preimage(residual)
    common_low = max(low, gas_interval[0], volume+residual_interval[0])
    common_high = min(high, gas_interval[1], volume+residual_interval[1])
    defect = gas-volume-residual
    conversion_bound = max(gas-gas_interval[0], gas_interval[1]-gas)+max(residual-residual_interval[0], residual_interval[1]-residual)
    certificate = {"stored_volume": volume, "published_gas_volume": gas, "published_signed_residual": residual,
                   "exact_published_nRT_over_p": moles*R*temperature/pressure,
                   "moles_exact": moles, "moles_arithmetic_bound": mole_error,
                   "reconstructed_internal_gas_volume": (low, high),
                   "gas_output_preimage": gas_interval, "residual_output_preimage": residual_interval,
                   "common_internal_gas_volume": (common_low, common_high),
                   "signed_published_conversion_defect": defect, "local_output_conversion_bound": conversion_bound}
    need(common_low <= common_high,
         "No common internal gas volume authenticates nRT/p and its signed residual", certificate)
    need(abs(defect) <= conversion_bound,
         "Signed gas-volume residual exceeds its two local output conversions", certificate)
    return certificate


def add(a, b):
    return tuple((a[i] if i < len(a) else F(0)) + (b[i] if i < len(b) else F(0)) for i in range(max(len(a), len(b))))


def neg(a):
    return tuple(-x for x in a)


def mul(a, b):
    result = [F(0)] * (len(a) + len(b) - 1)
    for i, x in enumerate(a):
        for j, y in enumerate(b):
            result[i+j] += x*y
    return tuple(result)


YA = add((F(1),), neg(add(YB, YT)))
SPECIES = tuple(mul(RHO, p) for p in (YA, YB, YT))


def at(p, x):
    result = F(0)
    for c in reversed(p):
        result = result*x+c
    return result


def integrate(p, a=F(0), b=F(1)):
    return sum((c*(b**(i+1)-a**(i+1))/(i+1) for i, c in enumerate(p)), F(0))


def bernstein(p, a, b):
    """Exact conversion to Bernstein coefficients on the requested interval."""
    n = len(p)-1
    power = [sum((p[j]*math.comb(j, k)*a**(j-k)*(b-a)**k for j in range(k, n+1)), F(0)) for k in range(n+1)]
    return [sum((power[k]*F(math.comb(i, k), math.comb(n, k)) for k in range(i+1)), F(0)) for i in range(n+1)]


@dataclass(frozen=True)
class Bound:
    lo: F
    hi: F

    def __post_init__(self):
        need(F(0) <= self.lo <= self.hi, "Invalid nonnegative enclosure", {"lo": self.lo, "hi": self.hi})

    def __add__(self, other):
        if isinstance(other, int) and other == 0:
            return self
        return Bound(self.lo+other.lo, self.hi+other.hi)

    __radd__ = __add__

    @staticmethod
    def exact(x):
        return Bound(x, x)


def heat_rate_norm(history, uniform):
    """Uniform controls measure signed arithmetic heat by magnitude only.

    Raw Q/Qarith stay signed in every ledger and packet/conservation check.
    Nonuniform heat/refinement diagnostics retain their previous definition.
    """
    q = bits(history["Q"])
    if uniform:
        bound = bits(history["Kbound"])
        need(abs(q) <= bound, "Uniform control nonarithmetic heat", {"signed_Q": q, "Kbound": bound})
        return Bound.exact(abs(q)/TIME)
    return Bound.exact(q/TIME)


def absolute_integral(p, weight, a, b, depth=0):
    """Enclose integral weight*abs(p), weight positive on [0,1].

    Bernstein convex hulls certify signs; an unresolved root is enclosed after
    forty exact dyadic subdivisions. A float root suggestion only accelerates
    subdivision and is used solely after exact sign/interval certification.
    No scalar convergence decision uses a float approximation.
    """
    if a == b:
        return Bound.exact(F(0))
    coeff = bernstein(p, a, b)
    if min(coeff) >= 0:
        return Bound.exact(integrate(mul(p, weight), a, b))
    if max(coeff) <= 0:
        return Bound.exact(-integrate(mul(p, weight), a, b))
    if depth >= 40:
        return Bound(F(0), (b-a)*max(abs(c) for c in coeff)*max(bernstein(weight, a, b)))
    pa, pb = at(p, a), at(p, b)
    if depth == 0 and pa*pb < 0:
        # Fixed forty bisections; any float cancellation simply fails the
        # exact certificate and falls back to the exact Bernstein path.
        left, right = float(a), float(b)
        fp = [float(c) for c in p]
        for _ in range(40):
            mid = (left+right)*.5
            value = 0.0
            for c in reversed(fp):
                value = value*mid+c
            if (value > 0) == (pa > 0):
                left = mid
            else:
                right = mid
        l, r = F.from_float(left), F.from_float(right)
        if a < l < r < b and at(p, l)*at(p, r) <= 0:
            cl, cr = bernstein(p, a, l), bernstein(p, r, b)
            if (min(cl) >= 0 or max(cl) <= 0) and (min(cr) >= 0 or max(cr) <= 0):
                middle = (r-l)*max(abs(c) for c in bernstein(p, l, r))*max(bernstein(weight, l, r))
                outside = abs(integrate(mul(p, weight), a, l))+abs(integrate(mul(p, weight), r, b))
                return Bound(outside, outside+middle)
    mid = (a+b)/2
    return absolute_integral(p, weight, a, mid, depth+1)+absolute_integral(p, weight, mid, b, depth+1)


def supremum(p, a, b):
    """Enclose max abs polynomial with exact Bernstein branch and bound."""
    scale = sum(abs(x) for x in p)
    if scale == 0:
        return Bound.exact(F(0))
    target = scale/F(2**40)
    lower = max(abs(at(p, a)), abs(at(p, b)))
    work = [(a, b, max(abs(x) for x in bernstein(p, a, b)), 0)]
    while True:
        upper = max([lower]+[part[2] for part in work])
        if upper-lower <= target:
            return Bound(lower, upper)
        index = max(range(len(work)), key=lambda i: work[i][2])
        left, right, _, depth = work.pop(index)
        need(depth < 60, "Unresolved polynomial supremum enclosure")
        mid = (left+right)/2
        lower = max(lower, abs(at(p, mid)))
        for l, r in ((left, mid), (mid, right)):
            high = max(abs(x) for x in bernstein(p, l, r))
            if high > lower:
                work.append((l, r, high, depth+1))


def line(a, b, ua, ub):
    slope = (ub-ua)/(b-a)
    return ua-slope*a, slope


def interp(q, u, x):
    if x == q[-1]:
        return u[-1]
    for i in range(len(u)-1):
        if q[i] <= x < q[i+1]:
            return at(line(q[i], q[i+1], u[i], u[i+1]), x)
    raise GateFailure("Interpolation outside occupied partition")


def pl_union(a, b):
    qa, ua = a
    qb, ub = b
    need(qa[0] == qb[0] == 0 and qa[-1] == qb[-1] == 1, "Node-union slab mismatch")
    q = sorted(set(qa+qb))
    total = Bound.exact(F(0))
    high = F(0)
    for l, r in zip(q, q[1:]):
        dl, dr = interp(qa, ua, l)-interp(qb, ub, l), interp(qa, ua, r)-interp(qb, ub, r)
        total += absolute_integral(line(l, r, dl, dr), RHO, l, r)
        high = max(high, abs(dl), abs(dr))
    return total, Bound.exact(high)


def cell_union(a, b):
    qa, va = a
    qb, vb = b
    i = j = 0
    total = F(0)
    while i < len(va) and j < len(vb):
        l, r = max(qa[i], qb[j]), min(qa[i+1], qb[j+1])
        if r > l:
            total += (r-l)*abs(va[i]-vb[j])
        ai, bj = qa[i+1] <= qb[j+1], qb[j+1] <= qa[i+1]
        i += int(ai)
        j += int(bj)
    need(i == len(va) and j == len(vb), "Incomplete cell-union coverage")
    return Bound.exact(total)


def decimal(x):
    return Decimal(x.numerator)/Decimal(x.denominator)


def scalar_close(actual, exact, coefficient, scale, message):
    need(abs(actual-exact) <= coefficient*EPS*scale, message,
         {"actual": str(actual), "exact": str(exact), "error": str(actual-exact), "bound": str(coefficient*EPS*scale)})


def state_arrays(state, reflected=False):
    q = [bits(n["q"]) for n in state["nodes"]]
    u = [bits(n["u"]) for n in state["nodes"]]
    inertia = [bits(n["I"]) for n in state["nodes"]]
    cells = state["cells"]
    if reflected:
        q, u, inertia, cells = [1-x for x in reversed(q)], [-x for x in reversed(u)], list(reversed(inertia)), list(reversed(cells))
    return q, u, inertia, cells


def expected_geometry(config, tau):
    n, d, phase, carrier = config["N"], config["d"], config["phase"], config["carrier"]
    need(n in (8, 16, 32, 64) and d in (2, 4, 8, 16, 64, 256) and phase in range(4) and carrier in (-1, 1), "Foreign history parameters")
    h = F(1, n)
    travel = min(tau, TIME-tau) if config["reverse"] else tau
    origin = phase*h/4-carrier*travel
    eligible = [(k, origin+k*h) for k in range(-n, 2*n+1) if 2*h <= origin+k*h <= 1-2*h]
    return [F(0), h]+[x for _, x in eligible]+[1-h, F(1)], [k for k, _ in eligible[:-1]], origin


def actual_totals(state):
    masses = [sum((bits(c["m"][s]) for c in state["cells"]), F(0)) for s in range(3)]
    p = sum((bits(n["J"]) for n in state["nodes"]), F(0))
    k = sum((bits(n["J"])**2/(2*bits(n["I"])) for n in state["nodes"]), F(0))
    u = sum((bits(c["U"]) for c in state["cells"]), F(0))
    thermal = sum((bits(c["thermal"]) for c in state["cells"]), F(0))
    return masses, p, k, u, thermal


def verify_profiles(state):
    profiles = state["profiles"]
    need(len(profiles) == 3 and "hybrid-nonlinear-passive-v1" in state["model_metadata"], "Wrong immutable surrogate model version")
    # Atomic weights and declared cp are inputs of this numerical test, not a
    # lookup from native registry/thermodynamic/transport implementation.
    weights = (F.from_float(2*.0140067), F.from_float(2*.0159994), F.from_float(.0120107+2*.0159994))
    for s, cp in enumerate((1040, 920, 850)):
        need(profiles[s]["id"] == ("surrogate-A", "surrogate-B", "surrogate-trace")[s] and
             profiles[s]["provenance"] == "Nonlinear passive numerical surrogate, cp 1040/920/850; no physical calibration", "Wrong surrogate identity/provenance")
        need(bits(profiles[s]["molar"]) == weights[s] and bits(profiles[s]["Tmin"]) == F.from_float(1e-20) and bits(profiles[s]["Tmax"]) == 10**9, "Wrong declared species/range metadata")
        expected = cp-R/weights[s]
        scalar_close(bits(profiles[s]["cv"]), expected, 2, F(cp), "Declared composition-dependent caloric profile")
        scalar_close(bits(profiles[s]["u0"]), F(0), 64, cp*TREF, "Declared approximately zero caloric offset")


def coverage_certificate(expected, covered):
    """Authenticate A0's published two-term binary64 arithmetic certificate.

    This is metadata verification, separate from exact independent coverage
    geometry/sum/residual checks. It is not a new physical acceptance budget.
    """
    return rounded(rounded(64*EPS*abs(expected))+rounded(64*EPS*abs(covered)))


def verify_coverage(q, grid, rows, balances, partition):
    origin, low, h = [bits(x) for x in grid[:3]]
    first, end = grid[3:]
    need(low == 0 and h > 0 and first < end, "Foreign coverage grid")
    expected = []
    counts = []
    for cell, (a, b) in enumerate(zip(q, q[1:])):
        count = 0
        for k in range(first, end):
            left, right = max(a, origin+k*h), min(b, origin+(k+1)*h)
            if right > left:
                expected.append((partition, cell, k, left, right, right-left, right-left))
                count += 1
        counts.append(count)
    actual = [(r[0], r[1], r[2])+tuple(bits(x) for x in r[3:]) for r in rows]
    need(actual == expected, "Independent positive fixed-grid coverage mismatch")
    need(len(balances) == len(q)-1, "Coverage balance count mismatch")
    for i, balance in enumerate(balances):
        need(balance[:3] == [partition, i, counts[i]], "Coverage balance ownership/count mismatch")
        width = q[i+1]-q[i]
        values = [bits(x) for x in balance[3:]]
        need(values[0] == values[1] == values[4] == values[5] == width and values[2] == values[6] == 0,
             "Independent represented coverage sum/residual mismatch")
        need(values[3] == coverage_certificate(values[0], values[1]) and
             values[7] == coverage_certificate(values[4], values[5]),
             "Coverage published certificate differs from the accepted two-term local formula")


def verify_state(state, config, tau, initial=None):
    q, velocity, inertia, cells = state_arrays(state)
    expected, ids, _ = expected_geometry(config, tau)
    need(q == expected, "Actual partition differs from independent integer-grid geometry", {"actual": list(map(str, q)), "expected": list(map(str, expected))})
    need(len(cells)+1 == len(q) and all(a < b for a, b in zip(q, q[1:])), "Nonpositive/overlapping occupied geometry")
    need([identity[3] for identity in state["ids"] if identity[0] == 6] == ids if tau else True, "Actual Cartesian ID set differs from independent geometry")
    if tau:
        origin = expected_geometry(config, tau)[2]
        need(state["has_grid"] and [bits(x) for x in state["grid"][:3]] == [origin, F(0), F(1, config["N"])] and state["grid"][3:] == [-config["N"], 2*config["N"]], "Accepted preferred-grid identity mismatch")
        verify_coverage(q, state["grid"], state["coverage"], state["coverage_balances"], 0)
    else:
        need(not state["has_grid"] and not state["coverage"] and not state["coverage_balances"], "Initial material was silently rezoned/rebound")
    profiles = state["profiles"]
    verify_profiles(state)
    need(state["frame"][:5] == ["0000000000000000", "0000000000000000", "3ff0000000000000", "0000000000000000", "0000000000000000"], "Foreign frame/body/time")
    masses, p, k, u, thermal = actual_totals(state)
    with localcontext() as context:
        context.prec = 70
        for i, cell in enumerate(cells):
            m = [bits(x) for x in cell["m"]]
            need(all(x > 0 for x in m), "Dropped positive/trace constituent")
            need(bits(cell["V"]) == q[i+1]-q[i], "Stored volume differs from represented geometry")
            observed_bulk = F(0)
            for x in m:
                observed_bulk = rounded(observed_bulk+x)
            need(bits(cell["M"]) == observed_bulk, "Native scalar bulk mass does not equal actual ordered sum")
            reference = sum((m[s]*bits(profiles[s]["u0"]) for s in range(3)), F(0))
            capacity = sum((m[s]*bits(profiles[s]["cv"]) for s in range(3)), F(0))
            moles = [m[s]/bits(profiles[s]["molar"]) for s in range(3)]
            temperature = (bits(cell["U"])-reference)/capacity
            need(all(bits(profiles[s]["Tmin"]) <= temperature <= bits(profiles[s]["Tmax"]) for s in range(3)), "Actual conserved temperature outside declared domain")
            pressure = R*temperature*sum(moles)/bits(cell["V"])
            scalar_close(bits(cell["T"]), temperature, 64, temperature, "Independent Fraction temperature")
            scalar_close(bits(cell["p"]), pressure, 64, pressure, "Independent Fraction pressure")
            entropy = decimal(capacity)*(decimal(temperature)/decimal(TREF)).ln()
            for n in moles:
                entropy += decimal(n*R)*(decimal(bits(cell["V"]))/decimal(n)).ln()
            observed = decimal(bits(cell["S"]))
            need(abs(observed-entropy) <= decimal(128*EPS)*(abs(entropy)+decimal(capacity)), "Independent Decimal ideal-mixture entropy")
            try:
                gas_volume_certificate(cell, profiles)
            except GateFailure as error:
                raise GateFailure(str(error), {"cell_index": i, "certificate": error.evidence}) from error
            exact_bulk_p = observed_bulk*(velocity[i]+velocity[i+1])/2
            scalar_close(bits(cell["Pbulk"]), exact_bulk_p, 8, abs(exact_bulk_p), "Derived compatible cell momentum")
            exact_variance = observed_bulk*(velocity[i+1]-velocity[i])**2/8
            scalar_close(bits(cell["variance"]), exact_variance, 8, exact_variance, "Derived mechanical variance from actual canonical nodes")
    for j, node in enumerate(state["nodes"]):
        need(bits(node["u"]) == rounded(bits(node["J"])/bits(node["I"])), "Canonical velocity division mismatch")
        need(bits(node["K"]) == rounded(bits(node["J"])**2/(2*bits(node["I"]))), "Independent exact-rational nodal K mismatch")
        exact_inertia = (bits(cells[j-1]["M"])/2 if j else F(0))+(bits(cells[j]["M"])/2 if j < len(cells) else F(0))
        scalar_close(bits(node["I"]), exact_inertia, 4, exact_inertia, "Canonical half-mass gather")
    if initial is not None:
        for side in (0, -1):
            need(state["nodes"][side] == initial["nodes"][side], "Physical outer packet/trace not bitwise identical")
            need(state["cells"][side]["m"] == initial["cells"][side]["m"] and state["cells"][side]["V"] == initial["cells"][side]["V"], "Physical cap inventory/volume changed")
        need(state["caps"] == initial["caps"] and state["limits"] == initial["limits"] and state["model_handle"] == initial["model_handle"] and state["model_metadata"] == initial["model_metadata"] and state["profiles"] == initial["profiles"] and state["initialization"] == initial["initialization"] and state["unresolved_initialization"] == initial["unresolved_initialization"] and state["identity"][0] == initial["identity"][0] and state["identity"][1] == int(tau*config["N"]*config["d"]) and state["identity"][2] != initial["identity"][2], "Immutable identity/metadata/conversion changed")
        m0, p0, k0, u0, th0 = actual_totals(initial)
        for s in range(3):
            scalar_close(masses[s], m0[s], 128, m0[s], "Cumulative own-species mass conservation")
        scalar_close(p, p0, 128, sum(abs(bits(n["J"])) for n in initial["nodes"]), "Cumulative actual canonical momentum")
        scalar_close(u+k, u0+k0, 256, th0+thermal+k0+k, "Cumulative actual U+K+PE")
        entropy = sum((bits(c["S"]) for c in cells), F(0))
        entropy0 = sum((bits(c["S"]) for c in initial["cells"]), F(0))
        scale = sum((abs(bits(c["S"]))+bits(c["cv"]) for c in cells+initial["cells"]), F(0))
        need(entropy-entropy0 >= -128*EPS*scale, "Independent cumulative actual EOS entropy loss")
    return masses, p, k, u, thermal


def error_norms(state, config):
    q, u, inertia, cells = state_arrays(state, config["reflected"])
    velocity = (F(1),) if config["uniform"] else U
    amounts = [F(0)]*3
    rho_amount = F(0)
    fields = [Bound.exact(F(0)) for _ in range(4)]
    pl = Bound.exact(F(0))
    windows = [Bound.exact(F(0)), Bound.exact(F(0))]
    inf = Bound.exact(F(0))
    for i, cell in enumerate(cells):
        a, b = q[i], q[i+1]
        rho_amount += abs(bits(cell["M"])-integrate(RHO, a, b))
        actual_fields = [bits(cell["M"])/(b-a)]+[bits(x)/(b-a) for x in cell["m"]]
        for s in range(3):
            amounts[s] += abs(bits(cell["m"][s])-integrate(SPECIES[s], a, b))
        for s, analytic in enumerate((RHO,)+SPECIES):
            fields[s] += absolute_integral(add((actual_fields[s],), neg(analytic)), (F(1),), a, b)
        difference = add(line(a, b, u[i], u[i+1]), neg(velocity))
        pl += absolute_integral(difference, RHO, a, b)
        local = supremum(difference, a, b)
        inf = Bound(max(inf.lo, local.lo), max(inf.hi, local.hi))
        for side, (left, right) in enumerate(((F(0), F(1, 8)), (F(7, 8), F(1)))):
            l, r = max(a, left), min(b, right)
            if r > l:
                windows[side] += absolute_integral(difference, RHO, l, r)
    point = sum((inertia[j]*abs(u[j]-at(velocity, q[j])) for j in range(len(q))), F(0))
    dual = dualmass = F(0)
    dual_profile = []
    for j in range(len(q)):
        a = q[j] if j == 0 else (q[j-1]+q[j])/2
        b = q[j] if j == len(q)-1 else (q[j]+q[j+1])/2
        mass = integrate(RHO, a, b)
        mean = integrate(mul(RHO, velocity), a, b)/mass
        dual += inertia[j]*abs(u[j]-mean)
        dualmass += abs(inertia[j]-mass)
        dual_profile.append({"q": q[j], "point_u": u[j], "exact_point": at(velocity, q[j]), "geo_mass": mass, "lumped_I": inertia[j], "geo_mean": mean})
    result = {f"mass{s}": Bound.exact(amounts[s]) for s in range(3)}
    result.update(rho=Bound.exact(rho_amount), node=Bound.exact(point), PL=pl, inf=inf,
                  left_window=windows[0], right_window=windows[1], dual=Bound.exact(dual), dualmass=Bound.exact(dualmass))
    result.update({f"field{s}": fields[s] for s in range(4)})
    result["left_bias"] = Bound.exact(abs(u[1]-at(velocity, q[1])))
    result["right_bias"] = Bound.exact(abs(u[-2]-at(velocity, q[-2])))
    return result, dual_profile


def overlaps(a, b):
    i = j = 0
    result = []
    while i+1 < len(a) and j+1 < len(b):
        l, r = max(a[i], b[j]), min(a[i+1], b[j+1])
        if r > l:
            result.append((i, j, l, r))
        ai, bj = a[i+1] <= b[j+1], b[j+1] <= a[i+1]
        i += int(ai)
        j += int(bj)
    need(i == len(a)-1 and j == len(b)-1, "Independent overlap lost coverage")
    return result


def corners(q):
    result = []
    for a, b in zip(q, q[1:]):
        result += [a, (a+b)/2]
    return result+[q[-1]]


def balance_half(left, right, mass):
    """Exact positive material redistribution; no tool/report inputs."""
    left, right = list(left), list(right)
    heavy, light = (left, right) if left[0] >= right[0] else (right, left)
    transfer = heavy[0]-mass/2
    need(transfer >= 0 and all(x > 0 for x in (heavy[0], light[0])), "Invalid exact positive half-mass rebalance")
    moved = [transfer, heavy[1]*transfer/heavy[0], heavy[2]*transfer/heavy[0]]
    for k in range(3):
        heavy[k] -= moved[k]
        light[k] += moved[k]
    return left, right, transfer


def packet_oracle(document):
    """Exact geometric integration + ideal positive material half rebalance.

    Actual source bulk mass, J/I and protected packets are inputs. A1's scalar
    quadrature normalization is assessed separately; it never repairs primal
    species or U. Bounds count local operations and measured packet errors.
    """
    before, after, report = document["before"], document["after"], document["report"]
    qa = [bits(n["q"]) for n in before["nodes"]]
    qb = [bits(n["q"]) for n in after["nodes"]]
    plan = document["plan"]
    need(plan["source"] == before["identity"] and report["before"] == before["identity"] and report["after"] == after["identity"] and
         before["identity"][0] == after["identity"][0] and before["identity"][1]+1 == after["identity"][1] and before["identity"][2] != after["identity"][2], "Independent immutable plan/publication identity mismatch")
    need([bits(x) for x in plan["q"]] == qb, "Actual target differs from authenticated plan geometry")
    verify_coverage(qa, plan["grid"], plan["source_coverage"], plan["source_balances"], 0)
    verify_coverage(qb, plan["grid"], plan["target_coverage"], plan["target_balances"], 1)
    ca, cb = before["cells"], after["cells"]
    profiles = before["profiles"]
    source_nonref = [bits(c["U"])-sum((bits(c["m"][s])*bits(profiles[s]["u0"]) for s in range(3)), F(0)) for c in ca]
    ideal_m = [[F(0)]*3 for _ in cb]
    ideal_nonref = [F(0)]*len(cb)
    primal = overlaps(qa, qb)
    need([(r["donor"], r["target"], bits(r["length"])) for r in report["primal"]] == [(i, j, r-l) for i, j, l, r in primal], "Independent primal overlap topology")
    for i, j, l, r in primal:
        fraction = (r-l)/(qa[i+1]-qa[i])
        for s in range(3):
            ideal_m[j][s] += fraction*bits(ca[i]["m"][s])
        ideal_nonref[j] += fraction*source_nonref[i]
    for j, c in enumerate(cb):
        for s in range(3):
            scalar_close(bits(c["m"][s]), ideal_m[j][s], 64, ideal_m[j][s], "Independent positive primal species packet")
    da, db = corners(qa), corners(qb)
    intersection = overlaps(da, db)
    need([(r["donor"], r["target"], bits(r["length"])) for r in report["dual"]] == [(i, j, r-l) for i, j, l, r in intersection], "Independent virtual-corner overlap topology")
    packets = [[F(0), F(0), F(0)] for _ in range(2*len(cb))]
    error = [[F(0), F(0), F(0)] for _ in packets]
    observed = [[F(0), F(0), F(0)] for _ in packets]
    for (i, j, l, r), row in zip(intersection, report["dual"]):
        cell, side = divmod(i, 2)
        node = before["nodes"][cell+side]
        mass = bits(ca[cell]["M"])/2
        velocity = bits(node["u"])
        original = [mass, mass*velocity, mass*velocity**2/2]
        if i in (0, 2*len(ca)-1):
            original = [bits(node["I"]), bits(node["J"]), bits(node["K"])]
        fraction = (r-l)/(da[i+1]-da[i])
        for k in range(3):
            exact = original[k]*fraction
            actual = bits(row["packet"][k])
            scalar_close(actual, exact, 64, abs(exact), "Independent positive dual share")
            packets[j][k] += exact
            observed[j][k] += actual
            error[j][k] += abs(actual-exact)
    balanced = []
    rebalance_total = F(0)
    for j, cell in enumerate(cb):
        r = report["cells"][j]
        left, right = packets[2*j][:], packets[2*j+1][:]
        actual_raw = [[bits(x) for x in r[key]] for key in ("rawL", "rawR")]
        for side in range(2):
            for k in range(3):
                scalar_close(actual_raw[side][k], observed[2*j+side][k], 4, abs(observed[2*j+side][k]), "Actual raw packet gather")
        mass = bits(cell["M"])
        raw_mass = sum(p[0] for p in actual_raw)
        raw_error = raw_mass-mass
        need(abs(raw_error) <= bits(r["mass_bound"]), "Normalization exceeds declared arithmetic budget")
        independent_bound = sum(error[2*j+side][0] for side in range(2))+64*EPS*(mass+raw_mass)
        need(abs(raw_error) <= independent_bound, "Normalization exceeds independent local forward mass bound")
        scalar_close(bits(r["lambda"]), mass/raw_mass, 2, abs(mass/raw_mass), "Quadrature-only lambda arithmetic")
        for k, name in enumerate(("deltaM", "deltaJ", "deltaH")):
            measured = sum(bits(r[key][k]) for key in ("normL", "normR"))-sum(actual_raw[side][k] for side in range(2))
            scalar_close(bits(r[name]), measured, 4, sum(abs(bits(r[key][k]))+abs(actual_raw[side][k]) for side, key in enumerate(("normL", "normR"))), "Signed normalization delta")
        # Exact ideal packets may have an O(eps) source scalar mismatch versus
        # primal. Normalize that measured mismatch only, not material rho'.
        total = left[0]+right[0]
        factor = mass/total
        left = [x*factor for x in left]
        right = [x*factor for x in right]
        left, right, transfer = balance_half(left, right, mass)
        rebalance_total += transfer
        for side, key in enumerate(("L", "R")):
            ideal = left if side == 0 else right
            for k in range(3):
                scale = abs(left[k])+abs(right[k])+sum(abs(x[k]) for x in actual_raw)
                scalar_close(bits(r[key][k]), ideal[k], 256, scale, "Independent actual half-mass J/H packet")
        scalar_close(bits(r["rebalanceM"]), transfer, 256, mass, "Real material rebalance distinct from lambda")
        need(bits(r["Qarith"]) == -bits(r["deltaH"]), "Signed normalization energy is not minus actual deltaH")
        balanced += [left, right]
    need(rebalance_total > 0, "Independent selected event lacks variable-rho rebalance")
    node_q = []
    for j, n in enumerate(after["nodes"]):
        incident = ([] if j == 0 else [balanced[2*j-1]])+([] if j == len(cb) else [balanced[2*j]])
        inertia, momentum, h = [sum((x[k] for x in incident), F(0)) for k in range(3)]
        expected_q = h-momentum**2/(2*inertia)
        if j in (0, len(cb)):
            expected_q = F(0)
        need(expected_q >= -256*EPS*abs(h), "Exact compatible nodal heat is materially negative")
        # This is an error check, not a heat clip: preserve the signed exact Q.
        node_q.append(expected_q)
        scalar_close(bits(n["I"]), inertia, 256, inertia, "Independent target nodal inertia")
        scalar_close(bits(n["J"]), momentum, 256, sum(abs(x[1]) for x in incident), "Independent target nodal momentum")
        scalar_close(bits(report["nodes"][j]["Q"]), expected_q, 512, abs(h), "Independent compatible nodal mixing heat")
    for i, c in enumerate(cb):
        half = bits(c["M"])/2
        left_i = bits(after["nodes"][i]["I"])
        right_i = bits(after["nodes"][i+1]["I"])
        q = node_q[i]*half/left_i+node_q[i+1]*half/right_i
        scalar_close(bits(report["cells"][i]["Q"]), q, 1024, sum(abs(x[2]) for x in balanced[max(0, 2*i-1):min(len(balanced), 2*i+3)]), "Independent compatible cap/interior heat allocation")
        formation = sum((bits(c["m"][s])*bits(profiles[s]["u0"]) for s in range(3)), F(0))
        actual_nonref = bits(c["U"])-formation
        # Scalar storage error uses local thermal energy, never formation energy
        # or another species as a tolerance floor.
        scalar_close(actual_nonref, ideal_nonref[i]+q+bits(report["cells"][i]["Qarith"]), 128, abs(ideal_nonref[i])+abs(q), "Actual nonreference U from independent primal overlap and compatible heat")
    return {"primal_records": len(primal), "dual_records": len(intersection), "material_rebalance": rebalance_total,
            "exact_signed_heat": sum(node_q), "deltaM": sum(bits(c["deltaM"]) for c in report["cells"]),
            "deltaJ": sum(bits(c["deltaJ"]) for c in report["cells"]), "deltaH": sum(bits(c["deltaH"]) for c in report["cells"])}


def serial(value):
    if isinstance(value, F):
        return {"fraction": str(value), "approximate": float(value)}
    if isinstance(value, Bound):
        return {"lower": serial(value.lo), "upper": serial(value.hi)}
    if isinstance(value, dict):
        return {k: serial(v) for k, v in value.items()}
    if isinstance(value, (list, tuple)):
        return [serial(v) for v in value]
    return value


def write(path, value):
    path.write_text(json.dumps(serial(value), indent=2, sort_keys=True)+"\n", encoding="utf-8")


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def key(config):
    return tuple(config[x] for x in ("N", "d", "phase", "carrier", "reverse", "reflected", "uniform"))


def expected_keys():
    configurations = ((16, 4), (32, 4), (64, 4), (32, 2), (32, 8), (32, 16), (32, 64), (32, 256), (64, 16))
    result = {(n, d, p, a, rev, ref, False) for n, d in configurations for p in range(4) for a in (-1, 1) for rev in (False, True) for ref in (False, True)}
    result |= {(32, 256, p, a, False, ref, True) for p in range(4) for a in (-1, 1) for ref in (False, True)}
    need(len(result) == 304, "Oracle matrix definition is incomplete")
    return result


def event_transition(old_count, old_ids, new_count, new_ids):
    """Count/ID-set event, separate from a nonidentity geometric rezone."""
    event = old_count != new_count or old_ids != new_ids
    return event, event and old_count == new_count


def authenticate_event_counters(previous, old_count, old_ids, new_count, new_ids,
                                event_flag, equal_flag, cumulative, cumulative_equal):
    event, equal = event_transition(old_count, old_ids, new_count, new_ids)
    need(event_flag is event and equal_flag is equal,
         "Event detector missed count or equal-count ID change")
    expected = previous[0]+int(event), previous[1]+int(equal)
    need(type(cumulative) is int and type(cumulative_equal) is int and
         (cumulative, cumulative_equal) == expected,
         "Cumulative event/equal-count counters disagree with actual call history",
         {"previous": previous, "expected": expected, "reported": (cumulative, cumulative_equal)})
    return expected


def first_event_categories(first, step, tau, reverse, old_count, old_ids, new_count, new_ids):
    """Independently identify exports that exist; absent categories stay absent."""
    event, equal = event_transition(old_count, old_ids, new_count, new_ids)
    present = (new_count > old_count, new_count < old_count, equal,
               reverse and tau > TIME/2 and event)
    result = dict(first)
    for category, occurred in enumerate(present):
        if occurred and category not in result:
            result[category] = step
    return result


def compare_decay(fine, coarse, factor, message, context):
    need(fine.hi < factor*coarse.lo, message,
         {"context": context, "fine": fine, "coarse": coarse, "factor": factor})


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    need(sys.version_info >= (3, 11), "Python 3.11+ required")
    root, output = args.input.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    files = sorted(path for path in root.rglob("*") if path.is_file())
    before = [{"file": str(p.relative_to(root)), "sha256": sha(p), "bytes": p.stat().st_size} for p in files]
    write(output/"input-manifest.json", before)
    summary = {"schema": 1, "native_cases": 0, "oracle_passed": False, "whole_A2": False, "all_node_rate": False, "pressure_front": False}
    current = None
    try:
        need(not (root/"FIRST_FAILURE.json").exists(), "Native first numerical failure already exists")
        complete = json.loads((root/"COMPLETE.json").read_text(encoding="utf-8"))
        need(complete == {"schema": 1, "cases": 304, "nonlinear": 288, "uniform": 16, "native_physical_checks": True, "independent_oracle_required": True, "whole_A2": False}, "Missing/extraneous native completion metadata")
        records = {}
        aggregate_categories = set()
        for directory in sorted(p for p in root.iterdir() if p.is_dir()):
            samples = [json.loads((directory/f"sample-{i}.json").read_text(encoding="utf-8")) for i in range(5)]
            current = {"case": directory.name, "sample_file": str(directory/"sample-0.json"),
                       "files": [str(directory/f"sample-{i}.json") for i in range(5)]}
            config = samples[0]["config"]
            k = key(config)
            need(k in expected_keys() and k not in records, "Foreign/duplicate nonlinear history", current)
            initial = samples[0]["state"]
            verify_state(initial, config, F(0))
            q, _, _, cells = state_arrays(initial, config["reflected"])
            sampling = []
            for i, cell in enumerate(cells):
                for s in range(3):
                    exact = integrate(SPECIES[s], q[i], q[i+1])
                    need(bits(cell["m"][s]) == rounded(exact), "Initial species primitive not independently rounded once", {**current, "cell": i, "species": s})
                    sampling.append(bits(cell["m"][s])-exact)
            rows = []
            for number, sample in enumerate(samples):
                current = {**current, "sample": number, "sample_file": str(directory/f"sample-{number}.json")}
                need(sample["config"] == config and bits(sample["tau"]) == number*TIME/4, "Wrong sample history/time")
                verify_state(sample["state"], config, number*TIME/4, initial if number else None)
                norms, profile = error_norms(sample["state"], config)
                history = sample["history"]
                if number:
                    norms["heat_rate"] = heat_rate_norm(history, config["uniform"])
                    for side, label in enumerate(("left", "right")):
                        norms[label+"_TV_rate"] = Bound.exact(bits(history["TV"][side])/TIME)
                    if config["uniform"]:
                        for node in sample["state"]["nodes"]:
                            scalar_close(bits(node["u"]), F(-1 if config["reflected"] else 1), 128, F(1), "Uniform control fixed velocity bound")
                rows.append({"norms": norms, "profile": profile, "state": sample["state"], "history": history})
            # Fresh per-call ledger coverage, event labels and actual cumulative
            # totals are checked independently from the summary/profile reports.
            steps = config["N"]*config["d"]//8
            ledger = [json.loads(line) for line in (directory/"steps.jsonl").read_text(encoding="utf-8").splitlines()]
            need(len(ledger) == steps and [r["step"] for r in ledger] == list(range(1, steps+1)), "Missing/reordered carrier steps")
            initial_m, initial_p, initial_k, initial_u, initial_thermal = actual_totals(initial)
            initial_s = sum((bits(c["S"]) for c in initial["cells"]), F(0))
            initial_s_scale = sum((abs(bits(c["S"]))+bits(c["cv"]) for c in initial["cells"]), F(0))
            old_ids = expected_geometry(config, F(0))[1]
            old_count = len(initial["cells"])
            event_counters = (0, 0)
            sample_event_counters = {0: event_counters}
            first_categories = {}
            old_k, energy_ledger, volume_ledger = initial_k, F(0), F(0)
            initial_inner = [bits(initial["nodes"][j]["u"]) for j in (1, -2)]
            old_inner = initial_inner[:]
            tv = [F(0), F(0)]
            drift = [F(0), F(0)]
            event_drift = [F(0), F(0)]
            event_tv = [F(0), F(0)]
            other_drift = [F(0), F(0)]
            other_tv = [F(0), F(0)]
            wrong = [F(0), F(0)]
            arithmetic = [F(0), F(0)]
            initial_range = max(bits(n["u"]) for n in initial["nodes"])-min(bits(n["u"]) for n in initial["nodes"])
            for step in ledger:
                tau = F(step["step"], config["N"]*config["d"])
                geom, ids, origin = expected_geometry(config, tau)
                need(bits(step["tau"]) == tau and bits(step["phi"]) == origin and step["old_ids"] == old_ids and step["new_ids"] == ids, "Wrong per-call geometry/ID evidence")
                need(step["old_cells"] == old_count and step["new_cells"] == len(geom)-1, "Actual per-call cell counts disagree with independent geometry")
                event, _ = event_transition(old_count, old_ids, len(geom)-1, ids)
                event_counters = authenticate_event_counters(event_counters, old_count, old_ids, len(geom)-1, ids,
                    step["event"], step["equal"], step["history"]["events"], step["history"]["equal_events"])
                first_categories = first_event_categories(first_categories, step["step"], tau, config["reverse"],
                    old_count, old_ids, len(geom)-1, ids)
                if step["step"] % (steps//4) == 0:
                    sample_event_counters[step["step"]//(steps//4)] = event_counters
                for s in range(3):
                    scalar_close(bits(step["m"][s]), initial_m[s], 128, initial_m[s], "Per-call cumulative own-species trace balance")
                scalar_close(bits(step["P"]), initial_p, 128, abs(initial_p), "Per-call cumulative actual momentum")
                scalar_close(bits(step["U"])+bits(step["K"]), initial_u+initial_k, 256, initial_thermal+bits(step["thermal"])+initial_k+bits(step["K"]), "Per-call cumulative actual total energy")
                need(bits(step["S"])-initial_s >= -128*EPS*(initial_s_scale+bits(step["entropy_scale"])), "Per-call actual entropy decrease")
                scalar_close(bits(step["deltaE"]), bits(step["Eres"]), 8, old_k+bits(step["K"]), "Actual per-call physical error/report reconciliation")
                for field, residual, prior in (("energy_ledger", "Eres", energy_ledger), ("geometry_ledger", "Vres", volume_ledger)):
                    actual = bits(step[field])
                    increment = bits(step[residual])
                    scalar_close(actual-prior, increment, 2, abs(actual)+abs(prior)+abs(increment), "Independent signed "+field+" addition")
                requested = bits(step["history"]["Q"])+bits(step["history"]["Qarith"])
                need(abs(requested-(initial_k-bits(step["K"]))) <= bits(step["history"]["Kbound"]), "Independent per-call cumulative heat/canonical K loss")
                available = initial_k-initial_p**2/(2*sum(initial_m))
                need(bits(step["history"]["Q"]) <= available+abs(bits(step["history"]["Qarith"]))+bits(step["history"]["Kbound"]), "Independent available mechanical variance exceeded")
                need(bits(step["history"]["rebalance"]) > 0, "No measured variable-rho material rebalance")
                for side in (0, 1):
                    new_u, prior_u, du, budget = [bits(step[name][side]) for name in ("inner_u", "inner_old", "inner_du", "inner_arith")]
                    need(prior_u == old_inner[side] and du == rounded(new_u-prior_u), "Inner actual jump history is not consecutive canonical velocity")
                    independent_local = 64*EPS*(2*abs(new_u)+abs(prior_u))
                    scalar_close(budget, independent_local, 4, independent_local, "Independent local packet/division velocity arithmetic")
                    arithmetic[side] += budget
                    change = new_u-prior_u
                    tv[side] += abs(change)
                    drift[side] += change
                    (event_drift if event else other_drift)[side] += change
                    (event_tv if event else other_tv)[side] += abs(change)
                    wrong[side] += max(F(0), -change if side == 0 else change)
                    for name, exact in (("TV", tv[side]), ("drift", drift[side]), ("event_drift", event_drift[side]), ("other_drift", other_drift[side]), ("event_TV", event_tv[side]), ("other_TV", other_tv[side]), ("wrong", wrong[side]), ("velocity_bound", arithmetic[side])):
                        scalar_close(bits(step["history"][name][side]), exact, 4, abs(exact), "Independent actual signed/absolute event history: "+name)
                    need(tv[side] <= initial_range+arithmetic[side] and wrong[side] <= arithmetic[side], "Inner physical variation exceeds predeclared range/arithmetic")
                    if config["uniform"]:
                        scalar_close(new_u, F(-1 if config["reflected"] else 1), 128, F(1), "Per-call uniform inner fixed velocity bound")
                    old_inner[side] = new_u
                old_ids, old_count = ids, len(geom)-1
                old_k, energy_ledger, volume_ledger = bits(step["K"]), bits(step["energy_ledger"]), bits(step["geometry_ledger"])
            need(event_counters[0] > 0, "No genuine repeated topology/ID events over completed history")
            for number, sample in enumerate(samples):
                need((sample["history"]["events"], sample["history"]["equal_events"]) == sample_event_counters[number],
                     "Sample cumulative event counters disagree with actual call prefix")
            packet_results = {}
            event_files = {file.name: file for file in directory.glob("event-*.json")}
            need(set(event_files) == {f"event-{category}.json" for category in first_categories},
                 "Selected event exports do not match actual first-occurrence categories",
                 {"expected_first_steps": first_categories, "actual_files": sorted(event_files)})
            for category, first_step in sorted(first_categories.items()):
                file = event_files[f"event-{category}.json"]
                event = json.loads(file.read_text(encoding="utf-8"))
                current = {"case": directory.name, "event_file": str(file)}
                need(event["config"] == config and event["step"] == first_step and
                     bits(event["tau"]) == F(first_step, config["N"]*config["d"]),
                     "Selected packet is not the category's first actual call")
                need(event["before"]["identity"][:2] == [initial["identity"][0], first_step-1] and
                     event["after"]["identity"][:2] == [initial["identity"][0], first_step] and
                     [bits(n["q"]) for n in event["before"]["nodes"]] == expected_geometry(config, F(first_step-1, config["N"]*config["d"]))[0] and
                     [bits(n["q"]) for n in event["after"]["nodes"]] == expected_geometry(config, F(first_step, config["N"]*config["d"]))[0],
                     "Selected first-occurrence packet belongs to a different actual geometry/revision")
                packet_results[file.name] = packet_oracle(event)
            aggregate_categories.update(first_categories)
            records[k] = {"config": config, "rows": rows, "directory": str(directory)}
            write(output/(directory.name+".json"), {"config": config, "sampling_roundoff": sampling,
                  "initial_quadrature_P_error": initial_p-(-1 if config["reflected"] else 1)*integrate(mul(RHO, (F(1),) if config["uniform"] else U)),
                  "initial_quadrature_K_error": initial_k-integrate(mul(RHO, mul((F(1),) if config["uniform"] else U, (F(1),) if config["uniform"] else U)))/2,
                  "samples": [{"norms": row["norms"], "dual_profile": row["profile"], "history": row["history"]} for row in rows], "packets": packet_results})
            summary["native_cases"] += 1
        need(set(records) == expected_keys(), "Incomplete 288+16 oracle matrix")
        need({0, 1, 2} <= aggregate_categories, "Complete matrix does not cover insertion/retirement/equal-count categories")
        strong = ["mass0", "mass1", "mass2", "rho", "node", "PL", "inf", "left_window", "right_window", "left_bias", "right_bias", "left_TV_rate", "right_TV_rate"]
        weak = ["heat_rate", "dual", "dualmass"]
        comparisons = []
        for reflected in (False, True):
            for reverse in (False, True):
                for carrier in (-1, 1):
                    for phase in range(4):
                        family = (phase, carrier, reverse, reflected)
                        lookup = lambda n, d: records[(n, d, phase, carrier, reverse, reflected, False)]
                        for sample in range(1, 5):
                            for name, path in (("spatial", ((16, 4), (32, 4), (64, 4))), ("joint", ((16, 4), (32, 8), (64, 16)))):
                                for (na, da), (nb, db) in zip(path, path[1:]):
                                    a, b = lookup(na, da), lookup(nb, db)
                                    context = {"family": family, "sample": sample, "path": name, "coarse": a["directory"], "fine": b["directory"],
                                               "coarse_sample_file": str(Path(a["directory"])/f"sample-{sample}.json"),
                                               "fine_sample_file": str(Path(b["directory"])/f"sample-{sample}.json")}
                                    current = context
                                    for quantity in strong+weak:
                                        compare_decay(b["rows"][sample]["norms"][quantity], a["rows"][sample]["norms"][quantity], F(4, 5) if quantity in strong else F(1), "Predeclared nonlinear "+name+" error does not decay: "+quantity, context)
                                    comparisons.append(context)
                            previous = None
                            ds = (2, 4, 8, 16, 64, 256)
                            for da, db in zip(ds, ds[1:]):
                                a, b = lookup(32, da)["rows"][sample], lookup(32, db)["rows"][sample]
                                qa, ua, _, ca = state_arrays(a["state"], reflected)
                                qb, ub, _, cb = state_arrays(b["state"], reflected)
                                pl, sup = pl_union((qa, ua), (qb, ub))
                                difference = {"PL_union": pl, "sup_union": sup}
                                for s in range(4):
                                    va = [(bits(c["M"]) if s == 0 else bits(c["m"][s-1]))/(qa[i+1]-qa[i]) for i, c in enumerate(ca)]
                                    vb = [(bits(c["M"]) if s == 0 else bits(c["m"][s-1]))/(qb[i+1]-qb[i]) for i, c in enumerate(cb)]
                                    difference["cell_union"+str(s)] = cell_union((qa, va), (qb, vb))
                                difference["heat_rate_change"] = Bound.exact(abs(bits(a["history"]["Q"])-bits(b["history"]["Q"]))/TIME)
                                for side in (0, 1):
                                    difference["signed_bias"+str(side)] = Bound.exact(abs(bits(a["history"]["drift"][side])-bits(b["history"]["drift"][side])))
                                    difference["TV_rate"+str(side)] = Bound.exact(abs(bits(a["history"]["TV"][side])-bits(b["history"]["TV"][side]))/TIME)
                                context = {"family": family, "sample": sample, "d_coarse": da, "d_fine": db,
                                           "coarse_sample_file": str(Path(lookup(32, da)["directory"])/f"sample-{sample}.json"),
                                           "fine_sample_file": str(Path(lookup(32, db)["directory"])/f"sample-{sample}.json")}
                                current = context
                                if previous is not None:
                                    for quantity in difference:
                                        compare_decay(difference[quantity], previous[quantity], F(1), "Predeclared fixed-grid differences do not settle: "+quantity, context)
                                comparisons.append({**context, "differences": difference})
                                previous = difference
        rollback = json.loads((root/"rollback.json").read_text(encoding="utf-8"))
        need(rollback["stale_rejected"] is True and rollback["eight_cell_insertion_rejected"] is True and rollback["accepted"]["identity"][1] == 1 and len(rollback["accepted"]["cells"]) == 7, "Missing genuine stale/capacity rollback fixture")
        after = [{"file": str(p.relative_to(root)), "sha256": sha(p), "bytes": p.stat().st_size} for p in files]
        need(before == after, "Native dataset changed during independent oracle")
        write(output/"comparisons.json", comparisons)
        summary["oracle_passed"] = True
    except (GateFailure, OSError, ValueError, KeyError, TypeError) as error:
        summary["failure"] = str(error)
        # Copy complete actual before/plan/after/packets for the first failed
        # comparison, rather than retaining only paths or rounded norm values.
        attachments = {}
        if isinstance(current, dict):
            for name, value in current.items():
                if name.endswith("_file") and isinstance(value, str) and Path(value).is_file():
                    attachments[name] = json.loads(Path(value).read_text(encoding="utf-8"))
        write(output/"FIRST_FAILURE.json", {"error": str(error), "context": current,
              "evidence": error.evidence if isinstance(error, GateFailure) else None,
              "complete_actual_states_and_packets": attachments,
              "native_input": str(root), "source": str(Path(__file__).resolve()),
              "source_sha256": sha(Path(__file__))})
    finally:
        write(output/"summary.json", summary)
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0 if summary["oracle_passed"] else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (GateFailure, OSError) as error:
        print("Independent nonlinear oracle FAILED: "+str(error), file=sys.stderr)
        sys.exit(1)
