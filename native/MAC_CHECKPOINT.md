# Closed MAC physical checkpoints

`MacClosedWorld::checkpoint()` writes the authoritative occupied-grid owner.
`from_checkpoint(registry, eos, bytes)` validates a complete image before
returning a new owner. It retains the clock and revision; a failed read changes
no existing owner. This supplies physical persistence for the closed reference
and the sealed native domain, rather than reconstructing physics from a display
projection. Ordinary ONI colony save/load integration remains unfinished.

The image retains every constituent mass and U, canonical face J, passive
quantity, wall, periodic boundary, geometry value and timestep setting. Full
significand/exponent momentum is retained alongside its explicit binary64
projection. Exponents beyond a signed 64-bit word remain decimal integers in
the bounded `MacExponent` representation. They are never flattened to zero.

Registry order and all physical registry/EOS properties have a binary identity:
species IDs/phases, molecular mass, caloric/formation data, conductivity,
contamination limit and atomic composition; EOS version, gas constant, entropy
reference, profile version, material definitions, validity ranges and provenance.
Display names are outside the physical identity. A changed model rejects the
image. Binary identity avoids platform differences in floating-point text.
Adding physical settings or changing their wire meaning requires a schema
update and complete continuation controls.

## Wire format 2

All integers are little-endian; floating values are finite IEEE binary64 values
stored by their bits. Counts are unsigned 32-bit values. The image is bounded
to 1 GiB and consists of:

1. Eight-byte `MACCLS01` family signature, unsigned 32-bit version 2, and a
   length-prefixed binary model identity.
2. Physical time and unsigned 64-bit revision.
3. Width/height, spacing/depth and two periodicity flags.
4. Maximum timestep, outgoing-volume limit, pressure preconditioner, gravity,
   material/compression/momentum reconstruction settings, conduction flag and
   compression-activated heat enum.
5. Cell count and one boolean wall flag per cell.
6. Each cell's U, complete ordered constituent masses and passive entries
   (unsigned 64-bit identity and binary64 quantity).
7. For each axis, the projected face count/values and full face count/values.
   Full fields are empty or complete. A full value has a binary64 significand
   and a length-prefixed canonical decimal exponent.
8. Unsigned 64-bit FNV-1a integrity checksum over all previous bytes.

The checksum detects accidental damage. Model identity, dimensions, byte/count
limits, booleans, enums, canonical exponents, complete coverage, trailing bytes,
physical inventories and EOS validity are checked separately. Parsing does not
infer missing values, clip traces, normalize corrupted data or repair energy.
Version 1 lacks the heat setting and is rejected explicitly; it is not restored
using whichever default happens to be compiled into the current binary.

## Native domain boundary

Checkpoint extension ABI 1 adds buffer and restore APIs without changing domain ABI 2:

- `ma_oni_domain_checkpoint_copy(session, frame, output, capacity, required)`
  exports one exact completed frame under the native publication fence.
- `ma_oni_domain_checkpoint_advance(input, input_bytes, seconds, output,
  capacity, required)` restores an independent owner and returns its advanced
  physical image. Zero seconds validates and reserializes it.
- `ma_oni_domain_checkpoint_validate(config, input, input_bytes, saved_time)`
  checks a complete physical image against the intended native owner without
  initializing, loading or changing the native simulator.
- `ma_oni_domain_restore(config, input, input_bytes, saved_time, status)`
  activates a restored physical owner after the normal native world load. It
  requires an attached, initialized simulator with matching actual dimensions
  and no existing custom owner. Failure leaves the status output unchanged.

A null output with zero capacity queries the required size. Failed calls leave
buffer/count outputs unchanged. Export refuses stale frames, pending commands,
live actors and unacknowledged receipts; it cannot silently omit them. Stateless
restore/advance never changes the live colony or its clock. These operations
select and restore the executing thread's arithmetic environment.

Restore admission checks the complete physical model/integrity, saved clock,
grid, spacing/depth, gravity, impermeable exterior, phase and publishable energy
and momentum. Passive metadata and other shapes require their own adapters.
Activation preserves saved physical time, revision and complete state. It
assigns a new session and starts its publication counters at zero; those
counters do not replace physical time. Boundary diagnostics start from the
restored inventory rather than inheriting an unrelated previous session.

The physical image excludes native solids/buildings, ONI world placement, managed objects,
registration generations, actor state, callback outboxes and cumulative domain
boundary diagnostics. Full colony persistence must reserve and pair those
states with a matching native save and atomically publish the complete file.
The existing scalar-save guards remain active until that protocol exists.

## Paired native capture

Paired extension ABI 1 now captures the native v15 world and physical owner at
one completed native epoch. `ma_oni_domain_pair_request` admits an exact drained
session/frame on the activation thread and returns a pending token. The native
`EndFrame` barrier performs the capture under the complete frame fence. A
zero-time synchronization frame can complete this request while physical time
stays fixed. The 112-byte status records the actual captured frame, native
epoch, capture thread, geometry, time and both lengths.

`ma_oni_domain_pair_poll` observes completion; `ma_oni_domain_pair_copy` copies
the retained immutable buffers. Neither repeats native saving. Undersized or
overlapping output buffers change nothing. Caller changes do not affect later
copies. Release retires a token and is idempotent for the last released token;
release before fulfillment cancels the request. Closing the simulator turns an
unfinished request into an explicit failure. Only one token may be retained.

The worker uses a private single-use native-save authorization and always ends
the native borrow before publishing the paired buffers. A separate save lock
protects that borrow; an existing ordinary native save must finish before
ownership starts. The worker queue has its own lock, so capture never waits for
the API lifecycle lock while the game thread waits for a completed frame.
Raw scalar-save entry points remain guarded.

`sealed-pair` verifies paused capture on the licensed Linux and Windows
libraries (Windows under Wine), exactly one native save across repeated polls
and copies, source time/state retention, output failures, immutable buffers and
token retirement. `native-paired-checkpoint-domain` verifies the actual Linux
game's native compatibility tuples and physical checkpoint, then encodes both
sections in the existing SHA-256 `MACOLONY` envelope. Its custom section currently
contains versioned source metadata, the domain configuration and physical
image. Native Unity load binding, managed object state, actor/outbox persistence and
ordinary whole-colony save/reload remain unfinished. This capture does not grant
ordinary save permission.

`NativeDomainCheckpointPayload` reads/writes that inner `MAONID01` version 1
section. Its source session/frame/epoch, physical clock and geometry must agree
with the contained `MACCLS01` version 2 metadata. Before native loading, the
restore adapter also checks the complete native world inventory and backwalls,
guard border, world placement, the six-cell compatibility tuples and their
impermeable material border. Full C++ validation remains mandatory before
dispatch; the managed metadata reader does not replace physical validation.

`sealed-restore`, `sealed-restore-water` and `sealed-restore-gravity` write/read
the paired envelope, reject invalid sources before any native message, then
load the saved world through the actual licensed native loader. Native compact
saves omit flow flags. The adapter installs those flags and restores challenged
compatibility tuples through explicit zero-time frames, with ownership already
active. It verifies unchanged physical clock/state and every outside cell
against an unmodified native-load control. Twelve subsequent worker frames
must exactly match independent checkpoint continuation. Linux and Windows
libraries pass all three controls (Windows under Wine). These isolated native
reloads have no live Unity object graph; they do not authorize restarting the
native simulator beneath existing game objects.

## Verification

The native `mac_physical_checkpoint` CTest checks exact restored state and
uninterrupted/resumed continuation for moving gas and wet material, including
gravity, conduction, traces, passive quantities and a solid corner. A separate
enabled/disabled heat loop requires the restored setting and complete next
state/heat diagnostics to agree with uninterrupted advancement. Legacy and
unknown versions, invalid heat enums and invalid conduction flags reject even
with a valid checksum. A separate
case retains canonical momentum at exponent `-9223372036854775809` whose scalar
projection is zero. Every truncation, damaged data, foreign model, invalid
clock/revision, impossible dimensions and valid-checksum trailing data reject.
Domain regressions also require drained callbacks/pending work and compare a
restored image against the actual next worker timestep.

`sealed-checkpoint` runs that continuation comparison through the licensed
Linux or Windows SimDLL and the exported managed/native API. The Windows binary
is additionally exercised under Wine on Linux; this is not Windows OS/Unity
acceptance. Cross-platform controls read and reserialize the other platform's
image without changing a byte. Physical trajectories need not be bit-identical
between different compilers/platforms; the exact next-frame comparison uses the
same platform and binary.

`native-checkpoint-domain` performs the comparison in the normal rendered Linux
game: it exports a completed physical frame, restores/advances it independently,
then requires the actual next native worker frame to have identical complete
bytes. Reports retain source/expected/actual SHA-256 values and private binary
images. All native exclusion, grid, renderer, solid heat and save-guard checks
still apply. This verifies the physical restart format, not ordinary colony
save/reload or first-playable readiness.

### Heat-setting integration

After merging the default heat owner with the physical checkpoint work,
strict Windows/MSVC and Linux/GCC15 builds pass ten scoped groups per OS:
checkpoint, six heat-owner groups, native domain, physical conduction and gas
boundary transfer. The owner saved reviews again pass 122,035 / 122,029 checks
and all four corruptions. Source comparison confirms the accepted owning
algorithm and 88 native dependency files are unchanged; the owner header only
adds checkpoint methods and its schema reminder.

For gas and wet grids, both heat settings retain the entire image and resume
to the same next complete state and heat diagnostics as uninterrupted advancement.
Both platforms write enabled/disabled images, and each reads and reserializes
the other's complete 4,467-byte images without changing a byte. This does not
require trajectories from different compilers to be bit-identical.

The private harness's first two configurations fail before compiling: duplicate
registration of an existing owner target, followed by attempting to configure
that target before the main project declares it. Both first failures remain
retained. Correcting the private target list changes no native implementation.
The first Windows cross-read is launched before the Linux fixture is written
and fails on a missing file. The follow-up checks the now-completed file hashes
and reuses the same binary, source and earlier ten groups; no build or earlier
native group is replayed for that correction.

Private evidence under `local/occupied-staggered-validation`:

- `mac-shock-heat-checkpoint-windows-r3`, terminal SHA256
  `7e66e0d3801ffaf8d187bfa4660302f7e4ba3353529b58236d7ef257aa05402c`
  retains the first missing-file failure after ten passing scoped groups.
- `mac-shock-heat-checkpoint-windows-cross-after-file-r1`, supplemental terminal
  SHA256 `2f32ec1a84bd34ab69808706220b83c5c9f304f79a82153c98ef1a69ec49dba3`
  passes both cross-platform reads against completed Linux images.
- `mac-shock-heat-checkpoint-linux-r3`, terminal SHA256
  `7bbbdea2c7705c40589ae209189847ca370160bada8c283fdb315cac991301fc`
  passes all ten scoped groups, review and both cross-platform reads.

The merge/source review SHA256 is
`78d907f569ba19fc008e02296488ca416a356742564e6e0f88e8fe114fbbc0f4`.
These are standalone native controls. The original strong-flow gate is not
repeated; there is no new sanitizer, Unity, ordinary colony save/reload,
production or performance acceptance. The default game DLL remains unchanged.
