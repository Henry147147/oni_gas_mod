# Acoustic and quadratic compression component

This candidate adds a linear acoustic term to the separate compatible
compression component. The owning timestep still selects its original
quadratic mode; no checkpoint setting, native domain or game behavior selects
the new law. Component validation passes on both platforms, including Linux
sanitizers. Owning/strong-flow acceptance remains pending.
The latest failed [collision profiles](MAC_RIEMANN.md) show alternating pressure
behind the fronts after heat redistribution repairs most central excess entropy.
That observation motivates testing additional weak-compression damping; it
does not establish the cause of the full profile error or a successful repair.

For positive closing speed v, use `q = rho (v^2 + c v)` with `c = c_s/2`.
Expansion, uniform normal velocity and tangential shear receive no force.
The sound speed comes from the existing occupied, isentropic mixed-EOS tangent:
`c_s^2 = V^2 / (mass C)` where `C = (-dV/dp)_S > 0`. For this EOS,
`C = C_liquid + Cv/(Cv+nR) V_gas/p`. It reduces to the ideal-gas acoustic
speed and the elastic liquid's compressive speed. Internal void remains outside
this occupied-grid component's scope. Each component call freezes source-cell
speeds for both axes/sweeps; deposited heat cannot change a later pair's coefficient.
This is a first-order treatment of the speed's thermal dependence, even though
each frozen pair subflow is exact. No higher temporal order is claimed for the
temperature-dependent grid operator.

Linear plus quadratic artificial viscosity is described in the primary
[Sandia ALEGRA report, section 7.2](https://www.osti.gov/servlets/purl/934850).
The fixed linear coefficient 0.5 also appears in the primary
[LLNL LULESH source](https://github.com/LLNL/LULESH/blob/master/lulesh-init.cc).
For equal acoustic impedance, the linearized symmetric Riemann pressure
increment is `rho c_s (u_left-u_right)/2`; this supplies our coefficient choice.
Neither source certifies this split algorithm. The coefficient is fixed before
native validation and is not fitted to the collision norms. The quadratic
coefficient remains one grid spacing, as in the existing reference.

Let `B = dt area rho`, `a = 1/I_left + 1/I_right` (omit fixed endpoints),
`L = B a c`, and `phi(L) = (1-exp(-L))/L`, with phi(0)=1. Solving the frozen
pair ODE `dv/dt = -(area rho) a v(v+c)` gives impulse

`d = B v(v+c) phi(L) / (1 + B a v phi(L))`.

The compatible kinetic loss remains `Q = d(v-a d/2) >= 0`. Equal/opposite
momentum, fixed-wall impulse and cell heat use the existing actual-operand
128-epsilon accounts. Species, passive quantities, geometry and masses remain
unchanged. Full scaled impulses/heat and unresolved primary updates remain
explicit. No U, J or entropy is repaired to force closure.

For L<2^-53, phi=1 has relative truncation error below 2^-54. For L>=64,
phi=1/L omits exp(-64), below significand precision. Intermediate values use
expm1. The full reciprocal and all products retain their independent exponent;
there is no binary64 projection of a positive below-range impulse or heat.
The explicit c=0 branch retains the original quadratic arithmetic exactly.

Seven native groups are fixed before the first build:

1. Free/free, unequal-inertia, fixed/free, stiff and semigroup pair controls;
   c=0 retains the quadratic reference.
2. Below-range momenta/heat/coefficient and new target J, plus very large/small
   acoustic speeds and stiff relaxation.
3. Uniform velocity, expansion, tangential shear, wet stationary contacts and
   Galilean-compatible work.
4. Gas, mixed wet, pure liquid, periodic and solid-corner grids with actual
   U+K, wall impulse and entropy accounts.
5. Transposed grids and a positive heat transfer too small to change stored U.
6. Gas/wet/liquid acoustic speeds compared with independent isentropic volume
   perturbations, with relative discrepancy below 1e-5.
7. Invalid coefficients, law, shapes, projections, timestep and fixed momentum
   reject without changing source state.

The saved-only review independently solves the pair ODE in 80-digit arithmetic,
replays every grid event, reconstructs mixed-EOS sound speed, full state,
species/passives, actual U+K, entropy and wall momentum, and rejects altered
impulse, heat, trace and acoustic-tangent records. Its finite-difference control
uses the saved perturbed pressures. All previous physical limits remain fixed.

Per-platform bounds before first invocation: aggregate strict build 180 seconds,
new native control 120 seconds, saved review 180 seconds, new raw pipes 2 MiB.
Existing prerequisite caps and limits are retained. Both platforms must also
pass the existing quadratic component and owning regressions; Linux sanitizer
coverage is required. A failed strong-flow gate may only be retried after an
owning method change and all its applicable prerequisite reviews. No unchanged
strong failure is replayed from this standalone component.

## First range failure

Both first strict builds pass. The Windows native capture completes the
analytic/semigroup group and the first two range pairs, then rejects the large
linear-speed case at the existing primary-field binary64 upper-range guard.
The new derived sum `c+v` mistakenly uses the helper for committing canonical
momentum. Neither input momentum nor the resulting bounded impulse exceeds
that limit. Linux retains its completed strict build and does not repeat this
known shared native failure.

The repair keeps the derived positive coefficient sum in full range before
forming its bounded impulse. Actual primary momentum updates still use the
original guard, and no upper storage limit or scientific input is relaxed.
Private first cohorts `mac-acoustic-compression-{windows,linux}-r1` retain
terminal SHA256 values
`e07d2247721b2f260f7d0cc3731904f19f2c5968ca27f4eeb05c39e046ed6c41`
and `6cc6dc85ce9b25ea3412e0bb847f51af7c0eeaaf61740c78a546d6f275f15264`.

## Component results

All seven new groups pass on Windows/MSVC and Linux/GCC15. Each saved-only
review passes 12,971 checks and rejects all four impulse/heat/trace/tangent
corruptions. Gas, wet and liquid sound speeds agree with isentropic finite
differences to relative discrepancies 6.68e-11, 8.20e-11 and 5.05e-11 on
Windows, below the predeclared 1e-5 finite-difference limit. The independent
mixed-EOS reconstruction also passes its original 5e-10 relative comparison.

Both platforms pass all 110 native groups and their saved reviews: the new
seven and all 103 retained prerequisites, including the quadratic component,
heat/compression owners, original weak-acoustic/gravity controls and physical
checkpoint. The Linux ASan/UBSan build passes 49 groups and reviews with empty
native stderr, fatal findings and leak detection enabled. Its approval audits
sanitizer flags in every compile command, including the static dependencies.

Private accepted cohorts under `local/occupied-staggered-validation`:

- `mac-acoustic-compression-windows-r2`, source-freeze SHA256
  `beaf93e779643bd9cfe28d66d4cd44b255bb2cd6bb843019bd66c2c3cdc56877`,
  terminal SHA256
  `9001697a2e08678d7eb4f9d342de8ac7e44f75062b4d90ab562b875bad5c7e6e`.
- `mac-acoustic-compression-linux-r2`, source-freeze SHA256
  `2b8a5c0c544445114d0ad77a2c9a839d1142bf9f63fac022a2eda8d3b27210e1`,
  terminal SHA256
  `f7cc33bc9a266263f39394b42ab3da1b3d64bc4946738a14e3247908b6d5b376`.
- `mac-acoustic-compression-linux-asan-r1`, source-freeze SHA256
  `08706df8255686263a345d540a723599bf3ef46ff9e712f8d93c843e6ac39e67`,
  terminal SHA256
  `14a3b141a6e3626b75c5dc4da1b4ddac430ed0181c16b28e68b14aa107964261`.

The original Riemann/gravity sources and physical bounds remain unchanged.
The owning timestep still selects quadratic compression and default shock
heat. No strong-flow retry, game dispatch, Unity, production, independent-agent
or performance acceptance follows from these component controls. The default
game DLL remains unchanged. Owning integration must preserve explicit
quadratic/disabled references and checkpoint settings, review its complete
staged states and pass the original scientific gates before game acceptance.

The post-result source review confirms 182 native/test/build files remain
identical to the accepted sanitizer capture; the header differs only in
comments clarifying the law and its coefficient freezing. Review SHA256:
`f94b5c1b9b1a5d101120ac0ad8cc390a41123767ed0a82e74d64532bc9e744af`.
