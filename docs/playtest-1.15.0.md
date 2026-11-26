# Mixed Atmospheres 1.15.0

1.15.0 is the release that carries two supports at once, at the user's
requests of 2026-11-26:

- **Rephysicalized**: the work of [1.14.0](playtest-1.14.0.md), which was not
  released on its own (below).
- **Moono**: "Can you next, add support for
  https://github.com/tigin-backwards/oni-moono#setup, you will likely have to
  double the testing to cover with and without moono. Please get started on
  this now, make it version 1.15.0, continue testing 1.14.0 until the 1.15.0
  is ready to test, then you should switch testing over to that version."

Later that day the user decided: "Okay, lets just work on version 1.15.0 and
ship when both supported items are confirmed. You can stop working on 1.14.0
for now." 1.14.0 was therefore never staged or uploaded; its content ships
here. The release waits until the doubled suite (with and without Moono)
confirms both supports; no upload of 1.15.0 has been authorized. 1.15.0 also
contains the 1.13.10 hotfix: a pipe that leaves ONI's flow network with a
mixture inside no longer crashes the save, and its mixture survives save,
reload and repair (scenario `mixed-pipe-disconnected-save`).

The solver, its checkpoint bytes and the save format are those of 1.13.9.
Saves made with 1.13.9 and with 1.14.0 development builds load.

Measured on Linux, game build 764025, Moono release 2026.11.26.02 (commit
`36d6e4eb`), unless noted.

## Rephysicalized (from 1.14.0)

- Other mods' Harmony patches on game methods the mod uses no longer turn it
  off ([why and how](playtest-1.14.0.md#before-this-version)).
- Rephysicalized's own oxygen intake reads the owner's mixture
  ([changes](playtest-1.14.0.md#changes)).
- A colony cannot gain or lose Rephysicalized
  ([why](playtest-1.14.0.md#a-colony-cannot-gain-or-lose-rephysicalized)).
- Another mod's own pipe component is served on owned pipes
  ([how](mixed-pipes.md#other-mods-pipe-components)).
- The development measurements are those of the [1.14.0 checks](playtest-1.14.0.md#checks).

## What Moono is

[Moono](https://github.com/tigin-backwards/oni-moono) replaces the game's
Unity player. It runs Klei's own DLLs on CoreCLR (.NET 10) with its own
reimplementation of Unity, TextMeshPro, uGUI and Timeline. It has a list of
"game tweaks" (43 on and 7 off by default in 2026.11.26.02) that speed up
saving, the simulation's bookkeeping and the interface.

## Before this version

A player ran 1.13.9 through Moono (commit `60a1c24`). Their Player.log is on
the share, `issues/01`. The mod stayed off at the main menu with "Your game
reports a version the mod supports (build 764025), but 2730 parts of its code
differ", for example `changed UnityEngine.Color -> UnityEngine.Texture`.

The game was the supported build; the mod's check read the wrong members.

- **The contract's tokens.** Moono's loader re-emits every mod assembly with
  Mono.Cecil. The file and its module ID stay, but the metadata rows are
  renumbered, and the game contract was keyed by those rows' tokens. A Cecil
  read and write of the mod alone reproduces the log: 2,731 differences
  against the player's 2,730.
- **The game's code.** Moono also re-emits the game's assemblies at load.
  Float-to-integer conversions become calls, delegates handed to native code
  are kept alive, and thread-abort catch blocks get an extra call. A method
  Moono rewrote no longer has the contract's form.
- **Unity's types.** They are Moono's own, with other private layouts, enum
  members and member lists.
- **Engine members.** The player's log also named three members the mod used
  that Moono's engine lacks: `Time.captureDeltaTime`, and `Scene.handle` as
  `SceneHandle` with its conversion. Moono replaces each such call by a throw.

## Changes

How Moono is detected, checked and tolerated is documented in
[game-update-resilience.md#moono](game-update-resilience.md#moono).

- **Admission by keys and files.** On Moono the game's identity is its files:
  the SHA-256 of `Assembly-CSharp.dll` and `Assembly-CSharp-firstpass.dll`
  must be the contract's. The firstpass hash is new in the contract. The
  contract is then read by member keys, not tokens.
  - On Unity's player the token check stays the authority.
  - Its Mono control (`tools/game_mono_contract_check.py`) proves the key
    path entry by entry on every package: "key path agrees".
- **Tolerated rewrites, measured.** Against Moono's own rewritten game
  assembly, 47 contract entries are game code Moono rewrote at load: 14
  called members, 10 named lookups and 23 patch targets, about 37 methods.
  They are accepted because the files are the build's, and counted in
  Player.log. The same 47 in a game with Moono's default tweaks.
- **Engine entries.** 41 entries of Moono's own Unity are checked for
  presence and signature only: three struct layouts, the `TextClipping` enum
  (no `Ellipsis` there), `Scene.handle`'s type, `SceneManager`'s member list,
  and 35 member lists of game types that inherit from Moono's
  `MonoBehaviour`. Every other property of game types must match.
- **Engine members removed.** The mod no longer names `Time.captureDeltaTime`,
  `Scene.handle` as `SceneHandle`, or `SceneManager.Internal_SceneUnloaded`.
  All three were in validation modes only.
- **Player.log.** It says `Running on Moono <release> (commit …)` and
  `Game build 764025 verified on Moono v2026.11.26.02: …`. Every refusal
  names the Moono release. Patches of Moono's tweaks on methods the mod uses
  are listed like other mods' patches, as `<- Moono`, and never refuse. A
  Moono release that lacks something the mod uses gets a notice of its own,
  suggesting the previous release or the game without Moono.
- **Frame timing.** Moono times frames from the real clock, so the V-Sync
  clock repair stays off there. Player.log: "on Moono the engine times
  frames from the real clock; the V-Sync clock repair
  (performance.frameTimingFix) stays off."
- **Save waits.** Moono's default `background-save` writes a save into
  `<save>.writing` and moves it over the save once complete. Until then the
  save holds only its header. The mod's checks before a colony loads (menu
  load, resume, the load guard of a refused mod) now wait for that file, up
  to 45 s, then refuse with "still being written".
- **The header-only defect.** Found in the review of Moono's tweaks: a write
  queued behind another has no `.writing` yet, so the first wait missed it.
  On Moono the wait now also holds while the save is header-only. Before the
  fix, the menu check could take such a save for a non-mixture save, and
  with the mod refused the guard would let the colony through.
- **.NET's zlib.** Moono's `fast-save-compression` writes .NET's zlib
  (zlib-ng). The save reader inflates its streams at every compression level,
  with sync flushes too, so it needed no change.

## Left as they are

- **Windows with Moono** cannot be tested here. Moono's Windows player is
  not expected to run under Proton, so that side needs the user's PC.
- **Moono releases** change often and are not pinned. The tested release is
  2026.11.26.02. A later one is supported for as long as its files and
  members meet the rule above, and the offline counts are those of this
  release.
- **Modes that cannot run under Moono.**
  - The owned-load boundary (`load-rejection` validation) needs Unity's own
    engine module; it refuses on Moono with that reason.
  - The all-identity pipe biology falls back with a warning: its signature
    is over raw IL. It does so on Unity's player for 764025 as well.
  - `linux-resume-refused` has no Moono twin: its refusal relies on a patch
    mark that only Mono keeps. `moono-refused-guard` tests the guard with a
    cause CoreCLR sees instead.
- **The display.** The GPU display `:99` has no monitor, and Moono's window
  needs one. Moono runs render on the harness's Xvfb desktop, where Vulkan
  still draws on the GPU.
- **Managed floating point.** Game code may land a last bit away from values
  measured on Unity's Mono. The suite's own checks judge no timing or speed.
  An in-game scenario check that shows a difference gets its own Moono
  expectation; no shared bound is loosened.

## Moono's tweaks against the mod

| Tweak | What it changes | Steps aside for our patches? | Risk to the owner | Where a break would show |
| --- | --- | --- | --- | --- |
| background-save | Compression and the file write move to a thread; the save holds its header until `.writing` is moved over it | No check | Medium: the header-only window, now waited for. "Colony saved" is logged before the file is complete | `moono-autosave-load`, resume, cycle autosaves |
| fast-save-compression | .NET zlib, files about 14% larger | No check | Low: the reader inflates every level; large colonies come closer to its 64 MiB compressed and 256 MiB body bounds | Large-colony loads |
| fast-serialization | Replaces `SerializationTemplate.SerializeData` (the same bytes) | No check | Low: the colony envelope goes through `Sim.Save` | storage-options, save controls |
| fast-save-type-checks | Transpiles `SaveLoadRoot.SaveWithoutTransform` (per-type cache) | Stands down for other transpilers only | Low | Save controls |
| no-save-gc, post-load-gc | No GC after a save; a GC during the load | — | Low: nothing of ours depends on GC timing; the native side keeps no managed pointers | — |
| compact-report-days | Prefix and finalizer on `SaveLoader.Save(BinaryWriter)` | — | Low | — |
| sim-data-check (always on) | Diagnostic on `Game.StepTheSim` | Yes: our prefix and finalizer there make it stand down | None | — |
| fast-sublimation | Skips items blocked on pressure | No check | Low: it calls the patched `Sublimates.Sim200ms` for every item it runs, so the keep-debris prefix runs; it skips only items whose update would change nothing | sublimation, natural-sublimation, storage-options |
| fast-conduit-sorts | Transpiles `ConduitFlow.Sim200ms` | Yes: we patch that method | None | — |
| sim-step-pace, low-fps-timing (off) | `Game.SimEveryTick` stepping and timing | Yes: our prefixes, rechecked from the first frame | None by default | `moono-tweaks-all` |
| fast-jobs | Replaces `JobManager.Run` with Moono's own threads, each item on one thread | No check | Low: our per-thread state lives within one item | Pipe batches |
| path-probe-repair (on) | Repairs path probes from a change feed; submerged changes come from `StepTheSim`'s substance changes | Our `Grid.IsSubstantialLiquidUnsafe` patch is outside its guarded list | Medium-low: a change reaches the feed only through substance-change entries, which the owner publishes | `moono-tweak-verify` (its feed audit) |
| compact-nav-links, paged-path-grids | Load-time IL rewrites of nav and path grids | — | None: the mod touches neither | — |
| exact-worldgen | Worldgen float math in double | — | Low | asteroid steps |
| fetch-by-cell (off in this release) | Fetch tracked per pile | Yes (stand-down list) | Low | `moono-tweaks-all` |
| off-world-fallers (off) | Lands items hanging below a world | Yes | Low | rocket and lander batches |
| fast-load-screen, fix-load-screen-clicks, shared-duplicates | Header reads, click fix, string interning during load | — | Low: our `LoadScreen.DoLoad` prefix is untouched | Menu load |

## New tests

- **Moono twins.** `tools/acceptance_suite.py --platform moono|all` repeats
  every Linux game step through Moono with its default tweaks, as
  `moono-<name>` on the Linux package. Each twin also requires the Moono
  player, its release, the default tweak set and the mod's
  "verified on Moono" line ([how](performance-acceptance.md#moono)).
- **Moono-only release steps.**
  - `moono-refused-guard`: the run's own copy of
    `Assembly-CSharp-firstpass.dll` gets bytes appended
    (`oni_headless --altered-game-file`); the mod must refuse on Moono with
    "changed game file", and its guard must refuse the colony resumed from
    the menu.
  - `moono-cross-save`: Unity's player loads and runs a colony Moono saved.
    The reverse direction is `moono-one-cycle` and `moono-ten-cycle`, which
    load the frozen save Unity's player made.
  - `moono-autosave-load`: the game's autosave, two saves back to back, and
    at once the menu's load check of each (save control `save-pair-read`).
  - `moono-tweaks-none` and `moono-tweaks-all`: the same short colony with
    no tweaks and with all of them.
  - `moono-tweak-verify`: Moono's own verify modes (`ONI_TWEAK_VERIFY=1`),
    judged by its `scripts/qa-tools.py verify`. It must report checks, and
    none may differ.
- **Harness.**
  - `tools/oni_headless.py`: `--moono-tweaks default`; result.json's
    `moono_player.tweaks_effective`.
  - `tools/moono.py fetch`: downloads a release and checks it against
    GitHub's digest and its `SHA256SUMS`.
- **Unit controls.**
  - Contract key path: Mono control and the offline CoreCLR rehearsal
    (`tools/GameContract verify --by-key`).
  - .NET zlib streams.
  - The Moono refusal texts and the save waits.
  - The header-only reading on real saves.

## Checks

Development packages of the 1.15.0 work, Moono 2026.11.26.02 on the Xvfb
desktop with Vulkan on the GPU, Linux, game build 764025. pkg-1150b and
pkg-1150c hold the Moono work alone. pkg-1150d is the tree rebased on
1.14.0 with the 1.13.10 pipe-save fix and the pipe-component fix, so both
supports together. pkg-1150e is version 1.15.0.

| Package | Run | Result |
| --- | --- | --- |
| b | Main menu through Moono, tweaks none (`moono-menu-b`) | passed-scoped-main-menu; "Game build 764025 verified on Moono v2026.11.26.02", native observer attached |
| b | New Spaced Out! colony through Moono, 600 ticks at speed 3, two saves, tweaks none, windowed (`moono-owner-b`) | passed-colony-owner |
| b | The same with Moono's `--headless` (`moono-owner-headless-b`) | passed-colony-owner |
| b | The same package on Unity's player, main menu and colony (`unity-menu-b`, `unity-owner-b`) | both passed |
| c | New Spaced Out! colony with Moono's default tweaks, 600 ticks, saved (`moono-default-owner`) | passed-colony-owner; 47 rewritten entries accepted |
| c | That save loaded under Moono with defaults, 300 ticks, saved (`moono-default-reload`) | passed-colony-owner-load |
| c | The Moono-made save loaded on Unity's player (`unity-loads-moono-save`) | passed-colony-owner-load |
| c | A save of Unity's player loaded under Moono with defaults (`moono-loads-unity-save`) | passed-colony-owner-load |
| c | Scenario `message-edges` under Moono with defaults (`moono-default-edges`) | passed, 5 checks; ledgers 1.7e-17 mass, 2.6e-17 energy |
| c | Scenario `mixed-pipes` under Moono with defaults (`moono-default-pipes`) | passed, 8 checks; ledgers 7.0e-18, 2.4e-17 |
| c | The user's Cycle 130 save with their seven mods under Moono with defaults (`moono-default-c130`) | passed-colony-owner-load at owner frame 387,121; ledgers 2.76e-12 mass (the colony's own figure since Cycle 70), 7.8e-14 energy |
| c | A new colony with all of Moono's tweaks, 600 ticks (`moono-all-owner`) | passed-colony-owner; ledgers 4.9e-17, 1.2e-16 |
| d | Main menu, Moono defaults, Rephysicalized first (`moono-rephys-menu`) | passed-scoped-main-menu |
| d | New colony, Moono defaults, Rephysicalized first, 600 ticks, saved (`moono-rephys-colony`) | passed-colony-owner |
| d | Scenario `rephysicalized` under Moono, both simulations (`moono-rephys-scenario`, `moono-rephys-scenario-vanilla`) | passed-colony-owner (12 checks; ledgers 1.4e-15, 4.6e-17) and passed-colony-vanilla (7 checks) |
| d | Scenario `mixed-pipe-disconnected-save` under Moono (`moono-pipe-disconnected`) | passed-colony-owner, 4 checks |
| d | The same two scenarios on Unity's player (`unity-rephys-scenario-d`, `unity-pipe-disconnected-d`) | both passed-colony-owner |
| e | The 1.15.0 package on Unity's player, main menu (`unity-menu-e`) | passed-scoped-main-menu |
| e | `moono-refused-guard` by hand: the run's copy of `Assembly-CSharp-firstpass.dll` with 35 bytes appended, a Mixed Atmospheres save resumed from the menu | passed-resume-refused; Moono starts with the altered file (lines below) |
| e | `moono-autosave-load` by hand: autosave, two saves back to back, the menu's load check at once | passed-colony-owner; both save controls completed, `PendingAtRead` 1 (below) |
| e | `moono-tweak-verify` by hand: Moono defaults with `ONI_TWEAK_VERIFY=1`, then Moono's `qa-tools.py verify` of the log | passed-colony-owner; qa-tools exit 0, every check 0 wrong (below) |

The refused guard, from its Player.log:

```
[Mixed Atmospheres] Not active on game build 764025 on Moono v2026.11.26.02 (the game's code differs from the supported build); nothing is patched. Details:
  changed game file Assembly-CSharp-firstpass.dll (SHA-256 261d723b5b8632207cf7ed0ffc89ca67062f1062a6fd6eb69edf253ed95dd25c, build 764025 has 33028961e62386c9deb59acd85f334e7d36f38c55f59f5e36df3fd5c8688d7ab)
[Mixed Atmospheres] Only the verified save-load guard is installed.
[Mixed Atmospheres] Colony save refused before loading: This colony uses Mixed Atmospheres, which is not active because the game's code differs from the supported build 764025 on Moono v2026.11.26.02, so it was not loaded. The save has not been changed. Disable other mods or verify the game's files in Steam, then load the colony again.
```

The licensed file kept its hash (`33028961…`); only the run's own copy changed.

The save control reports:

- `autosave: autosave written at owner frame 326`
- `save-pair-read: two saves at owner frame 328; pair-2.sav still being written at the check, accepted after 213 ms; pair-1.sav complete at the check, accepted after 130 ms; MA-Linux-20261126T214855Z-88adb3 Cycle 1.sav complete at the check, accepted after 158 ms`

Player.log adds "Waited 30 ms for the background write of pair-2.sav to finish before reading it". The second save was still being written when the menu's check read it, which is the window the wait covers. Before the fix that read would have taken it for a non-mixture save.

Moono's own judgement of its verify modes (`qa-tools.py verify`, exit 0), the summary lines:

```
ok   fast-anim-cleanup: 91 chunks swept, 99909 skipped, 0 wrong skips
ok   fast-jobs: 40000 runs checked, 0 wrong
ok   fast-offset-tables: 4000 updates, 0 items recomputed, 0 stale items not marked
ok   fast-partitioner-events: 151922 walks skipped, 217 freed handles dropped, 0 wrong
ok   fast-save-type-checks: verify: 1 kept component arrays compared, 0 differ
ok   fast-serialization: verify: the report history identical (8 bytes against 8; 1 writes, 0 mismatches)
ok   fast-serialization: verify: today's report identical (1,809 bytes against 1,809; 2 writes, 0 mismatches)
ok   fast-symbol-overrides: 9287 kept applications checked, 4718120 checks, 0 mismatches
ok   instant-reachability: ... 114717 checks, 0 mismatches
ok   nav links verify (after the load): 19 grids, 3,027,238 links, 0 cell(s) differ from the game's arrays; ... 0 differed from the game's array as they ended
ok   nav links verify (in play): 19 grids, 3,027,297 links, 0 cell(s) differ from the game's arrays; ... 0 differed from the game's array as they ended
ok   path grids verify: 22 tables, 15,252,617 elements, 0 differ from the flat arrays, 0 only mid-write
ok   path-probe-repair: 11783 probes: 11711 repaired ..., 11711 repairs verified, 0 otherwise than a flood, ... 72 floods verified, 0 otherwise than the game's, ... 0 changes the feed missed
ok   trim-anim-lists: 4484 lists trimmed, 72.3 MB freed, 0 differed
```

`path-probe-repair` reported "0 changes the feed missed" beside the owner. The review's medium-low risk, that its feed misses a submerged change, did not occur in this run. `fast-sublimation` and `fast-conduit-sorts` were installed but wrote no verify summary in this 600-tick colony, so their verify modes are not confirmed here. Player.log lists the latter's patch beside the mod's, as other mods' are: `ConduitFlow.Sim200ms <- Moono (Harmony: OniMoono.Host.GameTweaks.fast-conduit-sorts)`.

## Staged package

PENDING(release chain): `artifacts/v1.15.0/accept-pre.sh` up to staging, then
`accept-post.sh`, on the cut revision. Nothing is uploaded.

| Step | Result |
| --- | --- |
| Builds: Linux for Ubuntu 22.04, the Windows build, the Linux build | PENDING |
| Package for game builds 744825, 763796 and 764025 | PENDING |
| Archive verification | PENDING |
| Five of the user's share saves (Cycles 130, 128, 70, 44, 33) with their seven other mods, Unity's player, release folder | PENDING |
| Rephysicalized on the release folder, Unity's player, 6 runs: main menu with it first; new colonies with it first and after it; the two-mod save reloaded; the element notices for the Cycle 130 save with it and for the two-mod save without it | PENDING |
| Moono with its default tweaks on the release folder, 5 runs: new colony; with Rephysicalized first; a save of Unity's player; the Cycle 130 save with the seven mods; without a window | PENDING |
| Two controls: the mod-off stand-in (its patches removed) refuses the Cycle 128 save; the stand-in's patches are accepted | PENDING |
| The Cycle 130 save with the seven mods, Windows build under Proton | PENDING |
| A new colony with Rephysicalized first, Windows build under Proton | PENDING |
| Stage, Workshop dry run | PENDING |
| Load matrix (the three game builds, Linux and Windows under Proton) | PENDING |
| Patch tier on the three platforms with the batches of `rephysicalized`, `message-edges` and `building-emitter` | PENDING |
| The other ten share saves (Cycles 129 to 121, 55, 49) with their seven mods, Unity's player, release folder | PENDING |
| Release tier on the three platforms (`--platform all`, 184 steps): every Linux game step on Unity's player and its Moono twin, the Rephysicalized steps and their twins, the six Moono-only steps, the Windows steps under Proton | PENDING |
