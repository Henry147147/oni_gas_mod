# Pressure increments after a stalled occupied-material root

`MacClosedWorld` can now restart an occupied-material pressure root using separate
source pressures and work-pressure changes. This repairs cancellation in an
existing implicit solve. It does not change the default timestep or promote the
old balanced configuration. Strong-flow, vacuum-front and real-time acceptance
remain open.

## Constitutive query and numerical restart

The fused `ReferenceIsentropicWork` query additionally returns an optional
continuous work-pressure change from its source anchor. For the continuous
isentropic law, with work pressure W and endpoint pressure p1,

```text
W' = (p1 - W) / dV
W - p0 = (p1 - p0) - dV W'.
```

Near the anchor, the existing gas/mixed/liquid curve retains `p1-p0` without
subtracting rounded absolute pressures. Its existing divided-difference tangent
supplies W'. Farther away, the direct work difference is well conditioned.
The independently rounded anchor, root and absolute secant retain their original
reference accuracy: reconstruction of W must agree with the ordinary secant
within its existing 32-epsilon pressure bound. An unrepresentable increment or
failed reconstruction check returns an unavailable value. It never substitutes
zero or invalidates an otherwise valid ordinary query. Coupled compression adds
the actual nonnegative bulk stress to this increment using the existing range
arithmetic.

Ordinary roots keep their existing subtraction path. Only a failed line search
or the existing 80-iteration limit requests one restart from the last accepted
volume iterate. That restart has a separate 80-iteration budget. It uses the same
EOS, frozen coefficients, pressure matrix, work tangent, line search, topology
checks and caller's volume tolerance. Missing pressure increments still reject;
cavitation and unresolved geometry remain unsupported.

For each node, compensated addition gathers all four pressure operands before
rounding:

```text
delta_p_node = p0_left - p0_right + dW_left - dW_right.
```

Rounding the two differences separately is insufficient when a stiff liquid
nearly cancels its source pressure gradient. A new coupled-liquid regression
caught that error at the original momentum bound. The complete four-operand sum
passes without changing the bound.

Completed restarted roots publish the source pressures, work-pressure changes
and node differences. Canonical momentum uses those same node differences;
wall reactions gather both pressure components before their common area/time
factor. Body force, full incoming momentum, midpoint volume sweeps and the
existing U+K+G accounts remain active. A restart cannot publish a partial owner.

Restart counts are diagnostics in the pressure and interval reports. The choice
depends on the saved state/settings and numerical convergence, with no wall-clock
input or retained scheduling history. Checkpoint layout and physical selectors
are unchanged; the existing maximum-timestep setting already persists.

## Controls and scope

`ma_mac_pressure_increment_tests` checks 24 gas/mixed/liquid work queries, both
signs, with volume changes down to 2^-70. It also checks an unavailable pressure
increment whose ordinary work remains valid. Periodic 256-cell roots exercise
0.01, 0.025, 0.05 and 0.1 seconds at the original 16-epsilon closure tolerance;
the last three previously failed their line searches. Coupled gas, mixed and
liquid cases exercise the restart with actual U/K and momentum accounts.

The owning gas case advances 0.1 seconds in one substep without retry, restores
its checkpoint, and continues exactly. Fixed-wall cases, including gravity,
exercise pressure reactions and independently sum actual U+K+G at the original
128-epsilon bound. These are standalone component controls.

`review_mac_pressure_increments.py` reconstructs the continuous anchored adiabat
independently at 100 digits, checks every captured pressure/mass/node operand,
actual midpoint geometry, U+K and closed momentum, and rejects four corruptions.
Owner continuation, unavailable-increment, coupled and wall/gravity controls
are native assertions, reported separately from the saved periodic-root review.

The benchmark's inventory totals now use full-range additive expansions for
each constituent and U+K. A positive below-range kinetic term no longer aborts
the final audit by requiring a binary64 projection. Relative-drift estimates
are projected only after subtracting the full totals. A newly introduced
constituent with zero source inventory rejects the benchmark.

**Larger steps still fail the accuracy requirement.** A five-interval, 256-cell
wave capture with the old balanced settings completes without timestep rejection.
It differs from the reference by about 0.015 K but **87% of the 20 Pa disturbance
amplitude**, against the accepted 1% pressure-error requirement. Background-
relative pressure error alone would hide this failure. This configuration is
not promoted. There is no large-colony timing or game acceptance in this slice.

The previously captured vacuum volume-rounding entropy rejection also remains
valid; no bound is enlarged to admit it. Primary-field remainders, interface
geometry and the pressure/remap velocity convention still need completion.

```sh
cmake --build build-production --target ma_mac_pressure_increment_tests
build-production/ma_mac_pressure_increment_tests > increments.stdout
python3 native/tests/review_mac_pressure_increments.py increments.stdout
```

Captured comparisons, private prototypes, rejected test versions and final
verification are under the ignored `local/pressure-increments/` directory.
