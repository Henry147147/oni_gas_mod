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

## Wire format 1

All integers are little-endian; floating values are finite IEEE binary64 values
stored by their bits. Counts are unsigned 32-bit values. The image is bounded
to 1 GiB and consists of:

1. Eight-byte `MACCLS01` signature, unsigned 32-bit version 1, and a
   length-prefixed binary model identity.
2. Physical time and unsigned 64-bit revision.
3. Width/height, spacing/depth and two periodicity flags.
4. Maximum timestep, outgoing-volume limit, pressure preconditioner, gravity,
   material/compression/momentum reconstruction settings and conduction flag.
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

## Native domain boundary

Checkpoint extension ABI 1 adds two buffer APIs without changing domain ABI 2:

- `ma_oni_domain_checkpoint_copy(session, frame, output, capacity, required)`
  exports one exact completed frame under the native publication fence.
- `ma_oni_domain_checkpoint_advance(input, input_bytes, seconds, output,
  capacity, required)` restores an independent owner and returns its advanced
  physical image. Zero seconds validates and reserializes it.

A null output with zero capacity queries the required size. Failed calls leave
buffer/count outputs unchanged. Export refuses stale frames, pending commands,
live actors and unacknowledged receipts; it cannot silently omit them. Stateless
restore/advance never changes the live colony or its clock. Both operations
select and restore the executing thread's arithmetic environment.

The physical image excludes native solids/buildings, ONI world placement, managed objects,
registration generations, actor state, callback outboxes and cumulative domain
boundary diagnostics. Full colony persistence must reserve and pair those
states with a matching native save and atomically publish the complete file.
The existing scalar-save guards remain active until that protocol exists.

## Verification

The native `mac_physical_checkpoint` CTest checks exact restored state and
uninterrupted/resumed continuation for moving gas and wet material, including
gravity, conduction, traces, passive quantities and a solid corner. A separate
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
