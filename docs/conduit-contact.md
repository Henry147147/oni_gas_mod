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
