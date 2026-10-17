# Owning acoustic compression stages

The closed occupied owner now selects the tested
[acoustic/quadratic component](MAC_ACOUSTIC_COMPRESSION.md), with the
[pressure-curvature sensor](MAC_PRESSURE_SENSOR.md), by default. Its
staged owning controls pass; the original strong-flow gate still fails and
real-game acceptance remains pending. Explicit
`none` and `quadratic` settings remain available. The primitive pressure and
transport component still excludes the owning regularization stages.

Each attempted substep retains the existing order: optional physical conduction
half-step, default shock heat half-step, selected compression half-step,
pressure/material/momentum transport, selected compression half-step, shock
heat half-step, and optional physical conduction half-step. Each compression
call freezes sound speeds and axis-specific pressure sensors from its actual incoming state. This does not
claim second-order treatment of their thermal dependence.

All candidate state and stage accounts stay local until acceptance. Rejected
attempts contribute no committed state, clock, wall impulse, heat or counters.
The existing full U+K(+PE), positive constituent/passive, momentum/wall and
actual stored-U entropy bounds remain unchanged. No additional floor, clipping,
state repair or error allowance is introduced.

The previous compression-only and momentum-only manufactured comparisons now
select their original quadratic law explicitly. The heat-owner fixture runs
both its retained `--quadratic-compression` reference and the new default law.
The original acoustic, gravity and Riemann scientific sources, inputs, grids,
timesteps and physical limits remain unchanged and exercise the new default.

Six default owning groups reuse the complete heat-owner scenarios:

1. Moving periodic wet mixture and exact component-stage replay.
2. Wet solid corner and full wall momentum.
3. Simultaneously active physical conduction and both regularizers in gas.
4. Two accepted substeps with one completed interval revision.
5. Positive initial heat below binary64 range and explicit unresolved storage.
6. Heat-disabled reference, invalid configuration, late failure after a completed
   candidate, unchanged source/clock and deterministic recovery.

Every compression stage exports its actual frozen source speeds, pressures,
axis-specific sensors and pair
relaxation operands. The saved-only review independently reconstructs the mixed
EOS sound speeds, exact pair impulse/work and chronological stage application.
The complete frame, composition/passives, pressure/material/momentum stages,
physical conduction, U+K, wall, entropy, counters and clock retain their prior
checks. The new default review must reject altered clock, stage chain, trace,
heat, acoustic tangent and selected law; the quadratic review retains its four
original corruptions. No independent-agent review is claimed.

## Checkpoint setting identity

Wire format 2 already stores the compression enum as an unsigned 32-bit field.
Its unchanged values are 0 (`none`) and 1 (`quadratic`); the new value 2 selects
`acoustic_quadratic` with pressure sensing. The layout and existing values retain their meanings, so
format 2 needs no layout migration. Readers validate the enum, and old readers
reject value 2. Version 1 still rejects because it lacks the separate heat
setting. No missing setting is inferred from a new default.

The checkpoint control must retain and exactly continue all twelve gas/wet,
heat-enabled/disabled and compression-none/quadratic/acoustic combinations.
It compares full next-state and heat/compression diagnostics. A valid-checksum
unknown compression enum must reject. Both new platforms must also reserialize
the retained format-2 quadratic images from the earlier verified checkpoint
cohort, and read each other's new acoustic/heat-disabled/quadratic images
without changing a byte. Cross-read processes start only after both writers
have completed and the dependency hashes are recorded.

## Validation gate

Before one changed-owner strong-flow retry, strict Windows/MSVC and Linux/GCC
must pass all 119 native groups and saved reviews: the 110 existing groups,
including the explicit quadratic heat-owner reference, six new default
owner groups and three pressure-sensor groups. Linux ASan/UBSan must pass the
prior 49 selected groups plus the six new owning and three pressure-sensor
groups (58 total), with fatal findings and leak detection enabled. Every
compiled command must carry the sanitizer flags. The original scientific
source hashes and physical thresholds remain fixed.

Bounds fixed before first invocation: each aggregate strict build 180 seconds,
each owning capture 120 seconds with a 4 MiB raw-pipe cap, each saved review
180 seconds, and existing ordinary native/review bounds unchanged. Checkpoint
fixture writes/reads retain 60 seconds each. The original strong-flow run keeps
180 seconds and 2 MiB per pipe. These are bounded correctness runs; they are
not production performance evidence. A failed shared strong-flow gate is not
repeated unchanged on the other platform.

There is no new production world ownership, vacuum/free-surface, phase,
material-catalogue, gameplay, renderer, colony save/reload, Windows/Linux Unity
or release acceptance from these standalone controls. Those remain required
under the completion plan. The default game DLL is not deployed by validation.

## First owning failure: weak-wave damping

Both first strict builds and the six new owning groups pass. Their data reviews
pass 125,004 / 125,000 checks and all six corruptions. The retained quadratic
heat-owner reference and earlier range/transport/reconstruction controls pass.
Both native closed-owner controls also pass joint pressure/velocity refinement.
The saved closed-owner review then rejects the original pointwise Fourier
profile criterion. This is a method failure, not a reason to change the oracle.

Saved-only Windows analysis finds maximum normalized pressure/velocity
deviations 0.114163 / 0.117754 at N16, 0.068811 / 0.075859 at N32, and
0.037179 / 0.039268 at N64, against the unchanged 5e-5 bound. The unsensed
coefficient damps the linear signal. The source above adds pressure sensing
to remove that first-order contribution. Its new validation is pending.

Private first cohorts `mac-acoustic-owner-{windows,linux}-r1` retain terminal
SHA256 values
`17a46303135b09c90d87510808bd3c29142260e2008e6bfceb2cd10efe72881b`
and `dd508c55d7ae2b5e2de4c4f25e87d184634a0f6b5217ba274b4f910b8e95de98`.
The data-only analysis is `local/mac-acoustic-owner-first-fourier-failure.json`.
Both cohorts stop before gravity, checkpoint/legacy/cross-image controls,
sanitizers or strong-flow. In particular, they write no accepted mode-2
checkpoint image. The last completed 110/49 component cohort and original
failed heat-owner strong-flow result remain separate evidence.

## Pressure-sensed owning results

All 119 native groups and saved reviews pass on Windows/MSVC and Linux/GCC15.
The new default owner review passes 126,803 checks and rejects all six altered
clock/chain/trace/heat/tangent/law records on each platform. The retained
quadratic heat-owner reference passes 122,058 / 122,052 checks and its four
corruptions. The pressure-sensor component passes 14,832 checks per OS.

The original weak-wave Fourier profiles now agree within 6.80e-7 of signal
amplitude across all three grids on both platforms, below the unchanged 5e-5
limit. Continuous pressure/velocity errors still refine jointly. Original
gravity, all range/transport/owner controls and twelve checkpoint combinations
pass. Both platforms reserialize all four retained Windows/Linux quadratic
images, then each other's three newly written acoustic, heat-disabled and
quadratic images without changing a byte. Completed writer receipts and all
six dependency hashes are recorded before the cross-read processes start.

Linux ASan/UBSan passes all 58 selected groups and their reviews with empty
native stderr, fatal findings and leak detection enabled. Every compile
command is audited for instrumentation. These controls preserve the original
scientific sources, physical limits, primary storage limits and energy/entropy
allowances.

Private accepted cohorts under `local/occupied-staggered-validation`:

- `mac-acoustic-owner-windows-r3`, source-freeze SHA256
  `6081378573b673218eff3b0286d35ee9d5b254a39c6252b6296338b72f8ed231`,
  terminal SHA256
  `9022cd5465b1d40773a19314ca49a1b15282599676c134a1b06626b0ffab257f`.
- `mac-acoustic-owner-linux-r3`, source-freeze SHA256
  `2673f075995540f4e8bf488dab65c6c883085739bfa2f3cf40663f2d84b9425f`,
  terminal SHA256
  `ef7d3545762ebcc69f23587a000a5d575e58ebeca103ba19b84f2ad2c30ef8ee`.
- `mac-acoustic-owner-linux-asan-r1`, source-freeze SHA256
  `e7925e7a4a44c43c361eaa0853d572e4b40df3cb71019ff866367887cdbef250`,
  terminal SHA256
  `c0eda4812fb119e20f69487867eaa16b74883e17e5c30468f5389ab5d48fb926`.

The one changed-source Windows strong-flow retry then passes Sod at all three
grids, but collision N128/N64 density and pressure ratios 0.859942 / 0.881544
still exceed 0.85. Its original full scientific gate remains failed; see
[the retained profiles](MAC_RIEMANN.md). Linux does not repeat this shared
failure. The current heat regularization still uses its original closing-speed
conductance. The additional acoustic viscosity has no corresponding added
thermal-conductance term in this cohort; that coupling remains a possible
method issue to investigate from the retained states. No coefficient is fitted
to these failed norms.

No independent-agent, Unity, first-playable, production, performance or release
acceptance follows. The default game DLL remains unchanged.

The subsequent optional [viscosity-matched heat component](MAC_MATCHED_HEAT.md)
passes 127 Windows/Linux groups and 66 selected sanitizer groups. Its shared
coefficient extraction leaves both owner reference outputs byte-for-byte
unchanged. It is not selected by that component cohort and supplies no new
strong-flow result.

The post-result review confirms all 185 native implementation/test/build files
match the retained strong-flow source exactly; only documentation is updated.
Review SHA256:
`a623d414bc2d6dd940beb485e276bbcdcbf5b354bae38265440d5a4e214dffd8`.
