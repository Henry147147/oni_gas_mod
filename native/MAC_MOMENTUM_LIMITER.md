# Compatible momentum correction

The scoped component controls pass on Windows/Linux, outside default builds and world/game
dispatch. It is the energy/positivity foundation for spatial momentum
reconstruction; the closed owner's normal transport is still first order and
the existing strong-flow gate remains failed. It does not supply a new pressure
solver, phase interface, vacuum support or a production timestep.

Build the explicit `ma_mac_momentum_limiter_tests` CMake target. Its output is
the input to `native/tests/review_mac_momentum_limiter.py <raw-output> <new-review.json>`;
use a fresh output directory and the bounded capture limits below. Visual Studio
places the executable under the build's `Release/` directory. The target is
excluded from the default build and is not a CTest production acceptance gate.

For dual inertias I_L/I_R, a requested impulse d removes d from J_L and adds it
to J_R. Let u=J/I and W=(u_L+u_R)d/2. The corresponding remainder changes are
Q'_L=Q_L+(u_L-u_R)d/2-d²/(2I_L), and similarly with I_R. Then each side obeys
Δ(K+Q)=−W/+W. Q is the existing upwind mixing remainder before material heat
deposition. No stored internal energy is borrowed, and no floor is introduced.

For a fraction a of the requested correction, write the heat change as
a*A−a²*B, with B>=0. Since 0<=a<=1, Q+a*(A−B)>=0 is a sufficient constraint.
The implementation uses the minimum of 1 and each positive Q/(B−A), retaining
the full significand/exponent representation. An exact zero budget may impose
zero correction. Up to 64 significand ULP backoffs handle a rounded boundary;
unresolved positive updates are reported. Pair/batch momentum and kinetic-plus-
remainder accounts retain 128 epsilon of actual represented operands.

Before the first build/run, six native control groups are fixed:

1. Analytic finite pairs, both correction signs and zero correction.
2. Positive-budget limiting, zero-budget antidiffusion rejection and a
   dissipative correction that creates nonnegative heat without an initial floor.
3. Reflected pairs and an exact 1024 m/s Galilean shift, with unchanged heat and
   the correct shifted energy flux.
4. Below-binary64 impulses and limiter fractions, retained full endpoint J,
   and explicit reporting when primary updates cannot resolve a tiny correction.
5. A complete period of positive periodic scalar momentum advection at C=0.2,
   32/64/128 cells, five times the cell count in timesteps, inertia 1/N, and
   initial u=1+0.2 sin(2πx). Minmod momentum corrections accompany unchanged
   donor material heat transport. Both error ratios must be below 0.35; N128
   error below 0.3 of its original donor result. Every heat remains nonnegative,
   U stays positive, and the original total energy account must pass.
6. Six invalid inertia/heat/edge inputs reject without changing their inputs.

A separate saved-data review must replay all exported pair identities with
exact fractions, check positivity, reflection/boost and range controls, and
recompute each smooth profile norm against its one-period initial data. Altered
impulse, heat and profile records must reject. First failures are retained.
Initial bounds: strict build 180 s; native control 120 s; saved review 60 s;
512 KiB per raw pipe. The existing energy, exponent and transport prerequisite
controls/reviews remain required. No strong-flow retry follows before a genuine
world integration and all of its prerequisite gates.

The need for a compatible kinetic/internal-energy balance in staggered schemes
is discussed by [Herbin, Latché and Nguyen](https://numdam.org/articles/10.1051/m2an/2017055/).
This newly derived pair limiter is not certified by that paper; smooth tests
alone also do not establish strong-flow or multidimensional Euler convergence.

## Results and integration boundary

Strict Windows/MSVC and Linux/GCC15 builds pass all six new groups, followed by
420 exact-fraction saved checks and three corruption rejections per platform.
The existing energy, exponent and regular MAC transport controls/reviews also
pass: 26 native groups per platform in total. Their saved checks remain
21,019/8,560/3,888. The six new groups and saved review additionally pass Linux
ASan/UBSan with fatal findings enabled, leak detection enabled and empty native
stderr. No independent agent review is claimed.

| Cells | Corrected one-period velocity L1 | Ratio to preceding grid |
| --- | --- | --- |
| 32 | 0.0107951145673 | — |
| 64 | 0.00327739832241 | 0.303600 |
| 128 | 0.000927664717294 | 0.283049 |

The original N128 donor result is 0.0147791188305. All unchanged 0.35 refinement,
0.3 donor-comparison and 128-epsilon endpoint-energy limits pass. These are
scalar advection controls at fixed CFL, not a full Euler accuracy result.
Positive corrections and limiter fractions below binary64 remain represented;
unresolved primary changes are reported. The limiter uses no concentration,
mass, internal-energy or pressure floor.

The first Windows build's missing namespace brace and the next run's misuse
of the nonnegative-share API for a signed product are retained. The repaired
signed product does not project its multiplier or change the physical limits.
No strong-flow retry occurred: the closed owner still needs geometric velocity
reconstruction, compatible work-flux insertion before heat deposition, wall
handling, complete prerequisite controls and the original Riemann gate.
The default game DLL is unchanged, and this component has no C ABI dispatch.

Private source/build/capture/review records are under
`local/occupied-staggered-validation/mac-momentum-limiter-{windows,linux}-r3`
and `mac-momentum-limiter-linux-asan-r1`. Completed review SHA256 values:

- Windows: `5265878278e49aa590d17ecb488d781f54e2daaff1deffa38f9774d0bae022cb`.
- Linux: `f42f92f282ae4d867b34af7dbeef274318f748190e635896cd117d14a36da6f9`.
- Linux ASan/UBSan: `06dd0adfe0eb309d2232b6d4e047a90677d1cc3a3bf143e6ff91d81d38dfc262`.
