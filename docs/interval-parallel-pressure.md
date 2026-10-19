# Deterministic interval pressure workers

The opt-in [interval kernel](../native/MAC_INTERVAL_KERNEL.md) can distribute
independent pressure rows across a reusable worker pool. The production
numerical default remains the reference kernel. This is an implementation
change to the admitted donor-cell ideal-gas equations; it does not port the
remaining material models or establish real-time colony acceptance.

## Work and ordering

The executor assigns fixed contiguous ranges of 256-row blocks. Each task owns
its outputs and completes before the next dependent pass. Parallel operations
include thermodynamic readings, pressure work and face responses, pressure
residuals, matrix-vector products, Krylov vector updates, dot-product blocks
and multigrid smoothing/restriction/interpolation. Small passes stay serial.

The matrix operation gathers a row's incident faces in the original global
x-face then y-face order. It preserves the ordering at periodic wraps and
processes a one-cell self edge once, applying both of that edge's contributions.
Each row evaluates the same coefficient product. Physical transport continues
to use the existing shared face fluxes.

Dot products retain the four double-double lanes and fixed 256-cell blocks.
The final reduction visits block results in the same order as the serial
implementation. Pressure residual maxima use fixed block results too; their
finite nonnegative maximum is independent of grouping. No floating-point
atomic sum is used. Conservation audits and the nonlinear/linear convergence
bounds remain unchanged.

Each job copies the caller's floating-point environment to its workers,
including x87 precision, rounding, SSE masks and FTZ/DAZ. Workers restore their
previous environments afterwards. Sticky arithmetic exceptions are merged
back into the caller. If multiple blocks throw, the caller waits for all
workers and receives the lowest failing block's exception. The original owner
is unchanged until the interval's existing validation and commit.

## Configuration and lifetime

The default uses at most four execution participants, including the caller.
`MA_MAC_WORKERS=1..64` selects a fixed count for a process; invalid values
reject. This controls execution only. It does not change any physical law,
timestep or reduction order, and never responds to wall-clock load. Checkpoint
continuation is checked across worker counts and against the preceding serial
binary. No scheduling history is needed to reproduce the physical state.

Concurrent callers serialize dispatches while keeping separate operands.
Nested independent operations execute inline, avoiding a pool waiting on
itself. Native shutdown retires the pool after stopping the simulator. A
retained active caller can finish on its old pool while another caller starts
a new one. Tests and standalone large-grid controls explicitly retire their
workers before exit. The process-wide holder has no DLL-detach destructor:
Windows must not join live threads while holding its loader lock.

The bounded development game ABI admits at most 4096 cells, below the parallel
thresholds. Existing small Unity controls therefore do not establish large-grid
worker integration or Windows DLL teardown at colony scale. Those remain part
of the production ABI and whole-colony activation gates.

## Evidence and limits

Standalone executor controls cover exact row ownership, partial final blocks,
concurrent and nested callers, stable exception selection, caller arithmetic,
and retirement/recreation during retained work. ThreadSanitizer passes these
controls. A 33,153-cell odd periodic source with a solid corner continues byte
for byte with the preceding serial binary and with one, four and twelve
workers. The source and continuations are retained under the ignored
`local/interval-parallel-v1/` directory.

Initial measurements do not meet the performance gate. A shared per-block
claim counter regressed a three-sample 250,000-cell run from about 6.06 s median
to 11.01 s with twelve workers. It was removed. Stable partitions and complete
pressure gathers gave about 4.93 s median with four workers, but subsequent
measurements varied substantially under concurrent host CPU workloads. These
are development diagnostics, not a controlled p95 comparison or a speedup
acceptance claim. All 250,000 cells change; the three-interval runs use 33
accepted substeps, no retries and 480 pressure iterations. The 62.5 ms
solver-plus-adapter budget, normal rendering, higher-order defaults,
multiphase ports and complete colony integration remain open.

Final verification passes all 40 native Release and all 40 frozen Ubuntu
22.04 tests. Five scheduler, solver and C ABI tests pass
fatal ASan/UBSan with leak detection; the standalone scheduler also passes
ThreadSanitizer. Four Windows-target groups pass under Wine. The interval and
hierarchy output streams remain byte-identical to the previous native binary;
their saved readers check 105,931 and 40,216 assertions and reject five and
eight corruptions respectively. Python tests pass 297 with four skips.

Additional continuations cover a periodic 17×1 strip, 2×17 and 17×2 grids,
an odd 3×7 grid and the 129×257 domain with a solid corner. Every native
one/four/twelve-worker image matches the previous serial binary. The large
image also matches under fatal ASan and the Windows executable under Wine
with one, four and twelve workers. This is distinct from actual Windows OS
and large-domain native DLL integration evidence. A redundant Windows text
dump to the NUL device was cancelled during formatting; the regular-file
controls completed normally and supply the acceptance evidence.
