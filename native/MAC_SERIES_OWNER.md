# Owning heat through two half-cell resistances

Optional persisted heat mode 3 (`pressure_sensed_series`) passes all 147 strict
Windows/Linux groups and 86 selected Linux sanitizer groups in its owning
trial. The original strong-flow gate then fails collision density and pressure
refinement, with worse ratios than mode 2. The default is restored to mode 2;
mode 3 requires explicit selection. Restored-default compatibility validation
passes all 147 strict groups per OS and 86 selected sanitizer groups. There is
no production, game, first-playable or performance
acceptance from this experiment.

## Selection and saved identity

Each heat half step selects the [series-resistance component](MAC_SERIES_HEAT.md)
when mode 3 is explicitly requested. Owner mode 3 maps to component law 2;
default owner mode 2 still maps to the
previous Cp/max-speed law 1. Owner modes 0 and 1 retain disabled heat and the
original closing-speed law. The persisted owner enum and component enum have
different meanings and are never cast into each other.

The owning order remains optional physical L/2, H/2, C/2, pressure/material/
momentum P, C/2, H/2, optional L/2. Each H call freezes its own incoming conserved
state. Heat/compression diagnostics, state, clock and revision are published
only with a complete accepted interval. A later failure leaves the original
owner unchanged. Physical conduction remains separately selectable.

Wire format 2 already stores the complete heat enum. Appended enum value 3
selects the new method; values 0/1/2 keep their previous meanings and image
layout. Old images never inherit a later default. Earlier readers reject the
unknown value. No registry/EOS identity or other physical setting changes.

Continuation must cover all 24 gas/wet, four-heat and three-compression
combinations, including every conserved field, clock and diagnostic. Each OS
must also reserialize ten retained images, including preceding mode-2 images,
and read the other platform's completed mode-3, heat-disabled and quadratic
images byte for byte. Writers must finish before cross-read admission.

## Validation contract

Six new owning groups exercise a periodic wet grid, a wet solid corner,
physical conduction with both regularizers, multiple accepted substeps,
positive below-range source heat, and disabled/invalid/later-failure/recovery
controls. The subrange source has equal pressure and two adjacent compressed
cells, so its reciprocal conductance remains positive without an acoustic
sensor contribution. All trace and passive inventories remain present.

The saved-only reviewer reconstructs stage order, incoming-state continuity,
Cp, sound, pressure sensors, both half-cell resistances, heat, EOS, U+K, wall
reaction, trace/passive and entropy accounts. It must reject clock, chain,
trace, heat, tangent, compression law, capacity and heat-setting corruptions.
The three preceding owner methods remain explicit invocations; their six
Windows/Linux raw captures must remain byte-for-byte unchanged.

Before a changed-method strong-flow retry, all 147 groups/reviews (the prior
141 plus six) must pass strict MSVC and GCC builds. This includes the incoming
filled-liquid boundary and registered water-actor controls. Linux ASan/UBSan
must pass all 86 selected groups/reviews, with fatal findings, leak detection
and every compile command instrumented. Original Fourier/acoustic, gravity
and Riemann sources, inputs, grids, timesteps and physical thresholds remain
unchanged. No identical failed strong-flow method is repeated.

The restored-default pass must repeat all 147 strict groups/reviews, 24 setting
combinations, ten retained images and cross-platform reads, plus all 86 selected
Linux sanitizer groups/reviews. The three previous owner captures and legacy
heat-disabled/quadratic images must remain unchanged. Explicit mode-3 captures
and written images must also match the completed r2 trial exactly. No new
strong-flow retry is authorized by restoring the unchanged mode-2 method.

Bounds fixed before invocation: aggregate strict builds 180 seconds; owning
native capture 120 seconds and 4 MiB per raw pipe; saved owning review 180
seconds; checkpoint operations 60 seconds. Other preceding stage limits are
retained. The original strong-flow deadline remains 180 seconds with 2 MiB
per raw pipe. Every failure is closed before source edits. The default game
DLL stays unchanged. These are correctness bounds, not performance evidence.

## Retained first review failure

Both first owning native runs pass all six groups. Their saved reviews then
reject the new subrange fixture's one-row geometry at the existing compression
reviewer's `width > 1 && height > 1` contract. Those attempts stop without
running the remaining prerequisites or retrying the strong-flow gate. The
fixture is corrected to a four-by-two closed grid, retaining its equal source
pressure, adjacent compressed cells, positive below-range heat and trace.
No reviewer geometry or physical tolerance is relaxed, and the heat method
is unchanged.

The private `mac-series-owner-{windows,linux}-r1` failed terminal SHA256 values
are `b15e4840e387df545d9264b0ca8a2950a193469f4c8b2bceb30b591f6889c3e0`
and `c3d3a5e52c8f06cdff0c95cc05a7e1b54941947c93d9044a088f7a88efb6b048`,
respectively. The corrected fixture uses new r2 cohorts on both platforms.

## Completed trial and failed strong-flow gate

The corrected r2 trial passes all 147 strict groups/reviews on each OS and 86
selected Linux ASan/UBSan groups/reviews. The new owner review checks 107,445
identities on both platforms and rejects all eight declared corruptions.
Twenty-four checkpoint combinations, ten retained images and all cross reads
pass. All six preceding heat-owner captures and four legacy written images
remain byte-for-byte unchanged. Sanitizer native stderr is empty; fatal findings
and leak detection are enabled and all 93 compile commands are instrumented.

One changed-method Windows strong-flow retry passes Sod at N64/128/256, then
fails collision refinement at N128. Density, pressure and velocity error ratios
are **0.9193097306 / 0.8978919659 / 0.5974023544** against 0.85. The preceding
mode-2 ratios were 0.8561655676 / 0.7963761586 / 0.6110308413. The new law does
not improve this gate. Collision N256 and separation do not run; Linux does not
repeat the shared failed method. A saved-only partial review verifies 59,205
identities across five completed profiles, not full strong-flow acceptance.

Private failed evidence is under `mac-series-owner-strong-windows-r1` in
`local/occupied-staggered-validation`. It exits 1 after 60.99 seconds, without
timeout or pipe overflow. Terminal SHA256 is
`13d0bc28dc2b0d1cf64cc2cd0602dba13b0a6ab9ed205581df0e2746b32d825e`;
source freeze SHA256 is
`57aa7ee4722f8171aa54aca487745718dce3f976f6f47ceb93062e413e404d53`.
The default game DLL is unchanged and no game run or independent-agent review
occurs in this trial. This result motivates restoring the previous default,
while retaining mode 3 for explicit, reproducible investigation.

Accepted trial terminal SHA256 values are:

- Windows r2: `df7d38e96bbf46d59cbed1e92c57a65f279512bcc97f3a60591c0b1aa82b9547`.
- Linux r2: `43148be1a4554a679ce12180155aca983df8426962ad6d850381118bbca4947c`.
- Linux ASan/UBSan r1: `c6de9cb242afb985bf76e3830f60c9524a4e06c7d0d599514703abc6b3b9b43b`.

The preceding-reference byte audit SHA256 is
`87547dc6882b7e38e260785679bc0e275b2fcaa85ab962e69a7c856065be0b60`.

## Restored-default compatibility results

New Windows/Linux r3 cohorts pass all 147 native groups and saved reviews with
mode 2 restored as default and mode 3 invoked explicitly. All 24 setting
combinations, ten retained images and three cross-platform reads per OS pass.
The new Linux ASan/UBSan r2 cohort passes all 86 selected groups and reviews
with empty native stderr, fatal findings, leak detection and all 93 compile
commands audited for instrumentation.

The six earlier heat-owner captures and four legacy written images are exactly
unchanged. Both explicit mode-3 captures and all six images written in the r2
trial are also byte-for-byte unchanged. The combined saved-byte audit SHA256 is
`877347b1951977b7dbd36f9342db7308d1fffcb51762cd071e681a1af55d44ac`.

Terminal-review SHA256 values are:

- Windows r3: `bcd8a4ffbc6c536065abdd7f3156c4ff136ec18ef0b21c507ba413a121639d88`.
- Linux r3: `4a76f0e2a01c6911fe13a896c8405dd8457e95c43bc2e6a67401625cedc5f5ca`.
- Linux ASan/UBSan r2: `e38083400a1f361fbd7ddf83b87e1ef8433023b3f6f433d20787131cb1797a02`.

No further strong-flow retry occurs after restoring the unchanged mode-2
method. Its preceding density-refinement failure remains a failure. No game
DLL, source input save, released package or Workshop item is changed by these
standalone controls. The experiment does not complete the overhaul.

## Following liquid-conduction integration

The incoming conservative liquid-conduction correction changes represented
physical heat updates and their diagnostics. After integrating it, repeat the
28 affected groups/reviews on Windows and Linux: all four heat-owner references,
physical conduction, point transfers, native-domain scheduling and checkpoints.
Repeat all 24 saved-setting combinations, ten retained image reads and three
cross-platform reads per OS, plus the same 28 selected Linux sanitizer groups.
The existing compiler, pipe and per-stage bounds remain unchanged. The r4
integration cohorts pass all 28 affected groups/reviews on both platforms;
completed r3 evidence above predates this fix.

Old byte-equality receipts retain their earlier source scope. A legitimate
change to represented physical conduction can change these newer physical-L
accounts. Original Fourier, gravity and Riemann inputs remain unchanged; this
merge does not authorize another identical failed strong-flow retry. There is
no new game or first-playable acceptance from these numerical controls.

All four owner references pass their saved-only reviews with 107,445 new-law
checks per OS, the preceding corruption checks intact, and no physical tolerance
changes. The direct physical-conduction control includes 2,400 small-water
steps with exact represented component energy, insulation and explicitly
reported unavailable rounding. All 24 checkpoint combinations, ten retained
images and cross-platform reads pass again. Linux ASan/UBSan r3 passes the same
28 affected groups/reviews with empty native stderr, fatal findings, leak
detection and compile-command instrumentation audit.

Post-merge terminal SHA256 values are:

- Windows r4: `ed6d2d7c402e14dce0082343ce030f1f3342031aa31e9454c486c3f16c337ea6`.
- Linux r4: `edbbe7549753b0411315ec146efe3133ff02eb19b2280cc3938d403dd5ace6c0`.
- Linux ASan/UBSan r3: `ad062ade73b567ef60e8e0b7b8dc689714116adff97a18c5003deff67dfff656`.
