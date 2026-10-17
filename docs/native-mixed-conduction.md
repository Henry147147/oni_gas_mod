# Heat exchange in occupied gas/liquid cells

The native MAC owner now has a conservative conduction operator for cells that
contain both gas and liquid. The component is implemented and tested; a real
Unity heated-mixture source/save/reload is still required. Scenario 4 retains
its saved conduction-disabled setting.

The single-phase path retains the existing constant-capacity graph solve. A
mixed cell uses the complete EOS at fixed constituent masses and occupied
volume: heat changes temperature, pressure, gas headspace, compressed liquid
volume and elastic energy together. Its initial intrinsic or equilibrium heat
capacity cannot be substituted for this nonlinear response.

Each face uses the existing series thermal resistance. Gas/liquid conductivity
is weighted by actual completed partial volumes; gas partial volumes are
proportional to gas mole fractions. Conductance is frozen for one conductive
stage. A closed pair solves

`q = dt * G * (T_hot(U_hot - q) - T_cold(U_cold + q))`.

A bracketed Newton solve evaluates both full EOS endpoints. It respects each
constituent's temperature and compression ranges. If the other cell's initial
temperature exceeds a compression limit, it first locates the admissible
endpoint; a small valid exchange still proceeds. A requested exchange outside
the model rejects the candidate without changing the source. Stiff roots use
a temperature-scaled residual so `dt*G*deltaT` cannot overflow into a false
successful comparison.

One contact uses its full backward-Euler interval. Multiple contacts use
forward/reverse half sweeps in deterministic grid order. This is a first-order
contact-split scheme, rather than a simultaneous nonlinear graph solve.
Refining time approaches the independently integrated variable-conductivity
heat ODE. Walls and disconnected components exchange no heat.

The pair's two stored energies are rounded together using the existing closed
component rule. Their complete balance retains the 128-epsilon operand account,
with no absolute mass or heat floor. Unrepresentable small heat is reported as
unresolved; it is not forced to a minimum flux. Every constituent, trace,
passive quantity and canonical face momentum is retained. Entropy includes
both caloric temperature change and the gas headspace change. This supplies no
phase conversion, latent heat, fluid-solid heat adapter or interface dynamics.

## Component verification

`ma_mac_mixture_thermal_tests` passes on GNU Linux, GNU Windows binaries under
Wine and Linux ASan/UBSan. Controls cover low/high pressure, stiff intervals,
the nonlinear response versus frozen initial capacity, independent RK4 time
refinement, disconnected insulation, traces/passives/momentum, invalid inputs,
material temperature/compression limits and exact complete checkpoint
continuation. A finite `1e300`-second heat interval reaches the equilibrium
limit without accepting an overflowing residual. This is a numerical stress
control, not a game elapsed-time claim.

The saved-only reviewer reconstructs caloric/elastic U, phase volumes,
intrinsic/equilibrium heat capacities, initial conductance, backward-Euler
flux, closed energy and entropy from captured operands using 90-digit Decimal
arithmetic. All three captures pass 184 independent checks and reject seven
alterations: stored U, temperature, liquid volume, equilibrium capacity, trace
mass, conductance and entropy. The original 128-epsilon energy and 2e-10 volume
bounds remain explicit. No native code is loaded by the reviewer.

Run the component and save its output, then review that file:

```sh
cmake --build build-oni-observer --target ma_mac_mixture_thermal_tests
build-oni-observer/ma_mac_mixture_thermal_tests > /tmp/mixture-heat.txt
python3 native/tests/review_mac_mixture_thermal.py /tmp/mixture-heat.txt
```

The target is also registered as `mac_mixture_conduction` in backend CTest
builds. Integration into a heated native development scene and both installed
players remains the next acceptance step. Related contracts:
[gas/liquid ownership](native-mixed-phase-domain.md),
[native backend](native-backend-integration.md) and
[remaining features](completion-plan.md).
