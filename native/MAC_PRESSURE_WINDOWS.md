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
An undecidable or unconverged solve fails closed. The macro preconditioner
includes every occupied cell, with zero fine-cell right-hand sides; only its
coarse correction is retained. Fine local inverses use the fine-cell mask.
These auxiliary graphs do not change any physical face response in the root.
Fixed 256-entry reductions preserve worker-independent order and dispatch
their partial sums directly to the pool.

The cached macro graph also applies the analytic two-moment matrix. At a
coarse/fine interface the action subtracts its zero-fine macro response before
adding the streamed fine response. Internal coarse faces stay in the cached
matrix. This avoids re-integrating those faces through the general time-mortar
primitive on every Krylov iteration. Nonlinear residuals still use the original
shared face path and certify the same local root and accumulated closure bounds.

The coupled multigrid helper supports one fixed inverse application, a direct
matrix application and leaving inactive output entries untouched. The direct
application gathers inputs before publishing outputs, so input/output aliases
are supported. Malformed requests throw; unprepared coefficients and nonfinite
results fail without publication. Its existing solve defaults retain the
preceding version-8 trajectory. Cached topology and coefficients are
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
account. Maximum energy defect is 1.74e-15 against a 4.55e-12 allowance;
reference pressure refinement is at most 1.12e-10 of the disturbance. These are
pressure-only synthetic controls, not the release strong-flow or wave gates.
The mixed 1:8 outer root has 48 unknowns rather than 176 in a monolithic
two-moment history. The 88 cell intervals count physical local intervals,
excluding auxiliary nonlinear and Krylov evaluations.

Additional controls cover walls, gravity, an empty cell, mid-window rollback,
inactive-output preservation, nonfinite inverse rejection, twelve malformed
requests, caloric endpoint rejection, continuation after rejection, cold/warm
workspace agreement and exact one/twelve-worker fields and iteration counts.
The coupled real-block oracle independently checks the cached matrix on six
geometries, including 33,389 cells and 2,113 disconnected occupied cells. It
checks input/output aliases, one/twelve-worker agreement, inactive entries,
unprepared or invalid coefficients, malformed spans and overflow without
publication. The 33,389-cell window control uses 44,461 physical cell intervals
and retains 49,528,343 workspace bytes. This is not a whole-process memory
measurement or real-time acceptance run.

`review_mac_kernel_pressure_window.py` launches no native code. It independently
reintegrates the saved scientific inputs, reconstructs shared displacement
from face registers and checks the exact-fraction endpoint energy account.
It passes 3,307 checks and rejects nineteen altered captures on Release,
Debug ASan/UBSan, ThreadSanitizer and MinGW/Wine. The wall/body/
vacuum fixture separately checks energy and both canonical-momentum accounts
in native code; the reader checks its reported account bounds.
All 59 Release CTests pass with the ONI backend and warnings as errors. The
window and coupled-block inverse controls also pass Debug ASan/UBSan,
ThreadSanitizer and MinGW/Wine. Against `3f6c50a`, all 69 preceding MAC
executables retain physical output and exits: 67 captures are byte-identical
and two differ only in timing. The three newer executables also retain outputs
and exits for G1/G2, the existing `--quick` free-surface control and the M5
placeholder; these do not certify the full free-surface/multiphase gates.
The window's original scientific inputs, tolerances and saved review remain
intact. Existing checkpoint bytes and continuation are unchanged; no owner or
saved setting selects this component. The default reference wave SHA remains
`4d250703f98fd1b00af3a7da505aaac1e7a7aa5405f24644e9b299893e8cf763`.
Wine supplies Windows-target evidence; actual Windows OS runs remain open.

## Full-flow integration experiments

An unselected probe surrounds this component with the production interval
conservation/entropy audits, the existing macro remap and physical stages.
For the same 256x2, 20 Pa periodic wave over one second, with transported
constituents and passive quantities, comparison against reference steps of at
most .0005 s gives:

| Pressure schedule | Maximum wave error / 20 Pa | Maximum temperature error | Comparison |
| --- | ---: | ---: | --- |
| Uniform .1 s | 0.5374% | 0.0000919 K | Pass |
| .1 s coarse, first quarter at .05 s | 8.4886% | 0.001452 K | Fail |
| .025 s coarse, first quarter at .0125 s | 0.7697% | 0.000132 K | Pass |

All attempts pass the unchanged interval audits without rejection. Oxygen
partial pressure and every compared positive constituent/passive stay within
1% in these wave runs. The .001 s reference produces the same .1 s mixed-rate
failure (8.4861%). These comparisons do not certify reference resolution or an
automatic scheduling rule.

The first geometric local-remap probe uses one shared swept-volume event at
each face and tracks each cell's intermediate material volume. A uniform
256-piece remap matches the same-rate reference to 2.83e-14 relative helium
error. A mixed-rate remap fails: 267.1% maximum local helium error, despite
bulk pressure/temperature agreement and passing interval audits. A finer
4,096-piece mixed run still fails the 1% constituent gate. The strict positive
constituent comparison and all scene inputs remain intact. Limiter traces
identify background storage/normalization roundoff as one source of altered
trace slopes; no limiter adjustment is selected. Refining two extra stencil
layers instead passes the original front comparison: with 4,096 fine pieces
against an 8,192-step reference, maximum helium error is 0.3296% and maximum
passive error is 0.4442%. The prescribed fine mask grows to 28 of 32 cells,
with 58 of 64 faces on the fine cadence. This small-room control establishes
neither reference resolution nor useful computational savings; broader fronts,
physical stages and automatic halo admission remain open.

Commands, frozen public sources/libraries, host loads and captures are retained
in `local/pressure-window-flow-v1/`, `local/pressure-window-transport-v1/`,
`local/pressure-window-local-remap-v1/` and
`local/pressure-window-remap-trace-v1/` and `local/pressure-window-remap-halo-v1/`. These probes have no saved selection
or owner binding. Full local transport and automatic calm-region admission
remain open.

## Performance remains open

Two repeated `taskset -c 0-11` diagnostic runs use a 500x500 synthetic gas wave
at 100,000 Pa with a 20 Pa disturbance, .1-second windows and twelve requested
workers. All 250,000 cells change within a window. Eight independent
double-double chains in each fixed 256-term Krylov dot block shorten arithmetic
dependencies; the block partition and merge order stay independent of workers.
Repeated, interleaved captures of the compiled component measure warm
pressure-only costs of 813–905 ms with all cells coarse and 1,407–2,031 ms
with a prescribed 15,500-cell stripe taking two local intervals. Paired warm
means are 13–28% and 15–27% lower than the preceding reduction, respectively.
Sampled endpoint host loads are 17.26–21.88.
Retained workspace is 178,741,568 and 220,086,528 bytes respectively, excluding
transient candidates. The mixed case uses 16 outer iterations and 647 fine
linear iterations, against 60 and 1,732 with the preceding coarse-only
preconditioner. The same resolved controls and root bounds apply.

This profile resets the source for each sample and excludes remap, interval
audits and adapters. It is investigative data, not an evolving whole-owner
real-time acceptance run. These costs already exceed the 62.5 ms total budget.
CPU sampling identifies Krylov reductions and multigrid work as substantial
remaining costs. Commands, source hashes, loads, current-native profiles and
the preceding-commit comparison are in `local/pressure-window-operator-v1/`
and `local/pressure-window-dd-integration-v1/`.

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
