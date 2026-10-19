# Native domain persistence

This document covers what a [sealed native domain](sealed-native-domain.md)
colony save carries beyond the physical image, and the load paths that must
restore it: durable point actors, in-flight CO2 particles, natural day
autosaves, a same-process reload through the main menu, native orbital
save-reference admission and Spaced Out saves. The base paired save and
fresh-process load are described in
[paused colony persistence](sealed-native-domain.md#paused-colony-persistence);
the physical image format is [`MACCLS01`](../native/MAC_CHECKPOINT.md) and the
actor wrapper is [`MAACTR01`](../native/ONI_ACTOR_CHECKPOINT.md).

Every load control checks the same core gates: the exact saved physical image
before positive time, a paused frame-zero autosave with complete header/body
readback, both native startup event batches, and 120 complete continued frames
compared individually against independent replay of the actual inputs. The
input save must stay unchanged.

## Saved domain section

The paired colony save holds a `MAONID01` inner domain section. Its version
follows its content, and older versions stay readable:

| Version | Content |
| --- | --- |
| 1 | Physical image only (no actors, no particles) |
| 2 | `MAACTR01` wrapper: physical image plus durable actor records |
| 3 | A separately length-delimited physical image (version 1 or 2 content) followed by a nonempty `MACO2P01` particle image |
| 4 | [Configured](native-configured-domain.md#saves-and-reloads) (scenario 7) image: `MADCFG01`, or `MAACTR01` enclosing it |
| 5 | A version 4 image followed by a nonempty `MACO2P01` particle image |

Versions 1–3 are limited to scenarios 0–6 and versions 4–5 to configured
sessions. Empty particle data keeps the exact version 1, 2 or 4 framing. Native physical
admission and the outer paired-save checksum apply before the original native
loader runs.

The managed metadata check admits the scenario's legacy solver version 2 or
3, version 5 diffusion images and version 6 production images. These solver
versions share the identity, clock and grid prefix; they do not change the
`MAONID01` framing above. The complete native image and scenario settings
still require native validation. The standalone checkpoint evidence reader
accepts version 2 reference and version 6 production controls with the same
exact-next-frame requirements.

## Durable point actors

A saved consumer or emitter needs its remaining timing and a stable game
identity as well as the fluid. Native handles and callback indices belong to
one running session and are never persistent identities.

**Scope.** Admitted, disease-free, single-cell `ElementConsumer` and
`ElementEmitter` components that share a root with a registered `KPrefabID` and
`SaveLoadRoot`. Identity is the saved prefab instance ID, a prefab tag
fingerprint, the actor kind and the same-kind component index. Unsupported or
unsaved registrations still prevent a complete checkpoint. Pumps, metabolism
and general machines are not included.

**Capture.** Persistent configurations reserve a complete batch atomically;
duplicate identities, stale frames, changed prefab identities and unsupported
phases reject it. Stable identities order actor operations, so a different
native handle allocation after loading cannot reorder transfers. Refreshing an
unchanged configuration keeps its timing. An ordinary save drains edits and
completed callback receipts, then captures native cells, the solver and the
actor records at one worker fence. `MAACTR01` keeps emitter elapsed time, the
consumer's binary32 rounding remainder and blocked state, and excludes handles,
callbacks, thread IDs and session IDs. The physical-only export still refuses
live actors.

**Restore.** Managed and native admission validate the section and the complete
solver model before native load. Restored records stay pending until normal
scene spawning creates the actual components; each fresh native registration
must match the saved identity, location, material and configuration. Inactive
spawn bootstrap messages cannot reset saved timing. ONI's emitter waits for a
`SimComponent` dirty-state update after registration, so during the restore
pause the adapter runs that component's real `Sim200ms(0)` configuration pass
after its handle callback, checks it against the saved record and keeps
physical time; it never spends a managed subtick. The game holds positive time
while bindings are pending, and the native worker faults if an incomplete
restore tries to advance (fluid exclusion stays installed). The nonserialized
blocked property is restored without publishing another blocked edge.
Positive consumer parcels are checked against the real `OnElementConsumed`
callbacks, count and exact mass. Continuation compares actor timing as well as
fluid; a fluid-only match does not pass.

### Running the control

`--verify-persistent-actors` uses three real saveable prefabs: a consumer, an
emitter and a blocked emitter. It requires `native-domain` or
`native-liquid-domain`, `--save-colony` and at least 80 frames. Load the save
with `native-load-domain --load-save <file> --resave-before-resume
--reload-in-process`. On Windows use `run_colony_validation.ps1
-VerifyPersistentActors -NativeDomain gas|water -SaveColony -Frames 80`, then
`-NativeDomain load -LoadSave <file> -ResaveBeforeResume -ReloadInProcess`. The
load reviewer requires restored actor evidence whenever the save has actors.
Local oxygen exhaustion is a valid selective-consumer result in long runs.

### Actor persistence status

- **Native:** gas, compressed water and water with gravity pass with reversed
  registration order, changed handles, a positive remainder, blocked/timing
  state, malformed images with recomputed checksums and ten exact resumed
  frames each; premature advancement faults. They pass on Linux, ASan/UBSan,
  Windows binaries under Wine and MSVC on Windows.
- **Linux Unity:** gas and water sources save three live actors (the gas source
  after 114 consumer callbacks carrying 1.1400000331923366 kg). Both reload
  sessions bind all three actors, flush two real zero-time emitter
  configurations and match 120 complete actor-and-fluid frames with 120
  matching consumer callbacks.
- **WindowsPlayer under Proton:** gas and water sources in fresh nine-world
  Spaced Out clusters, two WindowsPlayer sessions and two native Linux sessions
  of each save pass.
- **Cross-platform numbers:** the complete gas checkpoint comparison (all 119
  stored quantities, exact actor/model/geometry/clock) has a maximum relative
  difference of 6.73e-12, within the 1e-8 bound. Water **fails** that bound
  (maximum stored-momentum difference about 2.1e-7) even after the
  [arithmetic scope repair](native-platform-numerics.md#solver-arithmetic-scope);
  this is open.
- **Actual Windows OS:** no Unity actor save/reload has run. Attempts stopped at
  Steam's "playing on another computer" prompt; the runner restored the profile.

Open: crash recovery of pending outboxes, object destruction during callback
delivery, moving actors across owners, arbitrary prefab hierarchies, complete
process reservations and production devices. A static actor round trip does
not establish any of them.

## In-flight CO2 particles

In-flight breath is material inventory: depositing it before saving would
change the simulation and dropping it on reload would remove mass and energy.
The paired save therefore includes the original CO2 pool's outstanding parcels
with the completed fluid image and the original native world.

The adapter observes the five-argument `CO2Manager.SpawnCO2` factory after the
game creates a particle. A particle whose origin is in the active fluid region
gets a mod-owned incarnation ID describing one parcel, not a Unity component or
native save object. Reusing a pooled component retires its previous
incarnation, even when the reuse is outside the region. The retirement witness
latches when the parcel leaves the active pool; later reuse is recorded
separately and cannot undo it. Save capture drops entries that have left the
manager's active list.

During paused save admission the adapter captures each parcel's exact binary32
position, velocity, mass, temperature, remaining lifetime, animation rotation
and flip, and checks they survive ordinary serialization unchanged. A particle
with a `SaveLoadRoot` rejects: the adapter must never serialize inventory the
game already saves through a native root.

`MACO2P01` version 1 is a 24-byte header, at most 4,096 canonical 64-byte
records and a 64-byte ASCII SHA-256. Records have strictly increasing positive
IDs and zero reserved fields. Admission rejects invalid lengths, checksums,
duplicate identities, nonfinite fields, invalid mass or temperature and
coordinates outside the saved world. Positive binary32 subnormal masses and
expired-but-undeposited lifetimes are kept.

On reload, positive ticks stay paused until the normal CO2 manager exists. The
adapter rejects an already occupied owned pool, recreates the saved particles
through the original factory, restores their fields and compares the complete
particle image byte for byte before releasing the pause. The game then owns
movement, deposition and retirement. Deposits enter the native transfer
journal like other material inputs; restore deposits nothing and spends no
artificial timestep.

**Controls.** `--verify-transient-particles` (Windows
`-VerifyTransientParticles`) with `--save-colony` on a gas source, then
`native-load-domain` (`-NativeDomain load`). The source creates one original
0.03125 kg, 333.25 K parcel while paused just before saving; the load requires
exact restore, the frame-zero autosave, one completed deposition receipt and
removal from the active pool. Adding `--subnormal-carbon-particle` saves the
smallest positive binary32 parcel (2^-149 kg) in flight instead; it then needs
one exact deposit and a positive transported CO2 inventory from an initially
empty constituent, and excludes the live breather so other CO2 cannot hide it.

**Status.** Sources on both players, each with a live breathing duplicant, save
two in-flight parcels. Both local and both cross-player reload sessions restore
them exactly and pass 120 exact input-replayed frames, one test-parcel
deposition and 120 native body-heat transfers per session. The smallest-parcel
variant passes both sources and all eight reload sessions (960 exact frames),
each depositing `2^-149` kg once and ending with positive CO2, with later pool
reuse observed. WindowsPlayer ran under Proton; actual Windows OS,
whole-colony particles, bubble/exhaust persistence and conserved metabolism
remain open.

## Natural day autosaves

The paired save boundary sits on the ordinary `SaveLoader.Save` entry, so a
scheduled autosave uses the same paused capture, readback and pause-count
restoration as a menu save.

```sh
python3 tools/oni_headless.py run --package /path/to/frozen/linux-probe \
  --mode native-domain --verify-natural-autosave --frames 3300 \
  --private-display --timeout 7200 --output artifacts/natural-autosave-001
```

The control sets this disposable colony's autosave interval to one cycle and
lets the game reach the next day. It never changes game time or calls the save
itself. Exactly one real `GameClock.DoAutoSave` request must occur, at cycle one
or later, and the completed save must be marked automatic. Simulation must
continue afterwards. The control replays the saved image through every later
worker interval and requires exact equality with the final live image (the
final image only, not each intermediate frame). It requires `native-domain`,
3100–5000 frames and no `--save-colony`, keeping it to one day rollover; add
`--verify-persistent-actors` to save live actors this way. Load the result with
`native-load-domain --load-save ... --resave-before-resume`. The Proton runner
has the same flag.

The physical owner starts after colony startup, so its clock does not claim the
earlier native intervals (the cycle-one save lands near physical frame 2,730,
native game time 602.03 s).

**Status.** Linux passes actor-free and three-live-actor sources (about 570
replayed intervals each) and their reloads; the actor reload passes two
same-process sessions. WindowsPlayer under Proton passes a live-actor source
and two reload sessions. Actual Windows OS scheduled saves, overwrite and
crash durability remain open.

## Same-process main-menu reload

The load runner can run two saved-colony sessions in one Unity process. After
the first paused autosave and exact continuation it uses ONI's normal
quit-to-main-menu entry, observes simulation shutdown and the returned menu,
and loads the same staged input again. The second session must pass another
paused autosave and the same number of physical frames.

```sh
python3 tools/oni_headless.py run --package /path/to/probe \
  --mode native-load-domain --load-save /path/to/paired-colony.sav \
  --resave-before-resume --reload-in-process --frames 120 --timeout 1800 \
  --output artifacts/same-process-reload-001
```

Windows: `run_colony_validation.ps1 -NativeDomain load -LoadSave ...
-ResaveBeforeResume -ReloadInProcess`. Both runners stage a separate input copy.

Native shutdown retires snapshots, callback bindings, paired tokens and held
restore events. The managed domain then clears its per-colony publication,
replay, save, actor and validation state; process-wide reload evidence stays.
The validation driver binds to the new main menu rather than adding a second
driver. The second autosave gets its own file. `InProcessReload` evidence needs
the same process ID, a later native session ID, observed shutdown and menu
boundaries, both initial autosaves, matching physical milestones and an
unchanged input hash. A single passing load cannot pass this mode.

The screenshot handoff waits for Unity's complete first-leg PNG (including its
`IEND` record) while the colony is still paused, and only then starts the
loading overlay. Otherwise the passed colony report receives a picture of the
loading screen.

### Comparing two sessions

With identical physical inputs, both sessions must reach the same complete
final image. Live breathing adds a second mode: each session must replay every
completed frame exactly from the saved source and its own captured game inputs,
with no actor-refresh fallback. A per-session input transcript hashes all
physical heat and mass operands, batch order, geometry and timing, and removes
only session IDs, source-state hashes and callback handles (source admission and
callback delivery are checked separately). Equal transcripts still require
identical final images. Different transcripts are admitted only for the
explicit live-breathing control and are reported as `captured-live-inputs` with
both timelines and hashes. The saved-binary reviewer recomputes the transcript
hashes. Tests confirm that callback and session changes leave the digest
unchanged while heat, mass, radius, target, constituent and timing changes
alter it.

**Status.** Linux and actual Windows (MSVC) pass actor-free gas/water
cohorts, the Windows one including the water-gravity profile and the repaired
liquid rendering; WindowsPlayer under Proton passes it in every later reload
cohort. Live-actor, particle and live-breathing reloads use this mode on both
players. Some older inputs log `Missing KPrefabID reference: 246809` during
resave; newer Windows actor-batch inputs do not. General managed-reference
integrity, the Load screen's file-selection UI, pending-outbox persistence and
arbitrary colony ownership remain open.

## Orbital reference admission

The fluid owner leaves ONI's managed worlds and nonfluid objects alone, so an
exact fluid checkpoint says nothing about their saved references. Native
orbital references are therefore admitted separately, before a restored domain
advances and before a paired save starts serialization.

The adapter reads each native world's `OrbitalMechanics` reference list, saved
object ID and cached target directly. It does not call `Ref.Get()` or
`GetId()`, which can clear a missing reference. Each populated reference must
name a tracked `OrbitalBG` in the correct world with one initialized, spawned
`SaveLoadRoot` registration. Worlds and slots must be unique. Empty optional
references and newly created cached references keep their native behaviour.

Restored physics stays at frame zero until the check passes. A missing or
mismatched target faults the bounded load, keeping exclusion and the pause. A
save with invalid references is declined before opening its destination,
through the existing save-problem path. The adapter never deletes a reference,
invents an object or assigns a different ID; an already damaged save needs an
intact source. `NativeOrbitalReferences` records the observed identities,
registrations, stage, inspection count, error and the frame/time of admission,
reports zero rewritten references and leaves general managed-reference
integrity unverified. Session shutdown clears it.

**Finishing the background save root.** `OrbitalObject.OnSpawn` deactivates its
own object, and a fresh colony could save with orbital ID 2 tracked but its
`SaveLoadRoot` never spawned. In native-domain modes the adapter calls the
existing root's normal `Spawn()` before `OrbitalObject.OnSpawn`. It requires the
original tracked `OrbitalBG`, an initialized root, an available native save
manager and zero prior registrations, and must leave exactly one. An already
spawned root keeps its lifecycle, and the orbital body still controls
visibility. `NativeOrbitalReferences.SaveRootLifecycle` records the completed
root IDs and errors; the reviewer rejects errors, duplicate or invalid IDs and
any created object or rewritten reference. Default observer modes do not
install this patch.

**Waiting for the restored native lifecycle.** A second same-process load can
reach admission before Unity calls the save roots' `Start`, with every root
initialized, `RootSpawned=false` and zero registrations. Admission waits only
for that exact combination. It inspects every world and reference first, so a
missing target, changed identity, duplicate registration or inconsistent root
still rejects immediately; ordinary save admission stays strict. The adapter
never spawns or registers world roots itself. Every waiting observation and the
final admission compare the complete physical checkpoint byte for byte with the
restored source and require its original time. A bounded timeout starts at the
first waiting observation, after loading and shader warm-up.
`RestoreLifecycleHold` records waiting inspections, pending root counts and the
checked frame, time and image hash; the reviewer rejects an outstanding wait or
a witness without the original image.

**Status.** Pure admission tests cover missing targets, changed IDs, wrong
worlds, duplicate slots and worlds, uninitialized or duplicate registration,
valid empty references and unchanged observations after rejection. Linux Unity
admits intact live-actor saves in two same-process sessions and in a fresh load
without an initial resave. WindowsPlayer under Proton and native Linux admit
fresh Spaced Out sources, including second sessions that wait once for all ten
roots. An older nine-world input whose asteroid references missing object 2
rejects at frame zero with unchanged physical time and raw ID, no save writes
and an unchanged input. Actual Windows OS execution and general
managed-reference integrity remain open.

## Spaced Out saves

Spaced Out saves load only where the installation has the expansion bundles;
the [DLC content preflight](native-backend-integration.md#dlc-content-preflight)
stays mandatory, and no assets are transferred between platforms. On Linux the
runner uses the licensed installation's bundles through
`oni_headless.py --game-dir`.

**Status.** Native Linux loads nine-world saves in two same-process sessions,
with the six owned cells at their saved location and the rest of the colony
native ONI; restored readers and the calibrated GPU check pass. Fresh
WindowsPlayer Spaced Out sources reload on both players, as recorded above.

`Missing KPrefabID reference: 2` during a resave means native `Ref<T>` could not
find a saved object and reset the reference. On the older input it was the
asteroid's `Ref<OrbitalObject>` pointing at an `OrbitalBG` that was never
serialized; current builds reject that input through orbital admission. Why that
source omitted the object is not established. All gameplay packs and a
playable nine-world overhaul remain open.
