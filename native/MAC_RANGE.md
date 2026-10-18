# Range arithmetic

[`MacClosedWorld`](MAC_SOLVER.md) never clips a trace or drops a small transfer, so a
positive quantity that cannot be represented must either be carried in a wider form
or reject the step. The strong-flow benchmark's helium trace (mass ratio 1e-28) and
passive (1e-30) exposed this repeatedly on Linux: a 9.12e-436 J kinetic-energy parcel,
an 8.59e-325 kg helium parcel, a 3.6e-585 kg m/s momentum share, a new open-face J of
the same size, and finally a scaled share needing exponent −3049816383. Each was a
correct rejection under the old binary64 representation; the types below remove them.

Primary cell storage (constituent masses, U, passive amounts) stays binary64.
Derived energies, parcels, momentum contributions, canonical J and fluxes carry full
range behind explicit binary64 projections.

Code: `mac_energy.hpp`/`.cpp`, `mac_exponent.hpp`/`.cpp`, `mac_scaled.hpp`,
`mac_fields.hpp`.

## MacEnergy and MacScaled

`MacEnergy` stores a normalized binary64 significand and an independent
`MacExponent`. `MacScaled` (with `MacScaledSum` and `MacScaledAccount`) is the same
type under a unit-neutral name for momentum, fluxes, volumes and parcel amounts.
Kinetic, variance, share and product operations work on normalized factors; their
significands still round like binary64 multiplication and division, so this is
extended range, not extended precision. Square roots act on the significand and an
exactly halved exponent.

`project()` returns the nearest binary64 value and flags subnormal or below-range
rounding; `require_representable()` rejects any loss. Overflow and exponent or
storage exhaustion are typed numerical-precision failures.

## Additive expansions and accounts

`MacEnergySum` keeps a nonoverlapping additive expansion whose components have their
own exponents. Two-sum runs on aligned normal significands when the exponent gap is at
most 54; larger gaps keep both operands. Every represented operand is preserved
through cancellation. Four ordinary components are stored inline and at most 4,096 in total.
`value()` is only a rounded estimate of the exact expansion.

The common accumulator uses normal binary64 components directly. It prepares an
error-free two-sum candidate, then promotes the original expansion to independent
exponents if an operand/result is subnormal, a sum overflows, or more than four
components are needed. Promotion retries the complete operand; a failed candidate
cannot discard visited low terms. Range extension and large integer exponents use
the existing addition algorithm. This changes storage and arithmetic cost, with no
trace threshold, altered precision, changed summation order or persisted selector.
The Linux x86-64 object is 72 bytes instead of 128. A promoted expansion allocates
its component vector; ordinary expansions through four components allocate none.

`MacEnergyAccount` holds the signed sum and the sum of absolute operands. Its
`within_bound()` compares the residual with exactly 2^-45 (128 binary64 epsilons)
times the absolute sum, comparing expansions, with no unit floor and no projected
zero. This is the account rule used throughout the solver. Error-free expansions
follow [Shewchuk](https://www.cs.cmu.edu/~quake/robust.html); the independent-exponent
adaptation is this implementation's.

`require_mac_energy_environment()` is a read-only entry guard: x86-64 binary64,
nearest rounding, FTZ/DAZ off and masked SSE exceptions. It changes no host setting;
the native domain establishes that environment itself
([floating-point environment](../docs/native-platform-numerics.md#solver-arithmetic-scope)).

## Bounded integer exponents

`MacExponent` stores signed 64-bit values inline and larger magnitudes in immutable,
reference-counted 32-bit limbs; copies share read-only data and results return to
inline storage when possible. Arithmetic is exact addition, subtraction, doubling,
floor halving and comparison. A large exponent gap is compared with 54 before any
shift, so a numerical magnitude is never an allocation size. Limits: 2,048 magnitude
words (65,536 bits) and 20,000 decimal characters excluding sign. Canonical decimal
parsing rejects leading zeros, negative zero and invalid characters; integer narrowing
must be exact. Capacity and narrowing failures leave inputs unchanged. Checkpoints
store exponents as canonical decimal text.

Optimized builds use compiler/linker-checked IPO by default (`MA_ENABLE_IPO`,
disabled for sanitizers); strict floating-point flags remain in force. Inline
64-bit exponent arithmetic falls back to the same limb operations on overflow.
Normal binary64 normalization, projection and aligned two-sum operands use exact
exponent-bit changes. Subnormal normalization/projection keeps the checked math
path. No trace, low additive component or rounding flag is removed. Native
controls compare all finite exponent bins and both signs with `frexp`, alongside
the existing exact-fraction energy and large-exponent reviews.

## Where full-range values are used

### Derived energy

Every face keeps full old, new and transported K, mixing heat and wall heat. The
legacy binary64 report fields are projections with a per-face rounding mask and an
aggregate count; accounts use the full values. Cell heat is added to the U components
before one explicit stored-U projection. A missed update counts as unresolved against
32 epsilons of the requested heat; nonzero heat below binary64 range stays in the
reports and global accounts. Pressure, composed and interval balances keep wide K and
potential terms. Gravity source and internal-work fields remain binary64.

### Primary parcels

When any field of a transfer would round in the subnormal range or become zero, the
whole parcel keeps a normalized prototype and a common reference/actual volume pair;
constituents, signed U and passive fields share that scale. A projectable parcel keeps
the ordinary representation. Donor accounts use the full contributions; targets
accumulate them until one explicit cell-storage projection with an exact rounding
account. Every nonzero result must keep its sign and stay nonzero. A destination trace
that cannot be stored accurately, a poorly resolved donor denormal, or (before full
fluxes) an unrepresentable bulk flux rejects atomically. EOS admission evaluates the
rounded parcel normalized back to the donor volume, and the prototype as well.

### Momentum contributions

Retained and incoming normal momentum contributions are accumulated in one exact
additive expansion before a single projection. Below-range contributions recombine
exactly, including signed cancellation (alternating ±minimum-denormal J gives exactly 0
or −2 minimum denormals).

### Canonical momentum and fluxes

`MacTransportState::full_momentum_x/y`, `MacMassFluxes::full_x/y`, dual fluxes, parcel
volumes and wall reactions hold full values. When a full array is present, the double
array is its projection and must match bit for bit; stale projections or wrong shapes
reject. Kinetic, variance and share overloads take scaled operands.

The pressure root itself receives projected initial velocities. The closed-step
wrapper keeps the raw root inputs and outputs and restores the full initial-velocity
correction to the returned velocity, so full J, midpoint sweeps and K are accounted at
the original bounds. Material volume and U rounding stay explicit, and the EOS and
its root tolerances are unchanged.

Vertical gravity retains full-range body impulse, midpoint body work, actual-mass
potential flux and signed internal work through the stage accounts. Their binary64
report fields are projections; full operands and a separate gravity projection count
remain available. A full vertical momentum, volume sweep or mass flux no longer
rejects solely because gravity is enabled. Ordinary represented force cancellation
still precedes multiplication by the timestep, preserving hydrostatic balance.
The equilibrium certificate checks these same full forces: equality of rounded
subnormal products alone cannot certify rest. A rigid equilibrium can also retain
a below-range body/wall impulse without falling back to ordinary stepping.

Coupled compression mode 3 now retains full incoming motion and midpoint sweeps,
and adds an actual per-cell pressure-work check at the original operand bound.
The full requested work and unresolved update counts remain visible even when the
root's binary64 volume parameter is zero ([coupled pressure](MAC_PRESSURE.md#coupled-compressive-pressure-work-mode-3)).

Limits: full range does not make arbitrarily small velocities change stored U or
volume. Unresolved gravity and coupled-pressure U changes are counted, but their
remainders are not persisted in the physical checkpoint. Completing primary-field
rounding remainder storage remains necessary for production acceptance.

## Verification and reproduction

| Target | Groups | What it covers |
| --- | --- | --- |
| `ma_mac_energy_tests` | 7 | Binary64 roundtrips; derived K/variance/parcel products including the 9.12e-436 J case; cancellation and 32 generated 80-operand expansions; accounts above and below binary64 range; projections and strict narrowing; invalid and overflow cases; five unsupported arithmetic modes (restored afterwards) |
| `ma_mac_compact_sum_tests` | 1 | Component-by-component comparison with the original independent-exponent recurrence after each addition; every binary64 exponent bin; subnormal, overflow, fifth-component and huge-exponent promotion; cancellation, copies/moves and direct nonfinite rejection |
| `ma_mac_energy_transport_tests` | 4 | Gas transport with momenta near 1e-200 and below-range K and heat; the original kinetic-parcel scale; a warm thin-gas (1e-200 kg) pressure reversal through constructor, candidate and atomic owner; a denormal passive parcel that must still reject |
| `ma_mac_primary_parcel_tests` | 5 | The 8.59e-325 kg helium contribution surviving on a uniform grid; a 1e-350 passive per parcel; four quarter-denormal helium contributions forming 2^-1074 kg; unresolved destination rejection; invalid reference volumes |
| `ma_mac_momentum_parcel_tests` | 4 | The 3.6e-585 kg m/s incoming share joining an existing J; minimum-denormal retained/incoming recombination; signed cancellation; the remaining atomic rejection |
| `ma_mac_canonical_range_tests` | 5 | A uniform atmosphere at the rejected J scale through one step and one owner interval; warm thin gas with J below range; a new positive J on a previously zero face; below-range wall reaction and heat; stale projections and invalid full-field shapes |
| `ma_mac_gravity_range_tests` | 5 | Full incoming vertical J with ordinary gravity; below-range body impulse, body work and potential mass flux; minimum-subnormal gravity with a below-range vertical sweep; checkpoint continuation and atomic rollback; 10,000 closed steps retaining all primary masses/passives and the accumulated full impulse |
| `ma_mac_compressive_range_tests` | 4 | Eight full incoming-motion/sweep/work controls in coupled gas/wet/liquid pressure, active stress with a projected incoming velocity, source/endpoint continuation and genuine unrepresentable-work/shared-budget rollback |
| `ma_mac_exponent_tests` | 6 | Signed 32/64-bit boundaries and large integers; shared immutable ownership under three threads; the −3049816383 exponent and cancelling huge exponents; expansions and accounts; 256 successive scaled products; decimal, narrowing and capacity limits |

```sh
cmake --build build --config Release --target ma_mac_energy_tests ma_mac_energy_transport_tests \
  ma_mac_primary_parcel_tests ma_mac_momentum_parcel_tests ma_mac_canonical_range_tests ma_mac_gravity_range_tests ma_mac_exponent_tests
build/ma_mac_exponent_tests > exponent.stdout
python3 native/tests/review_mac_exponent.py exponent.stdout
```

The matching reviewers are `review_mac_energy.py`, `review_mac_energy_transport.py`,
`review_mac_primary_parcel.py`, `review_mac_momentum_parcel.py`,
`review_mac_canonical_range.py`, `review_mac_gravity_range.py` and `review_mac_exponent.py`. They check products,
shares and full additive expansions with exact fractions, reconstruct the transport and
pressure accounts (ideal-gas pressure and temperature at 80 digits), and, for
exponents, use Python integers and symbolic significand/exponent clusters rather than
constructing `2**huge_exponent`. Raw outputs need not be bit-identical across
platforms; both platforms' independent reviews must pass.
