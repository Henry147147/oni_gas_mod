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

Development packages of the 1.15.0 work (pkg-1150b/c), Moono 2026.11.26.02
on the Xvfb desktop with Vulkan on the GPU:

| Run | Result |
| --- | --- |
| Main menu through Moono, tweaks none (`moono-menu-b`) | passed-scoped-main-menu; "Game build 764025 verified on Moono v2026.11.26.02", native observer attached |
| New Spaced Out! colony through Moono, 600 ticks at speed 3, two saves, tweaks none, windowed (`moono-owner-b`) | passed-colony-owner |
| The same with Moono's `--headless` (`moono-owner-headless-b`) | passed-colony-owner |
| The same package on Unity's player: main menu and colony (`unity-menu-b`, `unity-owner-b`) | both passed |
| New Spaced Out! colony with Moono's default tweaks, 600 ticks, saved (`moono-default-owner`) | passed-colony-owner; 47 rewritten entries accepted |
| That save loaded under Moono with defaults, 300 ticks, saved (`moono-default-reload`) | passed-colony-owner-load |
| The Moono-made save loaded on Unity's player (`unity-loads-moono-save`) | passed-colony-owner-load |
| A Unity-made save loaded under Moono (`moono-loads-unity-save`) | PENDING(moono-loads-unity-save) |
| Scenario `message-edges` under Moono with defaults (`moono-default-edges`) | PENDING(moono-default-edges) |
| Scenario `mixed-pipes` under Moono with defaults (`moono-default-pipes`) | PENDING(moono-default-pipes) |
| The user's Cycle 130 save with their seven mods under Moono (`moono-default-c130`) | PENDING(moono-default-c130) |
| A new colony with all tweaks (`moono-all-owner`) | PENDING(moono-all-owner) |

## Staged package

PENDING(release chain)

| Step | Result |
| --- | --- |
| Release builds (Linux for Ubuntu 22.04, Windows), package for game builds 744825, 763796 and 764025, archive verification | PENDING(release builds) |
| Release tier on Unity's player and Windows under Proton (`--platform both`), including the Rephysicalized steps (`rephysicalized`, `linux-menu-rephysicalized`) and `mixed-pipe-disconnected-save` | PENDING(release tier both) |
| Release tier through Moono (`--platform moono`): the twins, including `moono-menu-rephysicalized` and the `rephysicalized` batch twin, and the Moono-only steps | PENDING(release tier moono) |
| The user's share saves with the seven other mods on the release folder, Unity's player and Moono | PENDING(share saves) |
| Rephysicalized on the release folder, Unity's player and Moono: main menu with it first; a new colony with it first, saved and reloaded | PENDING(rephysicalized release folder) |
| The Cycle 130 save with the seven mods, Windows build under Proton | PENDING(windows c130) |
| Stage, Workshop dry run | PENDING(stage) |
