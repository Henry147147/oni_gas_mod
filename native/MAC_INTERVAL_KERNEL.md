# Interval-audited kernel

`MacClosedSettings::numerical_kernel = MacNumericalKernel::interval_audited`
selects the first port of the accepted production numerical contract inside
`MacClosedWorld`. The default remains `reference`. This is an intermediate
implementation; neither the strong-flow nor real-time colony gate is complete.

## Implemented path

The first admission is occupied, constant-property ideal-gas mixtures with
material and momentum selectors 0, compression and shock-heat selectors 0,
zero gravity, disabled physical viscosity/conduction, and automatic or Jacobi
preconditioning. These are the existing donor-cell physical laws. Selecting
this kernel never changes a physical selector to obtain a faster result.
Adding a physical setting must extend the admission predicate and checkpoint
schema; an unported enabled process must select the reference path.

The port uses reusable contiguous field columns, two candidate banks, cached
topology and analytic gas responses. Only present species and passive
identities have columns. Their number is unbounded by a development catalogue
limit, but each present column currently spans the grid. Spatial block sparsity
and workspace retention across intervals remain open.

The nonlinear pressure equation uses continuous work-pressure increments and
analytic isentropic secants/tangents. A matrix-free PCG operator solves
each Newton correction, with an automatic [geometric multigrid preconditioner](MAC_MULTIGRID.md)
for coupled large grids and retained explicit Jacobi. Midpoint sweeps define the material volumes; pressure
work uses those actual sweeps. Material quantities use one shared flux per
primal face. Normal momentum uses the original compatible primal-to-dual mass
fluxes, including transverse edges. Positive mixing variance and fixed-wall
kinetic removal heat the adjacent material inventory. U stays primary; it is
never recovered by subtracting kinetic energy from a scalar total energy.

Trials reuse their buffers. A failed trial cannot alter the accepted bank.
CFL/domain rejection halves the timestep under the shared attempt budget. A
failed interval audit discards the entire interval and retries from its original
state with a smaller maximum step. Discarded attempts also consume the budget
when a mid-interval range flag requests the reference implementation. No
candidate is published before the audits.

## Admission, auditing and continuation

A conservative normal-range check flags operands outside `2^-450..2^450`,
uncertain caloric cancellation, unsupported EOS/topology or an unported
selector. A flag restarts the entire interval with the retained reference
implementation. Full canonical fields and subnormal traces take this path.
There is no trace cutoff, mass floor, clipping or renormalization. Rounding in
primary gathers is counted; unresolved primary updates are counted separately.
Discarded third components remain tagged even if later additions cancel the
surviving double-double tail.
The first port does not yet use the permission to round below-range transfers.

Every constituent and passive quantity, both momentum components with wall
impulses, and actual endpoint U+K must reconcile within `128 epsilon` times
their absolute endpoint operands. Normal accounts use double-double sums. An
outward error envelope covers discarded components and the final comparison;
an undecidable comparison replays the original operands into `MacEnergyAccount`.
The reference fallback also checks the opted-in interval accounts, including
gravity and body impulses, rather than relying only on its stage checks.

The gas path computes relative ideal-mixture entropy from actual endpoint
caloric increments and constituent changes, including newly arriving species.
The interval rejects a decrease beyond `128 epsilon sum(|U|/T)`. This avoids
subtracting nearly equal absolute entropies in the ordinary path. A separate
numerical uncertainty estimate requests extended reference evaluation when it
could change the entropy decision; it never enlarges the physical allowance.
Pressure iterations also enforce the material temperature domain, refining near
its boundaries.

The kernel selection is persisted in checkpoint version 4, together with both
viscosity coefficients and all existing settings. Versions 2 and 3 retain their
previous bytes and meanings. Unsupported kernel/version values reject. There
is no scheduling history beyond the saved physical state/settings in this
initial serial kernel; checkpoint continuation is exact on the same build.

## Verification and measurement

`ma_mac_interval_kernel_tests` compares the port against identical reference
selectors in periodic 2-D, closed 2-D with an interior solid, and closed 1-D
scenes, including multigrid-sized periodic and disconnected domains. Native
controls cover exact checkpoint continuation,
failed-budget atomicity, invalid settings, reference fallback, 518 account
boundary/range decisions, 81 present species, 82 passive identities and 10,000
closed-system steps.

`review_mac_interval_kernel.py` independently recomputes the saved inventories,
EOS, entropy and U+K at 100 decimal digits or as exact fractions. It checks
traces without an absolute allowance and rejects five altered captures. It
does not run native code. This evidence covers the selected port, not the
original strong-flow gate or a playable colony.

Each saved-data interval review now passes 105,931 checks and rejects five altered captures.
The native controls and saved-data reviews pass on GNU/Linux, fatal ASan/UBSan,
and MinGW Windows binaries under Wine. Linux/Wine continuation from each
platform's gas checkpoint agrees byte for byte (480 inspected numeric fields
per source). This is distinct from Windows OS evidence and does not close the
existing liquid cross-platform continuation failure. The separate
[multigrid controls](MAC_MULTIGRID.md) verify periodic and disconnected
hierarchies, coefficient replacement and cache-independent continuation.

Build and review:

```sh
cmake --build build-production --target ma_mac_interval_kernel_tests
build-production/ma_mac_interval_kernel_tests > interval.stdout
python3 native/tests/review_mac_interval_kernel.py interval.stdout
build-production/ma_mac_interval_kernel_tests --benchmark 500 500 5
```

Benchmark arguments are width, height and measured interval count, followed
optionally by maximum timestep. One interval warms the solver before measured
consecutive 0.1 s intervals. `--reference-benchmark` uses identical physical
selectors through the strict kernel. The output records median/max wall time,
steps, retries, timestep, implementation and the minimum count of changed cells.
Packing, owner copies and interval audits are included; requested checkpoint
serialization, game adapters and rendering are outside this timing.

The GNU 15.2 Release/IPO run on the Ryzen 5900X took 9.51 ms median for
256 cells and 10,161.31 ms for 250,000 cells, over five intervals at maximum
dt=0.01 s (55 accepted substeps, no retries). Every cell changed in every
interval. The identical donor-cell reference configuration took 108.54 ms median
at 256 cells (50 accepted substeps); its absolute clock avoids the port's tiny
remainder step on some intervals. The port is about 11 times faster in this
small comparison; neither timing describes the original higher-order defaults.

That initial 250,000-cell result was about 163 times over the 62.5 ms target. Peak
standalone resident memory was about 329 MiB, including the test owner and
buffers, rather than additional memory inside ONI. These samples are not
performance acceptance: physical
regularization, higher-order reconstruction, liquids, free surfaces, local
subcycling, further parallel ports, machines and game rendering
remain open.

The subsequent [deterministic worker implementation](../docs/interval-parallel-pressure.md)
parallelizes large pressure and thermodynamic passes while retaining the
serial arithmetic order within each row and reduction block. It does not
promote a new physical configuration or close the performance gate.

The subsequent [multigrid and reduction follow-up](MAC_MULTIGRID.md) reduces
the measured 250,000-cell median to 6,626.66 ms, with no retries or physical
selector changes. The same fine operator, outer root and interval accounts
remain in force. This remains about 106 times over the solver-plus-adapter
budget, before adapters and rendering.
