# Hybrid nonlinear passive gate (open, failed)

This test-only gate asks whether the unchanged A1 hybrid remapper (see
[STAGGERED_EXPERIMENTS.md](STAGGERED_EXPERIMENTS.md)) converges on a smooth
nonlinear passive transport problem. It adds no state, algorithm, pressure
adapter, timestep, ABI or game use; passing it would not establish all-node
rates or whole A2.

It has two parts. The native CTest `protected_hybrid_nonlinear_passive`
(`ma_occupied_staggered_hybrid_nonlinear_tests`) checks state, identity and
physical balances and exports the data. The independent oracle
`tools/check_hybrid_nonlinear_oracle.py` makes the refinement decision. A
native exit 0 alone is **not** a pass.

## Current result: native PASS, refinement FAIL

The strict MSVC run completed all 304 native cases (288 nonlinear histories, 16
uniform controls). The oracle passed its 37 calibrations and every per-history
geometry, state, trace, packet, balance, heat and EOS check, then stopped (exit
1) at the first spatial comparison: unreflected, nonuniform, phase 0, carrier
-1, linear motion, C = 1/4, tau = T/4 = 1/32.

| Velocity supremum error | N32 | N64 |
|---|---:|---:|
| Exact | 12565816089467/2^52 | 12124367599963/2^52 |
| Decimal | 0.002790171669146302 | 0.0026921504136996344 |
| At q | 31/32 | 63/64 |

The ratio is about 0.964869 against the strict bound 0.8. Both maxima are at
the right **inner** cap node (confirmed global by exact bracketing); physical
outer endpoints stay bitwise exact. The earlier comparisons passed (ratios about
0.37-0.48); later ones did not run.

**Measured cause.** The first cap-adjacent retirement widens the transition from
h to 7h/4; its right corner overlaps old corners over 3h/8 and 4h/8. With beta
the rebalanced half mass and alpha the fixed cap half mass, compatible gather
gives

```
u_R_new = u_R - [3 beta / (7 (alpha+beta))] (u_R-u_L)
```

which reproduces the recorded velocities to roundoff. N32 has one such inner-cap
velocity loss by the sample time and N64 two (about 0.00142964 and 0.00126251),
so the error does not shrink. The geometric-dual correction (2.23e-5, 5.65e-6)
is far smaller. No pressure acts in this problem.

**Closes when** the oracle completes every comparison below with the criteria
unchanged.

## Problem

q in [0,1], area 1, zero gravity, free ends, 300 K, caps [0,h] and [1-h,1]:

```
rho     = 1 + q/4 + q^2/8
u       = 1 + q/8 + q^2/16 + q^3/32
Y_B     = 1/4 + q/16 + q^2/32
Y_trace = 2^-40 (1 + q/2 + q^2/4)
Y_A     = 1 - Y_B - Y_trace
```

The reflected problem uses rho and Y at 1-q and -u(1-q). Three ideal-gas
surrogates (N2/O2/CO2 atom sets, cp 1040/920/850) supply Cv, molar mass and
offset. Each species mass is integrated exactly and rounded once; no species is
a remainder.

Carrier time T = 1/8 does not advance the physical clock; phase
phi = p*h - a*tau (or p*h - a*min(tau,T-tau) for a reversed cycle), dtau = C*h.
Reflections, a = -1/+1, p = 0, 1/4, 1/2, 3/4 and both motions give 32 families,
each run spatially (N16/32/64 at C 1/4), at fixed grid (N32 at C 1/2 to 1/256)
and jointly ((16,1/4), (32,1/8), (64,1/16)): 288 histories plus 16 uniform-u
controls. States are compared at T/4, T/2, 3T/4 and T. Every call uses the real
planner on the accepted state with no retry.

## Predeclared criteria

- **Balances per call.** Independently summed species mass, momentum and
  U + J^2/(2I) stay within 128 eps of each initial species mass, 128 eps of
  initial sum|J| and 256 eps of thermal plus kinetic energy, with no floors.
  Heat is limited by available mechanical variance plus measured arithmetic.
  Ideal-gas T, p and entropy are recomputed from actual m/U/V.
- **Errors measured.** Per-species and bulk cell-amount error, point-nodal
  velocity L1, rho-weighted piecewise-linear velocity L1 and supremum, cap-window
  L1 on [0,1/8] and [7/8,1], and species/density field L1; profiles include the
  first and last six cells.
- **Refinement.** At every nonzero sample time, successive spatial and joint
  refinements need fine-upper < 0.8 x coarse-lower for each of those errors,
  both inner signed-bias magnitudes and both inner TV/T; heat/T and dual
  diagnostics must strictly decrease. Fixed-N adjacent-C differences must
  strictly decrease. An unresolved enclosure fails.
- **Identity.** Physical outer q/I/J/K/u and cap species, volume and IDs are
  bitwise retained; outer jump, rate and TV are zero. Inner jump/dtau is
  reported, not bounded. Uniform controls keep a fixed 128 eps |u| budget and
  only arithmetic heat.
- **Events.** Variable rho forces a genuine half-mass rebalance carrying the
  same fractions of J and H. At the first insertion/retirement event and the
  first reversed-leg event, the oracle recomputes target mass/J/H, Q and heat
  from the actual old intervals and compares the new inventories.

The oracle treats each exported binary64 as an exact Fraction, certifies
integrals with Bernstein signs (40 dyadic subdivisions), bounds the supremum by
exact branch-and-bound (2^-40 enclosure), and uses predeclared forward-error
envelopes (64-1024 eps by operation) for local packets. An extra seven-cell
fixture checks that a real eight-cell insertion and a stale plan reject with
full state identity. Any unexpected failure is fatal and keeps the complete
states; there is no retry, criterion change, floor, trace drop or heat clip.

## Reproduce

Build with `MA_BUILD_STAGGERED_EXPERIMENTS=ON`, then use the evidence runner
([STAGGERED_EXPERIMENTS.md](STAGGERED_EXPERIMENTS.md#reproduce)) or run both
parts:

```sh
build-experiments/ma_occupied_staggered_hybrid_nonlinear_tests --output nonlinear-data
# prints NONLINEAR_OUTPUT <path>; the dataset is nonlinear-data/run-N
python tools/check_hybrid_nonlinear_oracle.py --input nonlinear-data/run-1 --output nonlinear-oracle
```

Without `--output` the native test writes under `hybrid-nonlinear-profiles/` in
its working directory. The oracle currently exits nonzero with the failure
above. The evidence runner allows 300 s per native and 1800 s per oracle
subprocess.
