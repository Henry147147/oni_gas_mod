# Owning viscosity-matched heat

The closed occupied owner now selects heat mode 2
(`pressure_sensed_acoustic`), paired by default with pressure-sensed acoustic
compression. Its scoped owning controls pass; the changed-method strong-flow
retry still fails collision density refinement. No production, game,
first-playable, performance or release acceptance follows from this selection.

## Selection and atomicity

The [separate thermal component](MAC_MATCHED_HEAT.md) first passed 127 strict
Windows/Linux groups and 66 selected Linux sanitizer groups. Its coefficients,
capacity model, pair relaxation and physical limits are unchanged here.

The owning order remains optional physical L/2, H/2, C/2, pressure/material/
momentum P, C/2, H/2, optional L/2. Mode 2 chooses the new Cp and pressure-sensed
diffusion-speed coefficient for each H call. Its sound speed and pressure
stencil come from that call's actual incoming conserved state. The following
C call freezes its own incoming state using the same coefficient construction.
These separate frozen calls do not imply an exact nonlinear or second-order
thermal coupling.

All stages and diagnostics stay in the local candidate. Only an accepted
substep contributes state, heat, roots, unresolved updates, entropy, energy
bounds and wall momentum to the completed interval. A later interval failure
does not publish a partial candidate or advance the original clock.

Heat mode 0 stays disabled; mode 1 retains its original closing-speed,
fixed-volume-capacity conductance. Physical conduction remains a separate
setting. The heat law can be selected independently of compression for isolated
comparisons; its viscosity matching describes the default mode-2/mode-2 pair.

## Saved setting identity

Wire format 2 already stores the complete heat enum. Its values 0 and 1 keep
their original meanings; new value 2 selects this method. No old image silently
inherits the new default. Older readers reject the unknown enum. The image
layout and all preceding physical settings are unchanged.

Complete continuation must cover eighteen gas/wet, three-heat and
three-compression combinations. Both platforms must also reserialize the four
retained quadratic images and four preceding acoustic/legacy-heat images, then
read each other's newly written mode-2, heat-disabled and quadratic-reference
images byte for byte. Both writers must finish before cross-read admission.

## Validation contract

Six new owning groups cover a moving periodic wet grid, wet solid corner,
physical conduction plus both regularizers, multiple accepted substeps, positive
below-range initial heat, and disabled/invalid/later-failure/deterministic-recovery
controls. The below-range case uses equal source pressure so the pressure sensor
is exactly zero, retaining the quadratic full-range heat control. The preceding
quadratic and acoustic/legacy-heat references remain explicit invocations.

The saved-only owner review reconstructs every stage and all Cp, sound,
pressure-sensor, conductance, EOS, U+K, trace/passive, wall and entropy accounts.
It must reject clock, chain, trace, heat, compression tangent/law, conductance
capacity and heat-setting corruptions. The previous owner reviewers retain
their original corruption checks. No independent-agent review is claimed.

Before one changed-method strong-flow retry, all 133 native groups/reviews must
pass strict Windows/MSVC and Linux/GCC builds: the preceding 127 plus these six.
Linux ASan/UBSan must pass 72 selected groups/reviews (the prior 66 plus six),
with fatal findings, leak detection and every compile command instrumented.
Original acoustic/Fourier, gravity and Riemann sources, inputs, grids, timesteps
and physical thresholds remain unchanged.

Bounds fixed before invocation: each aggregate strict build 180 seconds,
owning native capture 120 seconds and 4 MiB per raw pipe, saved owning review
180 seconds; checkpoint operations retain 60 seconds and all other preceding
bounds remain unchanged. The original strong-flow run keeps 180 seconds and
2 MiB per pipe. A failed shared strong case is not repeated unchanged on the
other OS. Failure receipts close before source edits.

## Owning results

All 133 native groups and saved reviews pass on Windows/MSVC and Linux/GCC15.
The new owner review passes 133,225 / 133,219 checks and rejects all eight
corruptions. The explicit acoustic/legacy-heat reference passes 131,223 checks
on each OS; the quadratic/legacy-heat reference passes 126,478 / 126,472.
All eighteen checkpoint combinations, eight retained images and three new
cross-platform images retain their complete settings and bytes.

The original Fourier reference passes both platforms: its maximum normalized
deviation is below 6.75e-7 across N16/32/64 against the unchanged 5e-5 limit.
Gravity and all preceding physical controls pass. Fourteen component and legacy
owner raw output streams are byte-for-byte unchanged from the component cohort.
Saved-only byte-audit SHA256:
`1c2a8637c7037457e8fd749b420dafc9c00a255e7ad15f9b6fb5d96c54941469`.

Linux ASan/UBSan passes all 72 selected groups/reviews with empty native stderr,
fatal findings and leak detection. All 92 compile commands are audited for
instrumentation. These runs do not weaken scientific inputs or physical bounds.

An early Linux driver admission was rejected before its asynchronous compiler
audit completed. It launched no native process and created no first native
capture. Actual test admission used the completed audit receipt; the private
record is `local/mac-matched-owner-linux-approval-admission-r1.json`.

Accepted private cohorts under `local/occupied-staggered-validation`:

- `mac-matched-owner-windows-r1`, source-freeze SHA256
  `dd29a32636566dbe0cff671d4d29773b2220f7624eccda1e054d833a8b194594`,
  terminal SHA256
  `270abe5db3af7fe42320265771d0a64757d38c4e4fb02072fe6fb42da3e05b2b`.
- `mac-matched-owner-linux-r1`, source-freeze SHA256
  `b9a718e064bcc2811258052b65aec865d018571c68ce5ba77a5b903458a40e1c`,
  terminal SHA256
  `f1b7a4a4a52a31b4a2b664148bd9845707250f7a1429bb10fba8ef5c4e860744`.
- `mac-matched-owner-linux-asan-r1`, source-freeze SHA256
  `30a25954f0ec9e6c0bfb2e428da53072d503906ebff92f1690fce4677ea41580`,
  terminal SHA256
  `11845bb4d8b86876795919c0fdd2f4c9e9105b760a639a2dae666fe5e3db17f0`.

The one changed-method Windows strong-flow retry passes Sod at all three
grids and collision pressure/velocity refinement, then fails collision density
refinement: ratios 0.856166 / 0.796376 / 0.611031 against 0.85. Its original full
gate remains failed; see [profiles and limits](MAC_RIEMANN.md). Linux does not
repeat that shared failure. No collision N256 or separation result is claimed.

The post-result source audit confirms all 187 native/test/build files match the
retained strong-flow source exactly. Audit SHA256:
`c51dd39605d42049629d5fc943a75986803846eb5112321885496e5f7a69a867`.
Only documentation changed when that audit was recorded. A subsequent shared
merge adds restored-frame capture and Linux build provenance outside these
standalone controls. The post-merge audit finds 186 of those files unchanged;
only `oni_backend.cpp` differs, in paired restored-frame admission. MAC solver,
thermodynamic and scientific sources remain exact. Post-merge audit SHA256:
`ac16894cd55f3553621bf2bf3bde22e289cf09235778c78cd5bcb218f068467a`.
The incoming runtime changes need separate integration evidence. There is no
new Unity, first-playable, production or performance acceptance. The default
game DLL is unchanged and is not deployed by these standalone controls.
