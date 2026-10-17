# Viscosity-matched compression heat

This is a separate occupied-grid component. Its first cohort below retains
the previous closing-speed owning heat law. The later
[owning integration](MAC_MATCHED_OWNER.md) has separate evidence and a failed
strong-flow gate. No production world, game, presentation, performance or
first-playable acceptance follows from the component controls.

## Model

The preceding pressure-sensed acoustic owner passes its staged controls but
fails the original collision refinement gate. Its viscosity adds an acoustic
term while its heat conductance still uses only closing speed and fixed-volume
capacity. Saved collision profiles motivate checking this mismatch; they do
not prove its cause or authorize fitting a coefficient to the failed norms.

[Cook's primary derivation](https://www.osti.gov/servlets/purl/1224395), equations
6-9, relates conductivity to normal viscous stress by imposing constant
stagnation enthalpy through a steady ideal-gas shock. With artificial bulk
viscosity alone it gives `kappa = cp * beta`. Our compression stress is
`q = rho * v * (v + a)`, where `v` is positive normal closing speed and
`a = 0.5 * sound_speed * pressure_sensor`. Interpreting `v/h` as the compressive
strain rate gives `beta = rho * h * (v+a)`.

The optional heat law therefore uses:

- The same source-state half-sound speed, mechanical pressure and axis-specific
  pressure-curvature sensor as compression, built by one shared function.
- `Cp = Cintrinsic + nR` per cell. At constant pressure our zero-expansion liquid
  volume and elastic energy do not vary with temperature, so this is the actual
  constant-pressure heat capacity for the explicit surrogate EOS as well.
  Using the derivative Cp keeps arbitrary formation/reference energy offsets
  out of the conductivity coefficient.
- Cell diffusion speed `v+a` during positive compression, zero otherwise. The
  face takes the greater adjacent speed, retaining the earlier activation
  geometry. Rest, pure tangential shear and expansion supply no coefficient.
- Face conductance `max_speed * harmonic(Cp_left,Cp_right) / h` with coefficient
  one. Each face gets forward/reverse half steps in reversed axis order.

The thermal pair still evolves stored internal energy at fixed masses and cell
volumes. Its relaxation and mixed-EOS equilibrium solve use fixed-volume
capacities, independent of the Cp used to construct conductance. No heat crosses
walls; no species, passive, momentum or geometry is changed.

Full-range coefficients and transfers retain explicit unresolved-storage flags.
Actual U conservation, monotone temperature and entropy checks keep the existing
operand-relative bounds. There is no energy floor, pressure floor, trace cutoff
or relaxed acceptance limit.

This transfers a continuum ideal-gas relation to our existing grid and face
averaging. It does not implement Cook's high-order discretization or guarantee
constant stagnation enthalpy in this split method. Source coefficients are
frozen per component call; their nonlinear thermal dependence remains a
first-order regularization.

## Validation contract

Eight new native groups cover a periodic gas grid, wet solid corner, compressed
pure liquid, inactive rest/shear/contacts/equal temperature, positive heat below
binary64 range, frozen two-body semigroup, quadratic weak-wave amplitude and
immutable invalid requests. The saved-only review independently reconstructs
the EOS, Cp, sound speed, pressure stencil, every active face conductance,
chronological pair partition, full U/entropy accounts and unchanged traces,
passives, J and geometry. It must reject altered heat, trace, capacity, sound
tangent, sensor and geometry records. No independent-agent review is claimed.

Before any owning-method selection, strict Windows/MSVC and Linux/GCC builds
must pass all 127 groups and saved reviews (the prior 119 plus these eight),
including the original quadratic and pressure-sensed owner controls and the
original Fourier reference. Linux ASan/UBSan must pass the prior 58 selected
groups plus these eight (66 total), with fatal findings, leak detection and
instrumentation audited for every compile command. Complete checkpoint mode
identity, retained legacy images and cross-platform byte identity remain
required. Original scientific sources and thresholds stay unchanged.

Bounds fixed before invocation: aggregate strict build 180 seconds, matched
capture 120 seconds and 2 MiB per raw pipe, matched saved review 180 seconds;
all preceding ordinary bounds remain. A failed shared native case is not
replayed unchanged on the other OS. Failure receipts close before source edits.

## Component results

All 127 native groups and saved reviews pass on Windows/MSVC and Linux/GCC15.
The new review passes 44,924 checks and rejects all six corruptions on each
platform. All twelve existing checkpoint combinations, four retained quadratic
images and three new cross-platform images pass. Linux ASan/UBSan passes all
66 selected groups/reviews with empty native stderr, fatal findings and leak
detection enabled; all 92 compile commands carry the instrumentation.

The new weak-wave absolute heat divided by amplitude squared stays between
0.57282181 and 0.57282197 for amplitudes 1e-3, 1e-5 and 1e-7 on both platforms.
This is a separate amplitude control, not a replacement for the unchanged
owning Fourier reference, which also passes. The original two owner references,
compression, sensor, heat, closed acoustic and gravity captures are byte-for-byte
identical to their preceding accepted outputs: sixteen retained raw streams
across the two platforms. Saved-only byte-audit SHA256:
`fc065334963a5f65a0d6da45ef40b85cf99f04b3aaa8428592573bda830ddd14`.

Private cohorts under `local/occupied-staggered-validation`:

- `mac-matched-heat-windows-r1`, source-freeze SHA256
  `4a828245a18dfecd0de9109e5262c203ecf2ce9030da377c134fe32e1dd35968`,
  terminal SHA256
  `9d106a9763b5238039cd3273c874fa2fb8e2c8595bd19aa074825a8e75b70caa`.
- `mac-matched-heat-linux-r1`, source-freeze SHA256
  `6992060bfe6b19d587a36539e43473db412fdea4cb7fdfe5bad851262995f0da`,
  terminal SHA256
  `dbd47cc07100bfa788c70adc7bc2ad4e0d9886a5a806cf12d14061648a86a799`.
- `mac-matched-heat-linux-asan-r1`, source-freeze SHA256
  `5e5cfd2599a9a911de22f41f7a9722670056caeaea5487f98eee6240b7a1e379`,
  terminal SHA256
  `5f109190e6ee47de5b7bb8dae1859c7957e8f2410713509d018db1eed2f84080`.

The owner/default is unchanged in this cohort. The preceding strong-flow gate
remains failed and has not been retried. There is no new Unity or first-playable
acceptance. The default game DLL is unchanged.
