# Conserved conduit/body contact boundary

The original full-mixture pipe thermal check failed. Its independent
body-plus-mixture account includes heat that the owner world's audit does not.
Do not substitute the owner's passing audit or relax the fixed endpoint bound.

The pinned game's conduit temperature manager transfers heat through pure
native flow, clamp and temperature helpers. A private paired 601-frame trace
contains 587 contacts (1,761 math calls). Replaying all original helper calls
retains every returned binary32 bit. Managed pre-call dt, fluid/body
temperatures and registered capacities match every captured native input.
The registered pipe conductivity is the binary32 product of material
conductivity and building modifier; insulated means modifier below one.
The inspected native update supplies its fixed coefficient 50 for this contact.

The native heat sum is 90,673.992929773 J. Inferring work from the rounded
carrier temperature gives 90,674.229153523 J, while the finite body actually
loses 90,675.287708655 J. For each contact, replaying
`Tbody_after = f32(Tbody_before + f32(Q_to_body_kJ / Cbody_kJ_K))`
reproduces the complete body loss exactly, with zero difference. This is
represented contact work, not an endpoint residual correction.

`ma_oni_conduit_contact_version() == 1` exposes a read-only Linux replay for
up to 4,096 contacts. It requires the inspected native image to be attached.
The 44-byte input contains uint32 size/version/flags/reserved followed by seven
binary32 values: dt, fluid T, fluid capacity in kJ/K, fluid conductivity,
body T, registered body capacity in kJ/K, registered conduit conductivity.
Flags bit0 means insulated. The 20-byte output contains uint32 size/version,
native Q to body in kJ, expected carrier T and represented body T after contact.
It calls the original pure helpers and changes no pipe, body, owner or clock.
Invalid input, insufficient capacity, missing binding or invalid thermal range
leaves the entire output array unchanged. Other platforms report capability0.

The boundary preserves the caller's floating-point environment and errno.
Private active-image API controls match all 587 original contact outputs,
preserve all four rounding modes and exception flags, and retain every output
when the last input is invalid. Standalone backend controls cover inactive
binding, layout, capacity and invalid flags.

The managed caller must capture each physical pipe's registered inputs, retain
its held inventory, and verify the actual post-call carrier result before
settling exact `Cbody * (Tbody_before - Tbody_after)` into that mixture. Preserve
any separately observed earlier heat; do not also credit the rounded carrier
change for the same contact. Other body contacts and deferred heat cannot be
inferred from a whole-body snapshot delta. Explicit managed coupling now passes the unchanged 601-frame thermal check
5/5: represented body loss and mixture gain are both 90,674.695667622 J,
with zero residual against the original 0.245883883 J bound. Every actual
carrier result validates. This controlled result enables neither production
mixed ownership nor a new numerical profile; phase/range, full saved activity
and ordinary activation still need their own checks.

## Native body-message observation

A later heated Oceania save refuses one body still at the replay's input
temperature. Another1200-frame repeat with two saves passes, so this is not
established as a general queue-settlement fault. Inspection also shows that
the native message uses represented carrier temperature work, subject to a
posting threshold, and its actual registered body capacity. Agreement with
the carrier helpers alone does not establish every body's subsequent work.
The existing hot/cold results remain focused evidence; the general contact
account and save refusal remain open until the complete message path is proved.

`MA_ONI_BODY_HEAT_TRACE` selects a fresh private TSV path for a read-only
observation inside the existing validated native frame-message hook. It records
each queued body-energy message and the actual registered body's temperature,
capacity per occupied cell and area before message processing, plus its
temperature afterward. Float values are emitted as their binary32 hex bits.
Rows carry the native frame ordinal and validity flags (before1/after2).
Multiple messages or registration changes in one frame require separate
analysis; an aggregate body delta must not be attributed to one pipe.

The observer adds no detours, preserves the original floating-point environment
and errno, changes no inputs or native/owned state, and allocates nothing when
disabled. Layout, stale/retired handle, immutable-source and all-rounding-mode
controls pass, as do all86 native CTests. A private preload attempt was rejected
by the original-entry fingerprint guard before colony creation; the integrated
observation keeps that guard intact. No private captures or game code belong in
the repository. Instrumented runs establish no speed acceptance.

## Publishing settled body work before saves

The isolated `pipe-body-rounding` control reproduces the heated-save refusal
with one ordinary liquid pipe in vacuum. A native zero-energy probe verifies
the registered111.4000015259 kJ/K body capacity and the exact initial temperature.
The original barrier then refuses at frame38: native messages have applied the
contact work, while `Game.simData` still exposes an older body temperature.

`UnsafeSim200ms(0)` posts a native frame and calls `StepTheSim(0)` before that
asynchronous frame necessarily completes. Waiting for completion does not
itself republish GameData. Previously, the barrier called `StepTheSim(0)` again
only if owner receipts remained. It now always republishes after the wait,
including saves with no outstanding owner receipts. The thermal guard and
physics bounds are unchanged; native body/chunk callbacks must reach the saved
managed endpoint as well as the native simulator.

The same immutable-input control passes5/5 with that change. A later zero-work
probe advances native frame906→1064, retains the exact capacity/area and leaves
the body temperature unchanged; its native temperature equals the published
endpoint. All20 contacts validate,1 kg remains, and the registered body loss
and actual liquid gain both equal67.993164993822575 J, with exact residual0 J.
The original failure is retained. The longer Oceania save repeat and complete
storage/cold-continuation acceptance remain separate requirements.

An apparent two-contact helper discrepancy in an initial offline comparison
was caused by subtracting decimal log values before restoring binary32 inputs.
With the correct input precision, all19,918 observations agree; no contact-law
change is justified or made. The25 directly paired native messages also agree
with the existing helper and registered body update.
