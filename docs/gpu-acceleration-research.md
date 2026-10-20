# GPU acceleration research

**Decision (user, 2026-10-19): the GPU route is not pursued.** The real-time path
stays the CPU production kernel. This page keeps the evaluation's conclusions; its
benchmarks (RTX 5090, CUDA 13.2, Ryzen 9 5900X, commit `11c32a6`) were scratch-only
and are not tracked. Labels: [M] measured, [P] published, [E] estimated.

## Why not

- Under the binary64 contract the production kernel is FP64-throughput-bound: about
  6,700 FP64 operations per cell-substep [M]. GeForce GPUs run FP64 at 1/64 of FP32
  [P]; an RTX 3060/4060 (about 0.2 TFLOPS FP64) is below the 5900X's own FP64 vector
  peak, and Intel Arc/Iris Xe have no hardware FP64 [P].
- Even re-formulated, the best-case predictions miss 62.5 ms/250k cells and
  25 ms/100k cells by about 2–5× on RTX 3060/4060/4070-class GPUs and the Steam
  Deck, before rendering shares the GPU [E]. A full CUDA port reaches the targets
  only on RTX 5090-class hardware (0.4% of Steam users) [P], and CUDA cannot run
  under Proton or on AMD, Intel or the Deck [P].
- The structural blocker is the line-chained Gauss–Seidel sweeps (shock heat,
  compression, momentum pair limiter): in reference order one axis pass took
  1.07 ms on the RTX 5090, about 8× one CPU core [M], for a floor near 130 ms per
  0.1 s at any cell count [E]. A colour-ordered variant previously failed the
  reference by 2.4 K / 1% at a vacuum front.
- Unity compute shaders, WebGPU, HIP/ROCm, SYCL, OpenCL and vendor sparse libraries
  each lack FP64 transcendentals, cross-vendor reach, Proton support or bit
  reproducibility. FP32 or float-float state breaks the 128-ε audit by about six
  orders of magnitude [M].

## What carries over to the CPU kernel

These were the recommended levers for the real-time gap, and still apply to the
CPU production kernel ([performance plan](production-timestep-progress.md)):
- Fewer substeps and pressure solves per 0.1 s: calm-region steps and local
  subcycling.
- Fused passes to cut memory traffic: Jacobi PCG alone moves about 30 MB per
  iteration, about 12 GB per 0.1 s [E]; a parallel, fused V-cycle.
- Parallel, re-validated sweep formulations, which also fix CPU thread scaling.
- An FP32 V-cycle inside the FP64 PCG: 2.6× cheaper on the GPU with identical PCG
  iteration counts (12/12, 21/21) and solutions within 1.1e-15 [M].
- Device-independent reductions: a fixed tree, or an exact order-independent
  superaccumulator (26 µs per 250k values on the GPU, equal to `math.fsum`) [M].

## Determinism findings that affect the CPU build

- A single glibc 2.43 binary returns different `exp`/`expm1`/`log1p`/`log`/`pow`
  results depending on whether the CPU's FMA code path is selected (268–12,219 of
  4.19M arguments differ) [M]. In-tree portable or correctly rounded versions (for
  example CORE-MATH, MIT) would remove this.
- MSVC's 64-bit `long double` differs from GNU's 80-bit x87 type in the production
  conduction law; the production kernel should not use `long double`.
- nvcc's default FMA contraction changed 62–69% of PCG entries after 20 iterations;
  with `--fmad=false`, PCG, limited-linear remap, the compression sweep and
  in-house `exp`/`expm1`/`log1p` matched a CPU replica bit for bit [M].
