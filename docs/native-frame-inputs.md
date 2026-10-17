# Accepted game inputs and saved fluid continuation

The native fluid owner records successful external heat and mass reservations
in the order it accepts them. This is part of the game's simulation boundary:
a saved fluid image alone cannot reproduce the next physical frame when a
duplicant or device submits another command after loading.

Each completed worker frame publishes one `MAFRIN01` journal alongside its
physical state. It contains the source session/frame, completed frame, physical
time, actual timestep, domain geometry, SHA-256 of the complete source snapshot,
and ordered heat/transfer batches. Original callback identifiers and radius
flags are retained. Batch boundaries matter because one heat batch makes one
physical revision. Failed reservations add no entries. The journal is bounded
to 1 MiB, 2,048 batches and the live limits of 4,096 heat and 4,096 transfer
commands per frame.

Journal staging shares the native frame/state locks with the actual
reservation. Allocations and checks finish before publishing either candidate.
The worker prepares its journal before committing its new physical state;
failure publishes neither. A new session discards previous journals. Copying
requires the exact last completed session/frame, even if commands for the next
frame are already queued.

The additive C ABI capability `ma_oni_domain_frame_inputs_abi_version()` is 1.
`ma_oni_domain_frame_inputs_copy` obtains the immutable observation;
`ma_oni_domain_snapshot_advance_with_inputs` validates its checksum, framing,
source image, model/geometry and clock, applies the captured batches, then runs
the ordinary actor and physical advance. It does not call game callbacks,
replace the live owner, or change saved game identities. Callback routing is
still performed once by the live publication path. Internal replay uses local
epochs without modifying captured commands or save files. Failure leaves the
output buffer and required-length field unchanged.

The managed saved-colony verifier independently checks journal framing, both
SHA-256 bindings, geometry and every command's source epoch. It compares every
replayed complete snapshot with the actual next worker frame and retains the
initial source image, each journal and a per-frame digest report. It uses
actual game timesteps and commands; it does not predict AI decisions or replace
the game clock. Empty journals produce the same bytes as the existing
snapshot-only continuation.

Durable registered actors can advance together with captured external inputs.
Actor configuration changes are explicitly flagged; replaying those changes
is not implemented. Existing actor fixtures that only refresh a binding with
no external heat/mass commands retain their original exact snapshot comparison,
reported separately as actor-refresh frames. External inputs combined with an
actor refresh, and actors without durable identities, refuse this replay path.
These restrictions prevent an omitted actor change from being reported as a
successful replay.

## Verification

`native_oni_frame_inputs` passes on GNU Linux, Windows binaries under Wine and
Linux ASan/UBSan, with empty native stderr on all three builds.
It exercises gas, water, gravity, wet cells, nonlinear conduction and viscosity;
interleaved heat/injection/radius consumption; multiple commands in one heat
batch; failed late reservations; exact complete snapshots; empty-journal legacy
identity; source/session/frame mismatch; durable actor timing; and explicit
unsupported actor-change rejection. Order reversal changes the resulting
physical state, establishing that the ordering control is meaningful. C ABI
tests check actual replay and untouched output on bad checksums or insufficient
capacity. Truncated prefixes and structurally altered journals are rejected.
Both managed adapters build, and both declaration suites pass 159 groups.
All 19 backend groups also pass on GNU Linux, Windows binaries under Wine and
the clean Ubuntu 22.04 compatibility build, plus Linux ASan/UBSan (600.74 s).
Both frozen `c4d9e03` native Linux Unity reload sessions now pass all 120
individually exact journal-replayed frames each, with zero actor-refresh fallback.
The original physical trajectory is unchanged. The saved-only binary reviewer
passes every captured journal and the initial source binding without loading
native code. The first report SHA-256 is
`b4b2a3c139979fea0e051a14bb56174e0206ceab4d74618800c075f7fe486f89`.
The complete runner result SHA-256 is
`4070723a66411fd8792f74e84e8aca25194da8f8349ccad3b1cb2ffccf889281`.
The saved-only reviewer passes all 240 captured binary journals across both
sessions; its receipt SHA-256 is
`1fc92b6974cee9740ff17ea5d497a5da7330504cf8cc4d7779c46ed6393b212f`.
Actual live-command Unity continuation remains a separate gate.

The isolated licensed simulation worker also passes the journal path for gas
and compressed-water heat on both platforms (Windows binaries under Wine).
Each case checks 61 complete frames, including sixty after projection priming,
for 244 exact worker continuations. The C ABI exports a coherent journal;
managed code checks its source digest/epoch/geometry and statelessly replays it
to the actual complete worker image. Exactly one successful batch containing
two heat commands appears; the late-invalid batch is absent. Frame-zero journal
reads reject without changing output. Enable this additional console control
with `MA_VERIFY_FRAME_INPUTS=1`; see the
[headless native interface harness](../tools/NativeShadowProbe/README.md).
These controls use real licensed simulation workers, but do not start Unity or
certify an actual Windows OS.

`tools/oni_frame_input_evidence.py --report <native-domain.json>` reviews the
retained Unity journal files without loading native code. It independently
decodes the little-endian commands, order, batch boundaries, source epochs,
geometry, checksums and callback/radius fields, then checks each binary against
its reported digest, count and clock. It requires every frame to use supported
input replay and verifies the initial captured source image. This verifies
captured inputs and their bindings; it does not independently solve physics or
predict future game commands. Actor-refresh fallback reports refuse this
particular review.

This supplies a prerequisite for [live radius consumption](native-radial-consumption.md),
not proof of a live duplicant, conserved metabolism or whole-colony ownership.
Those remain explicit work in the [completion plan](completion-plan.md).
