# GPU acceleration research

**Decision (user, 2026-10-19): the GPU route is not pursued for now.** The real-time
path stays the CPU production kernel. This page records the evaluation that led
to that decision; nothing here is implemented.

**Scope.** This is research plus small isolated benchmarks. Nothing in the repository was changed. Source state: a clone of `main` at `11c32a6`, without the uncommitted working-tree edits.

**Host.** The work ran on a shared machine: Ryzen 9 5900X (12 cores), 60 GB RAM, GeForce RTX 5090 (32 GB, driver 610.43.02), CUDA 13.2 (nvcc 13.2.78) and GCC 15.2.
- CPU load: the 1-minute load average was 21–41 throughout.
- GPU sharing: another tenant held 6–29 GB of VRAM and used 8–19 % of the GPU, with bursts to 98–100 %. This inflates the latency figures. Kernel device times repeated within ±3 %.

**Labels used in this report:**
- **[M]** measured here;
- **[P]** published, with a source in §6;
- **[E]** estimated or predicted, with the method stated.

## 1. Summary and recommendation

**Bottom line.** A GPU port can't deliver 62.5 ms/250k or 25 ms/100k on typical player hardware. It should not be the main path to milestone B.

**Why the gains are limited.**
- Under the binary64 contract the kernel is **FP64-throughput-bound on GPUs**. It executes about **6,700 FP64 operations per cell-substep** [M, gcov-weighted].
- GeForce GPUs run FP64 at 1/64 of FP32 [P]. The RTX 3060 and 4060 (about 0.2 TFLOPS FP64) are below the 5900X's own FP64 vector peak.
- Intel Arc and Iris Xe have no hardware FP64 [P].
- Even after the necessary re-formulation, the best-case predictions miss both targets on RTX 3060/4060/4070-class GPUs and on the Steam Deck, by about 2–5×. That is before rendering shares the GPU (§3).

**Bit-for-bit reproducibility is achievable for the arithmetic that matters** [M]. On the RTX 5090, with `--fmad=false`, these all match a CPU replica bit for bit (0 differences in 250,000 entries):
- the PCG iteration, in both a fixed tree order and the exact production row-block order;
- a limited-linear remap;
- the production compression line sweep;
- my own `exp`/`expm1`/`log1p`.

Two things break it:
- **nvcc's default FMA contraction:** 62–69 % of PCG entries differ after 20 iterations.
- **Vendor math libraries:** CUDA and glibc disagree on 3.4–5.5 % of arguments.

**Pre-existing CPU risk.** A single glibc 2.43 binary returns different `exp`/`expm1`/`log1p`/`log`/`pow` results depending on whether the CPU's FMA code path is selected (268–12,219 of 4.19M arguments) [M]. Separately, MSVC's 64-bit `long double` already differs from GNU's 80-bit x87 type in the production conduction law.

**The structural blocker** is the line-chained Gauss–Seidel sweeps (shock heat, compression, momentum pair limiter).
- In the exact reference order, one axis pass takes 1.07 ms on the RTX 5090 [M]. That is only about 8× one CPU core, because just 500 lines run in parallel.
- The projected floor is about 130 ms per 0.1 s, whatever the cell count [E].
- Any GPU path first needs parallel formulations of these sweeps, re-validated against the reference. A colour-ordered variant previously failed the reference by 2.4 K / 1 % at a vacuum front.

**Recommended order:**

1. **Keep the CPU production kernel as the real-time path.** Fund the levers that scale with the 35× gap:
   - fewer substeps and pressure solves per 0.1 s, through calm-region steps and local subcycling;
   - fused passes to cut memory traffic. Jacobi PCG alone moves about 30 MB per iteration, about 12 GB per 0.1 s [E];
   - a parallel, fused V-cycle;
   - parallel, re-validated sweeps, which also fix CPU thread scaling.

2. **Make the code backend-neutral now.** Each change also helps the CPU:
   - **(a)** in-tree portable or correctly rounded `exp`/`expm1`/`log1p`/`log`, e.g. CORE-MATH (MIT);
   - **(b)** device-independent reductions: a fixed tree, or an exact order-independent superaccumulator. On the RTX 5090 it takes 26 µs per 250k values and equals `math.fsum` exactly [M];
   - **(c)** an FP32 V-cycle inside the FP64 PCG: 2.6× cheaper on the RTX 5090, with identical PCG iteration counts (12/12, 21/21) and solutions within 1.1e-15 [M];
   - **(d)** no `long double` in the production kernel.

3. **Optional, later: an NVIDIA-only CUDA accelerator** with a bit-identical CPU fallback.
   - **Best first slice:** the pressure linear solve (PCG plus geometric multigrid). It has no transcendentals and no sweeps, it is bandwidth-bound (even an RTX 3060 has about 7× the 5900X's DRAM bandwidth), and it is roughly 1k lines. Amdahl's law caps it at about 1.3–1.4× overall.
   - **Full CUDA port:** about 5–35× faster than today's CPU, depending on the GPU [E]. It reaches the targets only on RTX 5090-class hardware, which is 0.4 % of Steam users [P].
   - **Where CUDA can't run:** under Proton [P], and on AMD, Intel or the Deck.

4. **Vulkan compute** is the only API covering NVIDIA, AMD, the Steam Deck and Proton [P]. For binary64 it is the highest-effort route:
   - no FP64 transcendentals;
   - RADV computes FP64 division as `x*rcp(y)`;
   - every operation needs `NoContraction`;
   - every kernel must be rewritten in a shader language.

   Consider it only after a CUDA prototype proves the gains.

**Not viable, and why:**
- **Unity compute shaders:** D3D11 doubles are optional, HLSL/DXIL has no double `sqrt`/`exp`/`log`, Metal has no doubles, and dispatches share ONI's D3D11 render queue.
- **WebGPU/wgpu/Dawn:** WGSL has no f64.
- **HIP/ROCm:** no RX 6700 XT or Deck support, and the runtime can't be shipped.
- **SYCL:** heavy runtime plus the vendor stack, and Intel has no FP64.
- **OpenCL:** none under Proton, no default ICD on the Deck, Rusticl FP64 is opt-in, and Intel has no FP64.
- **AmgX, cuSPARSE, rocSPARSE, hypre, PETSc, Ginkgo:** vendor-specific, hundreds of MB, no cross-GPU bit reproducibility, and assembled AMG is the wrong tool for a matrix-free 5-point operator.
- **FP32 or float-float state:**
  - it breaks the 128-ε audit by about 6 orders of magnitude [M];
  - the wave's kinetic energy lies below FP32 resolution of U;
  - traces below about 1e-38 vanish.

## 2. Options table

| Option | What it is | License | Vendor / OS coverage | Maturity | Stages | Effort (codebase + mod) | Verdict |
|---|---|---|---|---|---|---|---|
| CUDA, hand-written kernels | C++ kernels sharing `__host__ __device__` code with the CPU kernel (as benchmarked) | NVIDIA EULA; cudart static and NVRTC redistributable | **NVIDIA only**, Windows and native Linux. CUDA 13 needs driver ≥580 and Turing+. **Not under Proton** (empty `nvcuda.dll` stub) | Mature | All; sweeps need re-formulation | Medium–high: about 6k kernel lines, `--fmad=false`, fallback and canaries | **Optional accelerator only** |
| HIP/ROCm | AMD's CUDA-like runtime | MIT | Linux RX 7700+/9000; Windows RX 7000/9000. No RX 6700 XT, no Deck. **Runtime can't be redistributed** | Patchy on consumer GPUs | All | High; deployment blocked | Not viable |
| SYCL (DPC++, AdaptiveCpp) | Single-source C++ over vendor backends | Apache-2.0+LLVM / Intel EULA binary runtime; AdaptiveCpp BSD-2 | Needs the vendor stack; AdaptiveCpp on Windows experimental; Intel Arc/Xe-LP lack FP64 | Medium | All | High: two runtimes on players' PCs | Not viable |
| OpenCL | Khronos C kernels | Vendor drivers | NVIDIA (fp64), AMD Windows (fp64); AMD Linux ROCm-only or Rusticl (fp64 opt-in); Intel none; Deck no default ICD; **Proton unusable** | Mature, declining | All. Double ÷ and √ correctly rounded; `exp` ≤3 ulp, so own math needed; FP_CONTRACT on by default | Medium–high | Weak |
| Vulkan compute | SPIR-V from GLSL/HLSL | Khronos; drivers | NVIDIA, AMD (Windows, RADV), **Steam Deck**, **Proton**; Intel Arc/Xe-LP no `shaderFloat64` | Mature API; fp64 rarely used | All. No fp64 `exp`/`log`; RADV ÷ not correctly rounded | **Highest** | Broadest coverage; defer |
| WebGPU/wgpu/Dawn | Portable GPU API | MIT/Apache, BSD | No f64 (wgpu `SHADER_F64` is native Vulkan only) | — | None | — | Not viable |
| Unity compute shaders | HLSL in ONI (Unity 6000.3.5f2, D3D11) | Unity | Doubles optional, no double transcendentals, Metal no doubles, **shares render queue** | — | None at binary64 | — | Not viable |
| D3D11/12 DirectCompute | Native Windows compute | Windows | Windows only; doubles optional | — | Partial | High | Not viable |
| Kokkos / RAJA / OpenMP offload | Portability layers | Apache-2.0+LLVM / BSD-3 / GCC-LLVM | Inherit the underlying runtime; one device backend per Kokkos build; nvcc FMA default stays on | Mature (HPC) | All | Medium | Optional wrapper for CUDA only |
| AmgX | NVIDIA algebraic MG | BSD-3 | CUDA only | Mature | Pressure | Medium; pulls in hundreds of MB of cuSPARSE/cuBLAS; no cross-GPU determinism | Not recommended |
| cuSPARSE / rocSPARSE / rocALUTION | Vendor sparse libraries | Proprietary / MIT | CUDA / ROCm | Mature | Pressure | Medium; only some algorithms deterministic | Not recommended |
| hypre / PETSc / Ginkgo | HPC solver frameworks | MIT-Apache / BSD-2 / BSD-3 | One GPU backend per build | Mature | Pressure | High | Not recommended |
| Custom matrix-free GMG + PCG | 2×2-bin V-cycle, 0.5/1.5 Jacobi smoothing | Own | Any, CPU included | Prototype [M] | Pressure | Low–medium (about 1k lines) | **Recommended** |
| Reference GPU hydro codes | Cholla (MIT), AthenaK (BSD-3), CloverLeaf (staggered Lagrange-remap; UoB ports GPL-3), TeaLeaf (implicit CG; GPL-3) | Various | — | Mature | Design reference | Read only | Useful designs |
| Reproducible reductions | Fixed tree + DD; exact superaccumulator (ExBLAS-style); CUB `gpu_to_gpu`; ReproBLAS (CPU only) | Own / BSD | Any | [M] | Audit, dot products | Low | **Recommended** |
| Correctly rounded / portable math | CORE-MATH (MIT); LLVM libc (GPU build not correctly rounded); own polynomials | MIT / Apache | Any | Mature on CPU | EOS, `phi`, diffusion | Low–medium | **Recommended** |
| Double-double on GPU | Own `DD` (works, measured); CAMPARY (license UNVERIFIED); QD (BSD-LBNL); GQD (stale) | — | Any | — | Audit | Low | Keep own DD |
| FP32 V-cycle inside FP64 PCG | Mixed-precision preconditioner | Own | Any | [M] | Pressure | Low | **Recommended** |
| FP32 or float-float state | Reduced-precision storage or fluxes | — | Would enable Intel GPUs | — | — | — | **Not viable** |
| CPU AVX2 recompile | `-march=x86-64-v3`, contraction off | — | Needs a baseline fallback build | [M] | All | Trivial | 6–10 % on 1 thread; bit-identical |
| CPU algorithmic work | Fewer substeps, fusion, parallel V-cycle and sweeps | Own | Every player | In progress | All | — | **Primary path** |

## 3. Time improvements

### 3.1 CPU baseline

**Reported figures** (Jacobi, 12 threads, 250k cells, per 0.1 s):
- pressure about 300 ms;
- transport about 340 ms (nested timers: reconstruction about 115, momentum about 170);
- shock heat about 175 ms;
- compression about 120 ms;
- EOS about 78 ms;
- audit 13–35 ms;
- conversion 13–25 ms;
- p50 total 1,076–1,107 ms; automatic multigrid p95 2,210–2,329 ms.

**Re-measured on this loaded host [M],** at 250k with a scratch-only stage-timing print:
- **12 threads** (`taskset -c 0-11`, load 31.7–35.8): 1,497–2,134 ms per 0.1 s. One sample's stages: shock 216, compression 152, EOS 91, pressure 472, transport 471 (reconstruction 164, momentum 218), audit 20, conversion 23 ms. That interval had 339–410 PCG iterations and 19–20 Newton solves.
- **1 thread:** 4,842–5,641 ms per 0.1 s.

### 3.2 RTX 5090 measurements of representative kernels [M]

All FP64 with `--fmad=false`, at 250k cells unless stated; medians of 3 repetitions. The "per 0.1 s" column assumes 400 PCG iterations, 72 V-cycles, 10 substeps and 12.5M EOS evaluations.

| Kernel | RTX 5090 | Per 0.1 s at 250k | CPU comparison |
|---|---|---|---|
| Jacobi-PCG iteration, tree+DD reduction, CUDA graph | 0.036 ms device, 0.043 ms wall | 14–17 ms | Production about 0.75 ms per iteration on 12 threads; replica 3.4–3.9 ms on 1 thread |
| Same, host readback every iteration | 0.18–0.20 ms | 70–80 ms | — |
| Same, **exact production row-block order** | 0.357 ms | 143 ms | Bit-identical to CPU replica |
| Same at 100k | 0.028 ms device / 0.035 ms wall | 11–14 ms | — |
| Multigrid V-cycle, FP64 / FP32 | 0.133 / 0.052 ms (100k: 0.111 / 0.047) | 9.6 / 3.7 ms | Production serial about 13 ms |
| Multigrid prepare | 0.027 ms | — | 5–6.5 ms |
| Remap, 6 primal + 2 dual fields, FP64 / FP32 | 0.168 / 0.024 ms per substep (100k: 0.081 / 0.016) | 1.7 ms | Replica of the primal part: 43–72 ms per substep on 1 thread |
| Remap, 10 + 2 fields, FP64 | 0.221–0.231 ms | 2.3 ms | — |
| EOS-like evaluation, 3 transcendentals, 12.5M | CUDA libm 1.05 ms; portable 2.0 ms; FP32 0.019 ms | 1–2 ms | glibc on 1 thread 308–344 ms |
| Exact-order line sweep, one axis | 1.07 ms (100k: 0.98–1.09) | about 128 ms [E] | 8.4–9.3 ms on 1 thread |
| Reduction of 250k doubles | atomic 4.3 µs (17–21 distinct results in 50 runs); tree+DD 10–13 µs; exact 26 µs | — | — |
| State copy, 16 MB, pinned / pageable | 0.56–0.65 / 1.0–2.0 ms per direction | 1.2–4 ms | — |
| Tiny kernel + readback | median 9.4–135 µs; p99 about 197 µs | — | — |

FP64 binds even on the RTX 5090: the FP64 remap is 7× slower than FP32, and the FP64 V-cycle is 2.6× slower.

### 3.3 Work content of the production kernel [M]

**Method.** I built an `-O0 --coverage` copy and ran the 500×16 wave with Jacobi, using the 3-sample minus 1-sample difference (20 substeps). I multiplied the gcov line counts by the FP instructions `objdump` attributes to each line. Weights for NVIDIA FP64 slots: add/sub/mul/compare 1, div and sqrt 10, transcendental 35. Expect about ±50 %.

**Counts per cell-substep:**
- 3,013 add/sub/mul;
- 1,395 compares;
- 219 divisions;
- 4 sqrt;
- 1.8 log.

That gives about **6,700 FP64 slots**:
- transport 2,292;
- pressure 1,984, of which PCG is about 1,200;
- regularisation 1,247;
- EOS 486;
- other 670.

**What this implies.**
- About 1,300 slots (20 %) sit in line-chained functions.
- At 250k this is 1.67e10 slots per 0.1 s.
- Today's CPU kernel therefore runs at about 15 G slots/s. That is roughly 2 % of the 5900X's AVX2 FP64 peak [E]: it is not FLOP-bound.

### 3.4 Per-player-GPU predictions [E] (`tools/predict.py`)

**Method.** For each stage, time = max(FP64 slots ÷ (40 % of peak), bytes ÷ (75 % of DRAM bandwidth)).
- **Pressure solve:** from the measured RTX 5090 iteration and V-cycle times, with doubled bandwidth when the 30 MB working set fits in cache.
- **Exact-order sweeps:** the measured 1.07 ms per axis pass, scaled by clock.
- **Fixed costs:** 80 kernels per substep at 3 µs, 40 syncs at 10 µs, and state copies.
- **Specs** (FP64 TFLOPS / bandwidth GB/s):
  - RTX 5090: 1.637 / 1,792;
  - RTX 4070: 0.455 / 504;
  - RTX 4060: 0.236 / 272;
  - RTX 3060 12 GB: 0.199 / 360;
  - RX 6700 XT: 0.826 / 384 (plus 96 MB Infinity Cache);
  - Steam Deck (LCD): 0.102 / 88.
- **Intel Arc A750 and Iris Xe:** no native FP64, so no binary64 port is possible.

**Scenario A: direct port, exact sweep order** (ms per 0.1 s):

| | RTX 5090 | RTX 4070 | RTX 4060 | RTX 3060 | RX 6700 XT | Steam Deck |
|---|---:|---:|---:|---:|---:|---:|
| Sweeps alone | 128 | 150 | 151 | 209 | 144 | 232 |
| Total, 250k | 163 | 233 | 330 | 393 | 206 | 678 |
| Total, 100k | 149 | 188 | 217 | 287 | 173 | 415 |

**Scenario B: parallel re-formulated sweeps, FP64 MG-PCG with FP32 V-cycle** (ms per 0.1 s, 250k):

| Stage | CPU now (12 threads) | RTX 5090 | RTX 4070 | RTX 4060 | RTX 3060 | RX 6700 XT | Steam Deck |
|---|---:|---:|---:|---:|---:|---:|---:|
| Pressure | about 300 | 9.3 | 24.8 | 48.8 | 47.8 | 21.4 | 116 |
| Transport | about 340 | 8.7 | 31.5 | 60.6 | 71.9 | 17.3 | 140 |
| Shock heat + compression | about 295 | 4.8 | 17.1 | 33.0 | 39.2 | 10.3 | 76 |
| EOS | about 78 | 1.9 | 6.7 | 12.9 | 15.3 | 3.7 | 30 |
| Audit + conversion | 26–60 | 2.6 | 9.2 | 17.7 | 21.0 | 5.1 | 41 |
| Launches, syncs, copies | — | 4.0 | 4.1 | 5.5 | 4.1 | 4.1 | 4.4 |
| **Total, 250k** (target 62.5) | 1,076–1,107 | **31** | **93** | **178** | **199** | **62** | **408** |
| **Total, 100k** (target 25) | 430–653 | **17** | **44** | **75** | **87** | **32** | **170** |

**Sensitivity.** At 25–55 % FP64 efficiency the 250k totals become: RTX 5090 44–25 ms, RTX 4070 140–73 ms, RTX 3060 306–152 ms, RX 6700 XT 86–51 ms.

**Excluded:**
- gas diffusion (180–300 ms actively mixing on CPU);
- the adapter and 32 duplicants;
- rendering contention.

### 3.5 Are the targets reachable?

- **Scenario A:** none reach either target.
- **Scenario B, 250k / 62.5 ms:**
  - RTX 5090: yes;
  - RX 6700 XT: borderline;
  - RTX 4070, 4060, 3060 and the Steam Deck: no;
  - Intel: impossible.
- **Scenario B, 100k / 25 ms:** only the RTX 5090.
- **GPU share.** At 1×, solver GPU time comes out of the rendering budget. An RTX 4070 at 93 ms per 100 ms leaves essentially nothing for frames.
- **Hardware share.** The most common Steam GPUs fall in the "no" band [P].

### 3.6 Coexistence with rendering [M]

**Setup.** Two CUDA processes:
- a "renderer" submitting 6 ms of GPU work every 16.7 ms;
- a "solver" running 4 kernels plus a readback per iteration.

**Results:**
- renderer frame p99 rose from 7.1 to 10.8 ms;
- solver iteration p99 rose from 0.22 to 2.80 ms (max 4.3 ms);
- solver throughput fell 32 %.

**Implication.** With about 40 syncs per 0.1 s, stalls at that level would add about 110 ms [E]. A GPU path needs O(1) syncs per interval (CUDA graphs, device-side convergence) and kernels far below the 2 s Windows TDR [P]. This was CUDA vs CUDA; ONI's own D3D11/OpenGL contexts were not tested.

### 3.7 CPU SIMD baseline [M]

Rebuilt with `-march=x86-64-v3` and contraction still off (4,792 ymm instructions, no FMA):
- 1 thread at 50k: 964–1,041 ms vs 1,040–1,158 ms baseline;
- 12 threads at 250k: within noise;
- checkpoint SHA-256 identical.

## 4. Accuracy, determinism, conservation and audit

### 4.1 Bit identity and determinism [M]

**What the docs require.**
- `MAC_SOLVER.md`: results bit-identical "for the same build and inputs, independent of thread count and scheduling", with exact checkpoint continuation.
- `completion-plan.md`: "same-platform deterministic continuation, bounded cross-platform numerical differences".
- Current evidence goes further: Linux and MinGW/Wine continuations match byte for byte.

**Consequence.** A GPU path must reproduce the CPU path bit for bit. Otherwise a save's continuation depends on the player's GPU, driver and fallback.

**Measured with `--fmad=false`:** 0 differences against a CPU replica for:
- PCG (both reduction orders);
- remap (FP64 and FP32);
- the exact-order sweep;
- portable math over 4.19M arguments.

**Measured with the default `--fmad=true`:**
- PCG: 155,456 and 172,402 of 250,000 entries differ (max relative 5e-12);
- sweep: 28,500 cells differ;
- portable math: up to 69,952 arguments differ.

**Vendor math libraries.** CUDA vs glibc differ for `exp` (3.4 %), `expm1` (4.3 %) and `log1p` (5.5 %) of arguments in [−0.5, 0.5], all within 2 ulp. Summed EOS outputs happened to agree on the benchmark's narrow range; that can't be relied on.

**Pre-existing CPU issue.** One glibc 2.43 binary, with and without its FMA code path, differs on:

| Function | Differing arguments (of 4.19M) |
|---|---:|
| `exp` | 2,929 |
| `expm1` | 12,219 |
| `log1p` | 1,393 |
| `log` | 268 |
| `pow` | 2,812 |

The production EOS, regularisation and entropy code use these functions, so cross-CPU (and MSVC vs glibc) bit identity isn't guaranteed today. The conduction law also uses `long double`.

**Reductions.**
- Atomic sums are non-deterministic.
- Fixed trees are deterministic and portable if the CPU uses the same shape.
- The exact superaccumulator is independent of order, launch shape and device, and equals `math.fsum`.

**Driver risk.** Driver JIT updates could change results. Mitigate with PTX `.rn` forms and a startup canary that falls back to the CPU on mismatch.

### 4.2 Conservation and audit

Conservation by construction holds on a GPU in any precision. The audit (128 binary64 ε, about 2.8e-14 relative) is what constrains the design.
- **FP64:** unchanged. Measured transport drift over 1,000 substeps ≤5.3e-17.
- **FP32 storage:** measured drift −3.3e-6 over 1,000 substeps, about 6 orders of magnitude over the bound.
- **Double-double audit sums** run on the GPU (measured).
- **Exact `MacEnergyAccount` escalation** stays on the CPU, or uses the exact accumulator.

### 4.3 Reduced precision against the gate tolerances [E unless marked]

Reference state: 20 Pa wave on 100 kPa, 300 K air.

| Quantity | FP32 resolution | Tolerance | Consequence |
|---|---|---|---|
| Absolute pressure | 0.0078 Pa | 0.2 Pa (26 ulp) | Random-walk error over 1,000 substeps about 0.12 Pa; a correlated bias up to 3.9 Pa. **Not acceptable.** An FP32 increment on an FP64 base is negligible (1.9e-6 Pa) |
| Temperature | 3.1e-5 K | 0.1 K | Acceptable as a derived quantity |
| U vs kinetic energy | ulp(U) 0.016 J vs wave KE 1.4e-3 J/m³ | Energy audit | Kinetic transfers are **lost** (binary64 ulp 2.9e-11 J) |
| O2 partial pressure | 0.002 Pa | 220 Pa | Acceptable |
| Bulk constituents | Measured max cellwise relative 1.7–2.3e-5 after 1,000 substeps [M] | 1 % | Acceptable for accuracy, not for the audit |
| Traces | FP32 floor 1.2e-38 | No positive constituent removed | Traces **vanish**. Not acceptable |
| Hydrostatic balance | 0.0078 Pa vs 11.4 Pa per cell | Exact certificate | Spurious forces; certificate impossible |
| FP32 V-cycle in FP64 PCG | — | 16 ε root closure enforced by the FP64 outer loop | **No measured change** in iterations or solution [M]; consistent with the literature [P]. **Acceptable** |
| Float-float | 48-bit significand, FP32 range | Binary64 identity and range | Not acceptable |

### 4.4 What stays FP64 or exact, and on the CPU

**All state and transfers stay binary64:**
- masses, U, passives and J;
- parcels and fluxes;
- pressure increments;
- EOS values;
- audit operands.

**Stays on the CPU:**
- the reference oracle and whole-interval range fallback, including the x87 `long double` EOS;
- exact expansion escalation and full-range `MacEnergy`/`MacScaled` arithmetic (the GPU flags, the CPU decides);
- exact rounding ledgers for flagged below-range products;
- checkpoint serialisation and publish/retry control.

**May use FP32 or either device:**
- the V-cycle preconditioner;
- diagnostics that never feed state;
- rendering projections.

## 5. Risks and open questions

1. **FP64 hardware gap.** GeForce runs FP64 at 1:64 and RDNA2 at 1:16; Intel, Metal and Xe-LP have none [P]. Speedups will be very uneven, and a CPU fallback is mandatory.
2. **Sweep re-formulation.** It is unproven: a colour-ordered variant already failed the reference by 2.4 K / 1 % at a vacuum front. It also changes CPU results, which means a new kernel selector and checkpoint version.
3. **Determinism across devices and drivers.**
   - Needs FMA off everywhere, own math, fixed or exact reductions, and no `long double`.
   - Driver JIT updates remain an ongoing risk.
   - RADV fp64 division needs a software replacement.
4. **Rendering contention.** Measured p99 stalls of 2.8 ms per sync, and frame p99 rose by 3.7 ms. Unity on D3D11 has no async compute, and the ≥30 FPS gate is unmeasured.
5. **Deployment.**
   - **Proton:** no CUDA or OpenCL. The project's Windows evidence comes from WindowsPlayer under Proton, so CUDA couldn't be validated that way.
   - **HIP:** the runtime can't be shipped.
   - **Steam Deck:** Vulkan only, and CPU and GPU share one ~15 W budget, so GPU load throttles the CPU.
   - **Integrated GPUs:** compete with the CPU for DRAM.
6. **Memory.** About 0.4–0.5 GB of VRAM at 250k. ONI's recommended spec is a 2 GB GPU.
7. **Maintenance.** Three implementations per stage (reference, CPU production, GPU), and the reviewers and controls would all need GPU routes.
8. **Open questions:**
   - the FP64 operation count (±50 %) and efficiency (25–55 %); a real port of one stage would settle both;
   - AMD FP64 efficiency on this code (no AMD GPU was available);
   - in-game contention with ONI's real D3D11/OpenGL renderer;
   - which ONI build (native Linux or Proton) Valve verified on the Deck;
   - GPU cost of the diffusion stage;
   - whether cross-machine bit identity is a product requirement.

## 6. Sources

**Project documents (read in the clone):**
- `docs/completion-plan.md`
- `docs/playtesting-readiness.md`
- `docs/solver-scaling-analysis.md`
- `docs/native-platform-numerics.md`
- `native/MAC_SOLVER.md`
- `native/MAC_KERNEL.md`
- `native/MAC_MULTIGRID.md`
- `native/MAC_PRESSURE_MOMENTS.md`
- `native/MAC_PRESSURE_WINDOWS.md`
- `native/MAC_DIFFUSION.md`
- `native/MAC_RANGE.md`

**GPU specs and FP64 ratios:**
- NVIDIA compute capabilities: https://docs.nvidia.com/cuda/cuda-programming-guide/05-appendices/compute-capabilities.html
- NVIDIA GPU list: https://en.wikipedia.org/wiki/List_of_Nvidia_graphics_processing_units
- RX 6700 XT: https://www.amd.com/en/products/graphics/desktops/radeon/6000-series/amd-radeon-rx-6700-xt.html
- RDNA2 FP64 ratio (W6800): https://www.amd.com/en/products/graphics/workstations/radeon-pro/w6800.html
- Arc A750: https://www.intel.com/content/www/us/en/products/sku/227954/intel-arc-a750-graphics/specifications.html
- Intel compute-runtime FAQ: https://github.com/intel/compute-runtime/blob/master/opencl/doc/FAQ.md
- Intel emulation speed: https://community.intel.com/t5/Intel-Fortran-Compiler/Double-precison-on-ARC-GPU/td-p/1450217
- Iris Xe architecture: https://www.intel.com/content/www/us/en/docs/oneapi/optimization-guide-gpu/2023-0/intel-iris-xe-gpu-architecture.html
- Intel `shaderFloat64`: https://www.intel.com/content/www/us/en/support/articles/000089817/graphics.html
- Steam Deck: https://www.steamdeck.com/en/tech
- Steam Deck APU analysis: https://chipsandcheese.com/p/van-gogh-amds-steam-deck-apu
- Ryzen 9 5900X: https://www.amd.com/en/products/processors/desktops/ryzen/5000-series/amd-ryzen-9-5900x.html
- Steam Hardware Survey: https://store.steampowered.com/hwsurvey/videocard/

**CUDA:**
- EULA: https://docs.nvidia.com/cuda/eula/index.html
- Release notes: https://docs.nvidia.com/cuda/cuda-toolkit-release-notes/index.html
- nvcc: https://docs.nvidia.com/cuda/cuda-compiler-driver-nvcc/index.html
- PTX: https://docs.nvidia.com/cuda/parallel-thread-execution/index.html
- Math functions: https://docs.nvidia.com/cuda/cuda-programming-guide/05-appendices/mathematical-functions.html
- MPS: https://docs.nvidia.com/deploy/mps/architecture.html
- Proton nvcuda stub: https://github.com/ValveSoftware/wine/blob/proton_10.0/dlls/nvcuda/nvcuda.spec

**HIP/ROCm:**
- Linux support: https://rocm.docs.amd.com/projects/install-on-linux/en/latest/reference/system-requirements.html
- Windows support: https://rocm.docs.amd.com/projects/install-on-windows/en/latest/reference/system-requirements.html
- Deployment: https://rocm.docs.amd.com/projects/install-on-windows/en/latest/conceptual/deployment-guidelines.html
- Clang contraction defaults: https://clang.llvm.org/docs/UsersManual.html
- HIP math: https://rocm.docs.amd.com/projects/HIP/en/latest/reference/math_api.html

**SYCL:**
- DPC++ license: https://github.com/intel/llvm/blob/sycl/sycl/LICENSE.TXT
- AdaptiveCpp: https://github.com/AdaptiveCpp/AdaptiveCpp

**OpenCL:**
- OpenCL C spec: https://registry.khronos.org/OpenCL/specs/3.0-unified/html/OpenCL_C.html
- Mesa environment variables: https://docs.mesa3d.org/envvars.html
- Proton OpenCL thunks: https://github.com/ValveSoftware/wine/blob/proton_10.0/dlls/opencl/unix_thunks.c

**Vulkan:**
- Precision rules: https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html#spirvenv-precision-operation
- GLSL.std.450: https://registry.khronos.org/SPIR-V/specs/unified1/GLSL.std.450.html
- RADV fp64 lowering: https://gitlab.freedesktop.org/mesa/mesa/-/blob/main/src/amd/vulkan/radv_shader.c

**WebGPU:**
- WGSL: https://www.w3.org/TR/WGSL/
- wgpu features: https://docs.rs/wgpu-types/latest/wgpu_types/struct.Features.html

**Portability layers:**
- Kokkos: https://github.com/kokkos/kokkos
- RAJA: https://github.com/LLNL/RAJA/blob/develop/LICENSE

**D3D:**
- D3D11 doubles: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ns-d3d11-d3d11_feature_data_doubles
- DXIL operations: https://github.com/microsoft/DirectXShaderCompiler/blob/main/utils/hct/hctdb.py
- TDR: https://learn.microsoft.com/en-us/windows-hardware/drivers/display/timeout-detection-and-recovery

**Unity and ONI:**
- Compute shaders: https://docs.unity3d.com/6000.3/Documentation/Manual/class-ComputeShader-introduction.html
- Async compute: https://docs.unity3d.com/6000.3/Documentation/ScriptReference/SystemInfo-supportsAsyncCompute.html
- ONI store page: https://store.steampowered.com/app/457140/Oxygen_Not_Included/
- ONI's Unity version (6000.3.5f2) and D3D11 device: local Player.log inspection, not a web source.

**Solver libraries:**
- AmgX: https://github.com/NVIDIA/AMGX
- cuSPARSE: https://docs.nvidia.com/cuda/cusparse/index.html
- CCCL determinism: https://developer.nvidia.com/blog/controlling-floating-point-determinism-in-nvidia-cccl
- rocSPARSE reproducibility: https://rocm.docs.amd.com/projects/rocSPARSE/en/docs-7.2.3/reference/reproducibility.html
- hypre: https://github.com/hypre-space/hypre
- PETSc: https://petsc.org/release/overview/gpu_roadmap/
- Ginkgo: https://github.com/ginkgo-project/ginkgo

**GPU multigrid and Poisson:**
- HPGMG GPU: https://developer.nvidia.com/blog/high-performance-geometric-multi-grid-gpu-acceleration/
- TeaLeaf: https://wrap.warwick.ac.uk/91037/1/WRAP-performance-portability-heat-conduction-solver-multi-core-systems-Kirk-2017.pdf

**GPU hydro codes:**
- Cholla: https://github.com/cholla-hydro/cholla
- AthenaK: https://ar5iv.arxiv.org/html/2409.16053
- CloverLeaf: https://github.com/UoB-HPC/cloverleaf
- NVIDIA Flow: https://github.com/NVIDIA-Omniverse/PhysX/blob/main/flow/source/nvflow/Pressure.cpp

**Reproducible and extended precision:**
- ReproBLAS: https://bebop.cs.berkeley.edu/reproblas/
- ExBLAS: https://github.com/riakymch/exblas
- Double-double costs: https://jncf2017.lip6.fr/files/slides/popescu.pdf

**Correctly rounded math:**
- CORE-MATH: https://core-math.gitlabpages.inria.fr/
- LLVM libc math: https://raw.githubusercontent.com/llvm/llvm-project/main/libc/docs/headers/math/index.rst

**Mixed precision:**
- Göddeke et al.: https://asc.ziti.uni-heidelberg.de/content/Goeddeke07.pdf
- Kronbichler and Ljungkvist: https://api.crossref.org/works/10.1145/3322813
- Oo and Vogel: https://arxiv.org/abs/2007.07539

**GPU sharing:**
- NVIDIA async compute: https://developer.nvidia.com/blog/advanced-api-performance-async-compute-and-overlap

## Appendix: artifacts

The benchmarks (`bench.cu`, `coexist.cu`), raw outputs, the FP64 operation-count
and per-GPU prediction scripts, and the scratch-only stage-timing patch were kept
in a temporary scratch directory. They are not tracked in this repository.
