# Conditional fine-cell pressure windows

`mac_kernel_pressure_window.*` is a development component for a fixed-mass
pressure stage. Coarse cells take one macro interval; prescribed fine cells
take `2^level` local intervals. The production owner does not select it. There
is no new checkpoint format, automatic calm classifier or subcycled remap.
The normal-range EOS and linear coefficients are its current scope; the
component does not establish the full-range production numerical contract.

## One physical face path

Each local cell uses the polynomial volume path and constitutive work from
[two-moment pressure integration](MAC_PRESSURE_MOMENTS.md). Its pressure trace
has a constant and a first Legendre coefficient. Fine anchors are rebuilt from
their actual stored primary U and material volume after each local interval;
temperature and energy are never reset from a global ideal isentrope.

Coarse/coarse faces integrate once. Faces touching a fine cell integrate on the
fine time grid using [the shared face primitive](MAC_TIME_MORTAR.md). One
quadratic canonical-momentum path supplies each piece's `Q0` and `Q1`.
Fine cells receive those local moments. Coarse cells receive the same `Q0`
and `P1_coarse(midpoint)*Q0 + (fine_dt/macro_dt)*Q1`. The physical face remains
open across time classes. Signs reverse at its two endpoints.

For a given coarse pressure curve, fine cells stream through their local
nonlinear pressure roots. The outer root solves only for the coarse volume
moments after eliminating that fine history. It resets all candidate U, V and
J to the source for each outer residual evaluation; rejected trials publish
nothing. Actual EOS trials certify the original 16-epsilon local root target.
Accumulated geometric closure must stay within 128 epsilons. The existing
macro-remap outgoing-volume limit also applies. It does not certify transport
along a face whose flow reverses during the window.

The outer linear solve is restarted right-preconditioned FGMRES with a
fixed-source acoustic Schur action. That action streams analytic linear fine
responses through the same grid, rather than forming finite-difference columns
or a monolithic space-time matrix. Fine roots use the coupled complex solve.
An undecidable or unconverged solve fails closed. Coarse/fine pressure masks
are used only for the multigrid preconditioners; all physical face responses
remain in the root. Fixed block reductions preserve worker-independent order.

The coupled multigrid helper now supports one fixed inverse application and
leaving inactive output entries untouched. Its existing solve defaults retain
the preceding version-8 trajectory. Cached topology and coefficients are
derived data. Fine coefficient refresh and local root work use packed fine
lists; initial setup and outer evaluations still allocate/scan full fields.
Reported storage counts retained workspace, excluding transient candidates.

## Bounded component evidence

`ma_mac_kernel_pressure_window_tests` uses a synthetic ideal-gas EOS in a
16x2 periodic grid with a `1e-4` pressure disturbance. The fine patch is eight
cells, with either equal masses or one-hundredth masses. Face inertia is
`(mass_left+mass_right)/2`. References independently integrate the continuous
fixed-mass gas ODE with RK4 at .0001 and .00005 seconds. All windows last .1 s.

| Prescribed schedule | Cell intervals | Pressure error / disturbance | Temperature error, K |
|---|---:|---:|---:|
| Uniform coarse | 32 | 5.55e-11 | 1.60e-15 |
| Mixed 1:4, equal mass | 56 | 7.24e-10 | 2.06e-14 |
| Mixed 1:8, light patch | 88 | 3.50e-7 | 7.96e-10 |
| Uniform fine 1:8, light patch | 256 | 3.69e-7 | 1.05e-9 |

These controls keep the original 0.1 K/1% bounds and a 128-epsilon energy
account. Maximum energy defect is 1.82e-15 against a 4.55e-12 allowance;
reference pressure refinement is at most 1.12e-10 of the disturbance. These are
pressure-only synthetic controls, not the release strong-flow or wave gates.
The mixed 1:8 outer root has 48 unknowns rather than 176 in a monolithic
two-moment history. The 88 cell intervals count physical local intervals,
excluding auxiliary nonlinear and Krylov evaluations.

Additional controls cover walls, gravity, an empty cell, mid-window rollback,
inactive-output preservation, nonfinite inverse
rejection, twelve malformed requests, caloric endpoint rejection, continuation
after rejection, cold/warm
workspace agreement and exact one/twelve-worker fields and iteration counts.
The 33,389-cell threaded control uses 44,461 physical cell intervals and retains
37,789,015 workspace bytes. This is not a whole-process memory measurement or
real-time acceptance run.

`review_mac_kernel_pressure_window.py` launches no native code. It independently
reintegrates the saved scientific inputs, reconstructs shared displacement
from face registers and checks the exact-fraction endpoint energy account.
It passes 3,307 checks and rejects nineteen altered captures on Release,
Debug ASan/UBSan, ThreadSanitizer and MinGW/Wine. The wall/body/
vacuum fixture separately checks energy and both canonical-momentum accounts
in native code; the reader checks its reported account bounds.
All 52 Release CTests pass with the ONI backend and warnings as errors. The
affected coupled-block and both pressure-moment controls also pass the
instrumented and Windows-target checks (targeted threaded checkpoint controls
for the older moment methods under ThreadSanitizer). All 66 preceding MAC
executables retain their physical output and exit status. Existing checkpoint
bytes and continuation are unchanged, including fresh Linux/Wine processes.
Wine supplies Windows-target evidence; actual Windows OS runs remain open.

## Performance remains open

Two repeated `taskset -c 0-11` diagnostic runs use a 500x500 synthetic gas wave
at 100,000 Pa with a 20 Pa disturbance, .1-second windows and twelve requested
workers. All 250,000 cells change within a window. Warm pressure-only cost is
1,767–1,857 ms with all cells coarse, and 10,317–10,327 ms with a prescribed
15,500-cell stripe taking two local intervals. Host load is 19.65–20.76.
This profile resets the source for each sample and excludes remap, interval
audits and adapters. It is investigative data, not an evolving whole-owner
real-time acceptance run. These costs already exceed the 62.5 ms total budget;
serial linearized face work and nested fine inverses need optimization.
Commands, source hashes, loads and captures are in
`local/pressure-windows-v1/accepted-profile-*.json` and the accompanying outputs.

```sh
cmake --build build-production --target ma_mac_kernel_pressure_window_tests
build-production/ma_mac_kernel_pressure_window_tests > CAPTURE
python3 native/tests/review_mac_kernel_pressure_window.py CAPTURE
```

Interval conservation, counted rounding and entropy audits must surround any
future production use. Automatic schedule accuracy, physical local transport,
constituent/partial-pressure validation, resolved liquid/vacuum-front controls, owner
admission, performance and saved selection remain open. No game package or
actual Windows OS evidence is supplied by this component.
