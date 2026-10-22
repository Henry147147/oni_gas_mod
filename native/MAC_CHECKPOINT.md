# MacClosedWorld physical checkpoints

`MacClosedWorld::checkpoint()` writes the complete authoritative owner as a
`MACCLS01` image; `from_checkpoint(registry, eos, bytes)` validates a complete image
before returning a new owner with the saved clock and revision. A failed read changes
no existing owner. This is physical persistence for the [solver](MAC_SOLVER.md) and
the sealed native domain, not a reconstruction from a display projection. The native
domain wraps the image in further sections for actors and particles; see
[native domain persistence](../docs/native-domain-persistence.md) and
[`MAACTR01`](ONI_ACTOR_CHECKPOINT.md).

The image keeps every constituent mass and U, canonical face J, passive quantity,
wall flag, periodic boundary, geometry value and physical setting. Full-range
momentum is kept beside its binary64 projection; exponents beyond 64 bits are stored
as canonical decimal integers and are never flattened to zero.

Registry order and every physical registry and EOS property form a binary model
identity: species IDs and phases, molar mass, heat capacity, density, conductivity,
formation enthalpy, contamination limit and atomic composition; the EOS version, gas
constant, entropy reference temperature, profile version, and each material's
definition, validity range and provenance. Display names are excluded. A different
model rejects the image. Binary identity avoids platform differences in
floating-point text. Adding a physical setting or changing a field's meaning needs a
format change and complete continuation controls (`mac_checkpoint.cpp`).

## Wire format

All integers are little-endian; floating values are finite IEEE binary64 values
stored by their bits. Counts are unsigned 32-bit values. The image is bounded to
1 GiB and consists of:

1. Eight-byte `MACCLS01` family signature, unsigned 32-bit version (2; 3 when
   reference-kernel viscosity is enabled; 4 for the retired interval kernel; 5 with gas
   diffusion, historically kernels 0/1; 6 for production kernel 2 with midpoint
   pressure; 7/8 for opt-in two-moment pressure implementations; 9 for the
   [void phase](MAC_VACUUM.md#void-phase); 10 for carried primary U; 11 for
   [phase change](MAC_PHASE_CHANGE.md); 12 for the [unresolved gas opt-in](MAC_VACUUM.md#unresolved-gas-opt-in);
   13 for predictive step control; 14 for a configured gas-expansion fraction;
   15 for regional predictive control; 16 for a saved game profile),
   and a length-prefixed binary model identity.
2. Physical time and unsigned 64-bit revision.
3. Width/height, spacing/depth and two periodicity flags.
4. Maximum timestep, outgoing-volume limit, pressure preconditioner, gravity,
   material/compression/momentum reconstruction settings, conduction flag and
   compression-activated heat enum. Version 3 then adds the shear and bulk viscosity
   coefficients (binary64). Version 4 also includes both viscosity coefficients
   and the numerical-kernel enum (unsigned 32-bit). Version 5 includes both
   viscosity coefficients, the numerical-kernel enum (0 or 1), then the diffusion
   mode, multiplier, the unsigned 32-bit discrete diffusion formulation (2) and the
   complete immutable transport profile (identity, pair coefficients, validated
   ranges, estimated flags and sources). Version 6 stores both viscosity
   coefficients, requires numerical kernel 2, and always stores the diffusion
   mode and multiplier, including canonical disabled mode 0/multiplier 1.
   Enabled diffusion then stores the same formulation and complete profile.
   Version 7 has the version-6 fields followed by the pressure-integration enum
   (unsigned 32-bit, canonical value 1) before any enabled diffusion formulation
   and profile. Versions 2–6 restore pressure-integration value 0 (midpoint).
   Version 9 stores both viscosity coefficients, the numerical-kernel enum (0 or 2),
   the diffusion mode and multiplier, the pressure-integration enum and the vacuum
   representation (canonical value 1, void phase), before any enabled diffusion
   formulation and profile. Version 10 stores this complete settings prefix too,
   retaining the supported pressure and vacuum selectors explicitly. Version 11 has
   the version-10 prefix followed by the phase-change enum (unsigned 32-bit,
   canonical value 1, saturation). Version 12 stores the version-10 prefix,
   the phase-change enum (0 or 1), then the subcell-void policy (unsigned 32-bit,
   canonical value 1). It requires supported production void-phase settings.
   Version 13 stores the version-12 fields (the subcell policy may also be 0)
   followed by the step control (unsigned 32-bit, canonical value 1, predictive).
   Version 14 stores that prefix with step control 0 or 1, followed by the
   binary64 gas-expansion volume fraction. It requires unresolved expansion and
   a fraction above the legacy `16 * epsilon`, at most 1. Older versions restore
   the legacy fraction. Version 15 has the version-14 prefix, requires step
   control 2, and always stores the gas-expansion fraction (including its legacy
   value). Versions 13/14 reject the regional selector. Version 16 has that
   prefix followed by the game-profile enum (unsigned 32-bit, canonical value
   1, base-game floor) and per-constituent trace-sink threshold (binary64,
   0..1e-6 kg). It requires production regional void-phase settings with no
   kernel phase change or diffusion. Zero disables the sink. Older versions
   restore legacy profile 0 with no sink and retain their bytes.
5. Cell count and one boolean wall flag per cell.
6. Each cell's U, complete ordered constituent masses and passive entries
   (unsigned 64-bit identity and binary64 quantity).
7. For each axis, the projected face count/values and full face count/values.
   Full fields are empty or complete. A full value has a binary64 significand
   and a length-prefixed canonical decimal exponent.
8. Versions 9–16: a count (0, or the cell count) and each cell's occupied volume
   (binary64). The count is 0 exactly when every occupied cell fills its grid
   volume; a restored owner that would canonicalize a stored vector rejects.
9. Versions 10–16: the cell count followed by each correction's projected binary64
   value and boolean full-value flag. A flagged value adds its normalized binary64
   significand and length-prefixed canonical decimal exponent. The full value is
   required exactly when its projection rounds. High/low overlap, scalar negative
   zero and incomplete coverage reject; so does an entirely zero correction vector
   in version 10, which versions 11–16 (selected by their opt-in setting) admit.
10. Unsigned 64-bit FNV-1a integrity checksum over all previous bytes.

Version 2 is written for the reference kernel whenever both viscosity coefficients
are zero and diffusion is disabled, so existing reference images retain their
bytes. A version 3 image with both
coefficients zero is noncanonical and rejects. Version 1, which lacked the heat
setting, rejects explicitly: no missing setting is filled from whatever default the
current binary has.

### Version assignment

Version **10 carries persistent primary-U corrections**. It stores complete
physical settings (including pressure and vacuum selectors), the occupied-volume
sidecar and one canonical correction per cell. Nonzero corrections select this
version; all-zero corrections keep the previous version and bytes.

Version **11 is phase change** (2026-10-20): any owner with `phase_change =
saturation` writes it, whatever its corrections. Sharp-interface boiling keeps no
pending state outside U, and the vapour's calibration offset is part of the model
identity, so the selector is the only new field. `ma_mac_phase_change_tests` restores
a boiling column mid-run and requires byte-identical continuation. Other checkpoint
extensions must use a later free version.

Version **12 is unresolved subcell gas expansion** (2026-10-20), allocated after
a fresh fetch/rebase onto `de08225`. Any owner with this explicit policy writes
it, including with phase change disabled and all-zero U corrections. Unknown or
zero policy values reject as noncanonical; older versions restore exact geometry.
`ma_mac_subcell_void_tests` checks complete identity, malformed settings,
canonical full-cell endpoints and byte-identical continuation with/without carried U.
Clean-build raw comparisons retain all fifteen controls covering versions 2–11
(including synthesized retired layout 4), plus 39 domain images and 39 publications.

Version **13 is predictive step control** (2026-10-21), allocated after a fresh
fetch onto `988ee34`. Any owner with `step_control = predictive` writes it;
`ma_mac_step_control_tests` checks the selector's position, rejection of values
0 and 2, and byte-identical continuation.

Version **14 is a configured gas-expansion volume fraction**,
allocated after a fresh fetch onto `d62ac3a`. The legacy fraction keeps all
older formats and continuation. Configured owners store the fraction and their
step controller explicitly; new colony owners select `0.001`.

Version **15 is regional predictive control**, claimed after a fresh fetch of
`aaef7f8`. It has version 14's complete prefix and fraction, requiring controller
2. Controllers 0/1 keep their previous versions and bytes. Region topology,
workspaces and reports are derived on every call and are not saved.

Version **16 is the saved game accuracy-floor profile**, claimed immediately
after a fresh fetch of `e06dc63` (2026-10-22). It stores profile family 1 and
the explicit trace-sink threshold; every numerical tradeoff must have saved
settings rather than depending on a process-wide preset. The colony owner
separately persists its named sink accounts. Profile 0 writes all previous
formats unchanged. Checkpoint controls cover zero/minimum/maximum thresholds,
malformed settings, old-save identity and byte-exact continuation. This format
addition alone does not select the profile for new colonies or remove traces.

Reference owners write version 2, 3 or 5 according to their settings; production
owners write version 6, or 7/8 with two-moment pressure. A void-phase owner without
U corrections writes version 9. Any owner with nonzero corrections writes version
10 unless phase change selects 11 or unresolved expansion selects 12. Retired layouts remain listed for explicit rejection:

| Version | Kernel | Viscosity | Gas diffusion |
| --- | --- | --- | --- |
| 2 | reference | off | off |
| 3 | reference | on | off |
| 4 | retired interval (1), explicitly rejected | either | off |
| 5 | reference (0); retired interval (1) explicitly rejected | either | physical or accelerated |
| 6 | production (2) | either | disabled, physical or accelerated |
| 7 | production (2), two-moment pressure (1) | either | disabled, physical or accelerated |
| 8 | production (2), coupled two-moment pressure (2) | either | disabled, physical or accelerated |
| 9 | reference (0) or production (2), void phase; midpoint pressure | production only | disabled |
| 10 | reference (0) or production (2), explicit pressure/vacuum selectors and carried U | according to physical settings | according to physical settings |
| 11 | reference (0) or production (2), explicit saturation phase change and primary-U sidecar | according to physical settings | according to physical settings |
| 12 | supported production (2), void phase, explicit unresolved gas expansion | either | disabled |
| 13 | supported production (2), predictive step control | either | disabled |
| 14 | supported production (2), configured gas-expansion fraction | either | disabled |
| 15 | supported production (2), regional predictive control | either | disabled |
| 16 | supported production (2), saved base-game-floor profile and trace threshold | either | disabled |

Version 4 and version-5 images carrying selector 1 reject explicitly as retired.
The old wire layout and selector meaning are reserved, never remapped to
production selection 2. Constructing a selector-1 owner also rejects.

Version 5 accepts only reference selector 0, enabled diffusion, the current
formulation and an exact matching transport profile, including properties and
provenance. Disabled diffusion is noncanonical there. Versions 2/3 restore with
diffusion disabled. A mode change requires explicit opt-in at a completed
boundary; loading a supported legacy save never enables diffusion.

Version 6 is canonical only for production kernel 2. Unknown or other
selectors reject even with a valid checksum; versions 4/5 cannot carry value 2.
An explicit production execution request persists this selection, including an
exact-equilibrium certificate and a range/selector fallback. An ordinary
`from_checkpoint(...).advanced(...)` then selects production with the saved
pressure preconditioner, without a policy flag. Execution options now contain
no preconditioner: automatic uses geometric multigrid on grids of at least
256 cells; explicit Jacobi stays Jacobi. Explicit incomplete Cholesky and
unported physical selectors use the audited reference through the owner.
Thread counts and derived hierarchy/workspaces are not saved.

Versions 7/8 require numerical kernel 2 and temporal selector 1/2 respectively.
Unknown values, the other version’s selector and a redundant midpoint selector
reject even with valid checksums. Every version-2 through 7 writer byte remains
unchanged; version 7 keeps its original real Schur implementation. A moment
owner refuses a legacy
reference fallback that would change its temporal scheme. The controls in
`mac_kernel_moment_tests.cpp` cover ordinary restoration, one/twelve-worker
continuation, malformed selectors and budget rollback; see
[pressure moments](MAC_PRESSURE_MOMENTS.md).

Six synthetic images spanning versions 2, 3, 4 and 5 (including both diffusion
modes, viscosity and the legacy kernel) remain byte-identical to the earlier
writer. Supported reference images restore unchanged; historical selector-1
images are explicitly rejected instead of silently changing continuation. Version-6 gas, liquid,
mixture and empty-cell images continue exactly after restore, with all three
diffusion modes where admitted and both automatic/Jacobi choices. The native
game owner now selects production; the managed paired-save reader admits the
historical reference images and physical versions 6/10. Full native validation
still checks the saved domain before activation.

Neither the interval-kernel version 4 nor the earlier diffusion-branch images
had been publicly released. Both earlier diffusion layouts (formulation 1 and
the first formulation-2 layout without a kernel field) reject as version 4.
Retirement preserves that rejection. Versions 2 and 3 remain byte-identical.

The managed paired-colony wrapper admits physical versions 2/3/5/6/10 for
ordinary, actor and configured-domain images. Version 4 stays rejected there,
because the game never selected the retired kernel. Full native admission still
validates the viscosity values, kernel, diffusion settings and exact transport
profile against the saved domain before activation. The enclosing paired-payload
versions and legacy bytes are unchanged, and unknown solver versions reject.

An explicit diffusion change during the bounded managed continuation audit
starts a new reference segment at the same clock and frame, retaining the exact
configured source image and its hash. The next native input journal binds that
new source. Actor-change flags retain their separate meaning. The continuation
audit ends with its submission fence after acceptance, so subsequent normal
play does not replay an obsolete reference configuration.

Setting enum values (unsigned 32-bit; existing values never change meaning, and an
older reader rejects a value it does not know):

| Field | Values |
| --- | --- |
| Material reconstruction | 0 donor cell, 1 limited linear, 2 swept first moment, 3 interface PLIC |
| Compression | 0 none, 1 quadratic, 2 pressure-sensed acoustic/quadratic, 3 coupled compressive pressure |
| Momentum reconstruction | 0 donor cell, 1 limited linear, 2 quadratic limited linear, 3 swept physical, 4 swept energy flux |
| Heat regularization | 0 none, 1 closing-speed, 2 pressure-sensed Cp/max-speed, 3 pressure-sensed series |
| Numerical kernel (versions 4–9) | 0 reference (versions 5 and 9; implicit in versions 2/3), 1 retired interval (versions 4/5), 2 production (versions 6–9) |
| Pressure integration (versions 7–9) | 1 real Schur two moments in version 7; 2 coupled two moments in version 8; midpoint 0 is implicit in versions 2–6, redundant/noncanonical in versions 7/8, and explicit in version 9 |
| Vacuum representation (version 9) | 1 void phase; 0 (empty cells) is implicit in versions 2–8 and noncanonical in version 9 |

The checksum detects accidental damage. Model identity, dimensions, byte and count
limits, booleans, enums, canonical exponents (significand and exponent must
round-trip exactly), complete face coverage, trailing bytes, physical inventories and
EOS validity are checked separately. Parsing does not infer missing values, clip
traces, normalize corrupted data or repair energy.

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
buffer/count outputs unchanged. Export refuses stale frames, pending commands, live
actors and unacknowledged receipts rather than silently omitting them. Stateless
restore/advance never changes the live colony or its clock. These calls select and
restore the executing thread's arithmetic environment.

Restore admission checks the complete physical model and integrity, saved clock,
grid, spacing/depth, gravity, impermeable exterior, phase and publishable energy and
momentum. Passive metadata and other shapes need their own adapters. Activation keeps
the saved physical time, revision and complete state; it assigns a new session and
starts publication counters at zero, which never replace physical time. Boundary
diagnostics start from the restored inventory.

The physical image excludes native solids and buildings, ONI world placement, managed
objects, registration generations, actor state, callback outboxes and cumulative
boundary diagnostics. A colony save must pair those with a matching native save and
publish the complete file atomically. The scalar-save guards stay active outside the
paused save path below.

## Paired native capture

Paired extension ABI 1 captures the native v15 world and the physical owner at one
completed native epoch:

- `ma_oni_domain_pair_request` admits an exact drained session/frame on the activation
  thread and returns a pending token. The native `EndFrame` barrier performs the
  capture under the complete frame fence; a zero-time synchronization frame can
  complete it while physical time stays fixed. The 112-byte status records the
  captured frame, native epoch, capture thread, geometry, time and both lengths.
- A restored owner may capture publication frame zero when its saved physical time is
  positive, so a paused loaded colony can be saved before its first new step. A
  never-advanced owner at time zero rejects.
- `ma_oni_domain_pair_poll` observes completion and `ma_oni_domain_pair_copy` copies
  the retained immutable buffers; neither repeats native saving. Undersized or
  overlapping outputs change nothing, and caller changes cannot affect later copies.
- Release retires a token and is idempotent for the last released token; release
  before fulfilment cancels the request. Closing the simulator turns an unfinished
  request into an explicit failure. Only one token may be held.

The worker uses a private single-use native-save authorization and always ends the
native borrow before publishing the buffers. A separate save lock protects that
borrow, and an ordinary native save must finish before ownership starts. The worker
queue has its own lock, so capture never waits on the API lifecycle lock while the
game thread waits for a frame. Raw scalar-save entry points stay guarded.

`NativeDomainCheckpointPayload` reads and writes the inner `MAONID01` section. Its
source session/frame/epoch, physical clock and geometry must agree with the contained
`MACCLS01` metadata; for a configured session it also parses the `MADCFG01` wrapper.
Before native loading, the restore adapter also checks the complete native world
inventory and backwalls, guard border, world placement, the compatibility tuples
and their impermeable border: the six-cell room's, or the configured room's with
its mask recomputed from the saved cells
([configured saves](../docs/native-configured-domain.md#saves-and-reloads)). Full C++ validation is
still mandatory before dispatch. The whole pair travels in the SHA-256 `MACOLONY`
envelope.

Licensed-library controls (Linux, and Windows binaries under Wine): `sealed-pair`
checks paused capture, a single native save across repeated polls and copies,
retained source time/state, output failures, immutable buffers and token retirement.
`sealed-restore`, `sealed-restore-water` and `sealed-restore-gravity` write and read
the paired envelope, reject invalid sources before any native message, load the
saved world through the licensed native loader, reinstall the flow flags that compact
native saves omit through zero-time frames, and require twelve subsequent worker
frames to match independent checkpoint continuation exactly. These have no live Unity
object graph.

## Paused Unity colony save/load

Saving:

- The ordinary `SaveLoader.Save(string, bool, bool)` entry (manual saves and
  autosaves) reserves a paired checkpoint when the bounded owner is active. It adds one
  pause request, synchronizes through ONI's ordinary zero-time frame, and requests a
  completed pair at the exact source frame and clock. Only that pair authorizes one
  `Sim.Save` inside the normal serializer, whose native section holds the
  length-prefixed `MACOLONY` envelope. Serialization must leave the physical image
  unchanged.
- Nested saves, foreign threads, pending commands, live owned actors without durable
  identities and undelivered receipts reject before the original serializer runs. Direct native saves and
  unstaged `Sim.Save` calls reject.
- A finalizer releases the pair and exactly the pause request this save added. ONI's
  pause counter is reference counted; checking only `IsPaused` leaks a request and
  blocks a later resume.
- Readback derives the disk header, type directory and body boundaries, checks the
  colony GUID, and requires the full body (compressed or not) and the paired sim
  section to match the exact in-memory serialization. It also requires exactly one
  current `SaveGame.GetSaveHeader` result with identical JSON bytes, captured after
  the body serializer and file open. ONI's return value is not evidence: its
  disk-error path can return the old save path. A mismatch gives no save receipt; the
  pair and pause request are still released. File replacement and crash durability
  are not verified.
- A busy checkpoint source declines through the ordinary save-decline UI before
  serialization. A disk or readback failure keeps ONI's error dialog, restores the
  previous active-save selection and finishes the deferred coroutine's cleanup.
  `--verify-save-io-recovery` (`-VerifySaveIoRecovery` on Windows; at least sixteen
  frames) forces a real denied write and checks all of this.
- While the first save after a load is pending, a gate on `Game.SimEveryTick`
  publishes a zero-time frame instead of consuming a positive `Time.deltaTime` that
  Unity latched before the pause took effect. Managed subtick accumulators are
  untouched.

Loading (`native-load-domain`, fresh process):

- Before native Load: the outer identity/checksum, the complete inner source, every
  native inventory and physical restore admission. After `Sim.Start`: activate the
  saved owner, restore native flags and compatibility cells with zero-time frames, and
  verify the original complete physical bytes; Unity then restores the object graph.
- Startup publications keep all one-shot native event channels (29 channel records) in
  an owned outbox, including zero-frame polls. The first successful ordinary
  publication prepends them in order and retires them once; rejected publications
  keep the outbox.
- Gas, compressed-water and gravity configurations are admitted from the saved
  metadata. `--resave-before-resume` saves at publication frame zero through
  `Game.StartDelayedSave` and the normal autosave coroutine, waiting across Unity
  updates before resuming; the resulting file loads again in a fresh process.
- The loaded scene must keep its colony GUID and duplicants, deliver both startup
  batches, and match every positive frame to independent checkpoint continuation. The
  validation driver publishes each worker frame before submitting the next so no
  observation is skipped.

ONI 744825 can serialize an unassigned disease hash zero with a positive count outside
the custom domain; both licensed loaders normalize it. `NativeWorldBlob.ValidatePreserved`
accepts that native-owned metadata while checking material, geometry, guard, backwall
and quantities; fluid migration (`Split`) still rejects it, and the six compatibility
cells require both disease fields zero.

`native-save-domain` exercises an unprepared save from a running colony, checks that
the pause depth returns to zero and renames its private temporary file to
`native-colony.sav`. Its first attempt holds a reserved heat command and must reject
without a file while keeping the command and clock. Other scenes accept
`--save-colony`. Commands and current results for Linux, MSVC Windows and
WindowsPlayer under Proton are in [sealed native domain](../docs/sealed-native-domain.md)
and [native domain persistence](../docs/native-domain-persistence.md). Save-entry
admission is limited to the drained development domain; general colony ownership
remains open.

## Verification

- `ma_mac_kernel_checkpoint_tests` checks fifteen ordinary continuation
  combinations across gas, liquid, mixtures and exact empty cells, automatic
  and Jacobi preconditioning, and diffusion off/physical/100× where admitted.
  Interrupted/restored runs match uninterrupted production requests byte for
  byte; the 33,389-cell case requires a twelve-member pool on
  the ordinary restored advance. Certified equilibrium persists selection
  too. Resealed wrong versions/kernels, malformed diffusion settings, trailing
  bytes and every truncation reject. The retained-reference fallback preserves
  explicit incomplete Cholesky and the saved production selection. A mid-interval
  range flag retains selection 2, charges discarded work to the shared budget,
  audits the reference result and leaves an exhausted source unchanged.
- `ma_mac_checkpoint_tests` (CTest `mac_physical_checkpoint`) checks exact restored
  state and resumed continuation for moving gas and wet material with gravity,
  conduction, traces, passives and a solid corner. Every heat and compression setting
  combination restores its setting and continues to the same complete next state and
  diagnostics as uninterrupted advance. Legacy and unknown versions, invalid enums and
  conduction flags reject even with a valid checksum. Canonical momentum at exponent
  −9223372036854775809, whose projection is zero, is retained. Truncation, damage, a
  foreign model, invalid clock/revision, impossible dimensions and valid-checksum
  trailing data reject.
- `--write-checkpoint[-no-heat|-quadratic] <file>` and `--read-checkpoint <file>`
  (here and in the swept, bounded-energy and quadratic-limiter suites) write an image
  on one platform and read it on another; the reader must reserialize the exact bytes.
  Trajectories from different compilers need not be bit-identical; exact
  continuation is compared on one platform and binary.
- Domain regressions require drained callbacks and pending work and compare a
  restored image with the actual next worker step. `sealed-checkpoint` repeats that
  through the licensed SimDLL and exported API, and `native-checkpoint-domain` in the
  rendered Linux game requires the next native worker frame to match the restored,
  independently advanced image byte for byte.
