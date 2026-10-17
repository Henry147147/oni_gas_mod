# Heat exchange in occupied gas/liquid cells

The native MAC owner now has a conservative conduction operator for cells that
contain both gas and liquid. The component and native development scene now
pass a real Linux Unity source/save and two exact reload sessions. WindowsPlayer
controls are still running. Scenario 4 retains its
saved conduction-disabled setting.

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
passive quantity and canonical face momentum is retained. Entropy uses the
fixed-volume identity `dS=dU/T` and integrates the two represented heat paths
jointly through the full EOS. This includes both caloric and gas headspace
changes without subtracting two nearly equal temperature-derived entropies. This supplies no
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
arithmetic. The reviewer also solves each saved conserved state independently at high
precision before reconstructing entropy, including the exchange of only a few
stored-energy quanta. All three captures pass 312 independent checks across
five cases and reject eight alterations: stored U, temperature, liquid volume,
equilibrium capacity, trace mass, conductance, entropy and a 0.1% change in the
weak entropy signal. The original 128-epsilon energy and 2e-10 volume
bounds remain explicit. No native code is loaded by the reviewer.

The earlier `0f1b48e` reporting method fails a new weak-heat regression:
subtracting rounded EOS temperatures erases the small physical entropy signal.
Its actual failing executable and output remain retained as a negative control.
The corrected joint integral passes on all three builds. Every original saved
physical-state row remains byte-for-byte unchanged; this repairs the reported
entropy rather than altering heat transfer or widening its energy allowance.

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

## Native heated-mixture scene

Scenario 5 uses the same six occupied gas/water cells as scenario 4, with
complete-EOS conduction enabled. The checkpoint stores the enabled flag and
scenario separately: restoring scenario 4 still requires conduction disabled,
and restoring scenario 5 requires it enabled. The existing ABI 2 configuration
remains 40 bytes; older adapters reject the unknown scenario.

`native-mixed-heat-domain` submits two actual game energy messages after
ownership: +2 MJ heating and -0.5 MJ cooling. It requires exactly two committed
commands, no original native fluid dispatch, the 1.5 MJ net energy account at
the existing 128-epsilon operand bound, a warmer source and neighbor, and
positive completed conduction/entropy progress from the simulation worker.
Readings and saves retain actual EOS phase volumes. The portable reviewer
requires the declared nonlinear model and completed worker counters; restored
checks retain that operator without inventing new source heat commands.

The matrix scene is `mixed-heat`; Windows PowerShell accepts
`-NativeDomain mixed-heat`. Both native worker scenarios now pass gas/liquid
callbacks, complete energy/trace accounts and exact restored continuation in
the Linux component control. Both adapters build, the Linux declaration suite
passes 148 groups and 234 Python checks pass (four optional/platform skips).
All 16 backend groups now pass on GNU Linux, GNU Windows binaries under Wine,
Linux ASan/UBSan and the clean Ubuntu compatibility build. Actual Unity
verification remains in progress. This scene
provides no free surfaces, phase changes or liquid world geometry.

## First native Linux source and reloads

Frozen `4db6e00` with native v16 passes an actual nine-world LinuxPlayer source
through frame 121. The two real energy messages commit +1.5 MJ net; the measured
U+K residual is -0.000568389892578125 J within the unchanged
0.0012169780096353763 J operand bound. The worker completes 5,082 conductive
stages, 35,574 contacts and 70,182 nonlinear iterations, with 6.844464302062988 J
of represented redistribution and positive entropy. Unresolved sub-ULP updates
remain reported separately.

The complete source file SHA-256 is
`268806453fc21f246b6b18a7ac7a20126bda842c07f4b8b0bc6faac18d276874`;
its physical image is
`55db5cf0a5b860ac19647b9248e725fd8c028928a4eeb5b825e0b1fa7947629d`.
Both native Linux load sessions preserve that initial image, finish paused
frame-zero saves/readbacks, and compare all 120 complete physical frames
individually. Native references/readers, calibrated GPU texels and input-file
preservation pass. Their final physical image is
`85552853053e1410dcbcf11ee2fc7991b0d9a8acd3f34cc399368136f8917c73`.

The actual source PNG was viewed with cell 103106/revision 121 hover:
300.32865395671405 K, 100445.08909295494 Pa and actual liquid volume
20.58571183123733%. Both bulk gas rows, trace notice and breathing status are
visible. This v16 capture retains the earlier gas-only world colour;
the new phase-volume appearance and liquid interface geometry are not claimed
from it. WindowsPlayer and bidirectional cross-loads remain separate gates.
