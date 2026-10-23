# Native Linux ONI playtesting

`oni_headless.py` runs the licensed Linux game on an authenticated private
Xvfb display with Mesa software rendering, Openbox, screenshots and keyboard /
mouse input. A physical monitor is unnecessary. Keep rendering enabled:
`-nographics` crashed this Unity build, and Unity batch mode does not accept
interactive input, so the runner uses a normal windowed player on the virtual
desktop. Every run uses a fresh profile and a disposable colony. Steam's
license and account checks are never bypassed, and game-owned files are never
packaged with the mod or added to this repository. Installations, pinned build
and hashes are in [the environment report](ONI_ENVIRONMENT.md).

## Start and sign in

From this repository:

```sh
python3 tools/oni_headless.py start
python3 tools/oni_headless.py status
```

Open <http://127.0.0.1:6080/vnc.html?autoconnect=true&resize=scale> on this host.
From another computer, forward the loopback-only viewer first:

```sh
ssh -N -L 6080:127.0.0.1:6080 henry@192.168.1.24
```

Then open that same URL locally. Sign in using Steam's ordinary UI or scan its
QR code with Steam Mobile. Passwords and Steam Guard codes belong in Steam, not
in command arguments or reports. The state directory has mode 0700; the viewer
and VNC listener accept only loopback connections. Steam's remembered session
stays in its private profile for later runs. `status` checks processes; it does
not assert that an account is authenticated.

Linux runs need this private Steam client signed in as the same OS user, with an
account that owns the App ID ([Steamworks initialization](https://partner.steamgames.com/doc/sdk/api?language=english)).
The Windows machine's Steam session cannot supply that context, and account or
session files are never copied. Without a signed-in client the game logs
`connect to global user failed` before the main menu and the runner returns
**3** (`blocked-steam-authentication`). Signing in elsewhere can displace the
session: Steam then shows "Refresh sign in · This account logged in on another
computer" and Klei's account service can return `E_INVALID_TICKET`, so a later
launch may need the account signed in again through the viewer.

## Game installation

The runner defaults to the read-only Linux depot copy at
`/mnt/data/share/oni/OxygenNotIncluded-linux`. Pass `--game-dir` (or
`ONI_GAME_DIR`) to use the private Steam install at
`<state>/steam-home/.local/share/Steam/steamapps/common/OxygenNotIncluded`;
its `Assembly-CSharp.dll` is byte-identical to the depot copy. That install
updates with Steam, and the mod's inspected-build gates reject a newer game
until it is re-inspected.

## Build and run a disposable test

Build against the Linux installation and package into a new empty directory:

```sh
python3 tools/build.py --game-dir /mnt/data/share/oni/OxygenNotIncluded-linux --build-dir build-headless
python3 tools/package_probe.py --native build-headless/libmixed_atmospheres.so --output artifacts/probe-linux-new
python3 tools/oni_headless.py run --package artifacts/probe-linux-new --mode menu
python3 tools/oni_headless.py run --package artifacts/probe-linux-new --mode presentation --timeout 1800
```

Native-domain scenes need an observer-enabled package
(`--oni-native-observer` build, `package_probe.py --native-observer`); see the
[README](../README.md#compile-install-and-enable) and, for a portable Linux
package, the [Ubuntu 22.04 compatibility build](../docs/linux-native-build.md).

Rebuild and package after changing the mod. `package_probe.py` records the
source tree's `source_revision` (git HEAD), `source_dirty` and `source_status`
(`git status --porcelain`) in `package-manifest.json`. These describe the tree
at packaging time; they do not prove the copied binaries were built from it.
The runner rejects packages without both fields (exit 2; repackage them). It
also checks every manifest hash, requires the Linux native library, rejects
unlisted files and symlinks, and rejects packages contaminated with earlier
test reports or configuration. The copied launch package is verified against
the original manifest before configuration is added or a game is launched.

Each invocation creates a new profile and disposable colony (seed 147147) below
`<state>/runs/<timestamp>-<random>/`, holding `result.json`, `Player.log`,
`console.log`, the final desktop screenshot, the profile, the package manifest
and the mod's reports. `--output /absolute/new/directory` selects another
location. The game runs under `bwrap` with a read-only root, a fresh writable
home and XDG directories, a writable evidence directory and `--die-with-parent`
(omitted with `--keep-open`), so personal ONI saves and settings stay outside
the test. It shares the host PID namespace and `/proc`, `/dev` (including
Steam's `/dev/shm` IPC objects) and `/tmp`: Steamworks reads
`~/.steam/steam.pid` and checks that process, and `steamclient.so` reads its
`/proc` entries; a private PID namespace made Steamworks report no running
Steam instance. The run's `~/.steam` is a symlink to the private profile's
`.steam`, which stays read-only to the game. A thin writable executable
directory references the read-only game data and contains the ordinary
development `steam_appid.txt`.

| Mode | Evidence and limits |
| --- | --- |
| `menu` | All six scoped integration checks, the main-menu log marker and, after it, the mod's `Native contract captured without native initialization` line; no colony |
| `presentation` | Automatic disposable-colony presentation/container/nitrogen fixtures and actual Unity captures; native ONI owns the physics |
| `tile-hover` | Four-case real mouse-hover display fixture ([procedure](../docs/mixture-presentation.md#tile-hover-acceptance-procedure)) |
| `snapshot-copy` | Disposable generation, staging and snapshot checks; currently expected to expose the documented custom-state import failure ([interop contract](../docs/interop-contract.md)) |
| `native-observer` | Forwarding observer: hook progress and byte-identical native snapshots, physics left with ONI ([backend contract](../docs/native-backend-integration.md#build-and-observe)) |
| `native-*-domain` | Sealed native domain scenes; see [Native-domain scenes](#native-domain-scenes) |
| `load-rejection` | Not available on Linux: rejected with exit 2 before launch. The mod's load boundary requires the Windows `Assembly-CSharp`/`Assembly-CSharp-firstpass` hashes (`OwnedReservoirLoadBoundary.cs`, `OwnedReservoirLoadValidationCounters.cs`); use the Windows machine |

In `menu` mode the runner waits after `-- MAIN MENU --` for the mod's contract
export to log success, `Native contract export failed` or `Native contract
diagnostics capture failed` (at most 120 s), then 5 s more for trailing
exception lines, before stopping the game. Either failure line fails the run.

`--frames` sets the completed-frame (snapshot acknowledgement) target (1–10,000) and `--timeout` accepts
30–7,200 seconds; larger values need the explicit `--long-run` below.
`--keep-open` keeps a successful test's game for further UI inspection. The
runner does not acknowledge license agreements or arbitrary dialogs;
first-login or content dialogs can need interaction in the viewer. Colony
reports record active DLC IDs; test base, Spaced Out! and pack configurations
separately. Their gameplay and performance gates are not completed by these
fixture modes.

Every mode fails (`failed-mod-log-errors`) if `Player.log` has a line that
names the mod (`Mixed Atmospheres`, `MixedAtmospheres` or the run's mod folder)
together with fail/error/exception, a Harmony `Patching exception` /
`HarmonyException`, or an exception whose stack contains a mod, `HarmonyLib` or
Harmony-patched (`DMD<...>`) frame. Game-only errors and warnings such as
`Missing Anim`, DPI, `kGfxThreadingMode` and the game's YAML worker exception
whose own header contains `Thread was being aborted` at exit do not fail a run.
An adjacent abort cannot suppress an explicit mod/Harmony failure or an
independently attributed exception.

Exit status is 0 for the requested scoped gate, 2 for failure/timeout and 3 for
a Steam authentication block. A mod failure or a present-but-failing
integration probe takes precedence over the Steam block (exit 2), so it is
never hidden. `result.json` records `steam_blocked`, `integration_probe_failed`,
`mod_failure_lines`, `main_menu_contract_export` and `timed_out` separately,
plus the integration and colony reports, observed process exit, game/package
hashes, command and limits. `source_revision`, `source_dirty` and
`source_status` come from the package manifest; `runner_git_revision` and
`runner_git_status` describe the runner's checkout at run time. A failed colony
report or missing main menu never becomes a successful claim. The runner
always keeps `full_mod_completion_verified` and
`authoritative_colony_simulation_ready` false; broader completion requires the
[accepted plan's acceptance evidence](../docs/completion-plan.md).

Post-spawn bookkeeping and evidence-write failures enter cleanup;
`--keep-open` commits retention only after writing the result. A failure to
record a freshly spawned child's identity also attempts bounded direct-child
cleanup and reports incomplete cleanup explicitly. Recorded process-group
cleanup also finds surviving children after their launcher exits, sends TERM,
and escalates to KILL after five seconds. A real exited-launcher control checks
a child that ignores TERM; a reused launcher PID is never signalled. Children
that leave the recorded process group/session are outside this cleanup.

`python3 -m unittest discover -s tools/tests` runs the runner's guards with the
other tool tests. The headless module runs only on Linux; other platforms skip
it before importing the runner.

## Native-domain scenes

The `native-*-domain` modes run the [sealed native domain](../docs/sealed-native-domain.md)
on ONI's actual native timestep; its [scene table](../docs/sealed-native-domain.md#scenes-and-how-to-run-them)
lists each mode, what it checks and its owning document. The runner rejects a
package without the native observer, then requires the scene's
`native-domain.json` facts, the native backend report and the normal
colony/probe report, and checks them with `oni_native_domain_evidence.py`. A
passing offline native test is insufficient for this game-level gate.

Saved-colony controls: `--save-colony` writes an ordinary paired save after an
initial scene; `--mode native-load-domain --load-save <file>
--resave-before-resume` loads it in a fresh process, autosaves paused at frame
zero and checks exact continuation; `--reload-in-process` repeats through the
main menu in the same process. The source file is copied into the new run and
its SHA-256 checked before and after staging. The runner uses a stable private
Dev mod identity across profiles; for a file made with another folder ID, pass
it with `--mod-id` to avoid ONI's Mod Differences dialog. Fixture flags
(`--verify-persistent-actors`, `--verify-live-breathing`,
`--verify-transient-particles`, `--subnormal-carbon-particle`,
`--verify-natural-autosave`, `--verify-save-io-recovery`) are described in the
feature documents linked from the scene table. `tools/oni_native_matrix.py`
runs many scenes and their reloads from one frozen runner and package
([validation matrix](../docs/native-validation-matrix.md)).

### Configured rooms

`native-configured-domain` (gas) and `native-configured-water-domain` (water)
run the configured sealed ABI in a 7×5 room with an interior Neutronium pillar
and ledge; the solids stay native data. They need an observer-enabled package
whose native libraries come from the Ubuntu 22.04 compatibility build. They
accept `--save-colony`, `--load-save` and `--long-run`. Both pass in the Linux
game, with saves and fresh-process loads; scope, commands and numbers are in the
[configured domain](../docs/native-configured-domain.md#linux-game-evidence).

`native-configured-vacuum-domain` uses the same room with a half-filled water
pool and vacuum above it, followed by a real oxygen emission. It supports the
same save/load/long-run options; its audited subcell policy and Linux evidence
are in the [vacuum scene](../docs/native-configured-domain.md#vacuum-scene).

## Separate test display

Add `--private-display` to `oni_headless.py run` to put the game on a separate
authenticated Xvfb/Openbox display. Steam and its login UI stay on the shared
VNC desktop. Each test records its own display process identity, authority-file
path, screenshot and cleanup receipt. The game stops before its window manager
and X server; setup or game-launch failures retire only those owned resources.
The shared-desktop mode remains available for manual `--keep-open` sessions.
`oni_native_matrix.py --private-display` applies this to both processes of every
source/save and reload pair and rejects missing display isolation or retirement
receipts. The Proton runner always uses its own private display. A
private-display native reload passes the frame-zero autosave, both startup
event batches and 120 exact continued frames, matching the shared-display
continuation, with both private display processes verified retired.

For a completed physical scene, `--inspection-seconds 120` keeps the paused
colony visible for two minutes of mouse/visual inspection, then retires the
game and private display. The host monotonic clock measures the hold from the
final passing colony report, so loading time does not consume it; a failed
scene ends immediately. Use 1–300 seconds with a colony mode and automatic
cleanup (not `--keep-open` or `menu`). The hold records its requested and
observed duration; it does not attest any image or hover result. The
`tile-hover` scene requires reviewing each actual PNG before publishing its
receipt ([procedure](../docs/mixture-presentation.md#tile-hover-acceptance-procedure)).

## GPU display

By default the game renders with Mesa's software rasteriser on the Xvfb
desktop, which takes CPU from the fluid solver and caps Unity at a few frames
per second on a full asteroid. `--gpu-display :N` (or `ONI_GPU_DISPLAY=:N`)
renders the game on a hardware X display instead, without the software
renderer settings and with that display's own access control; the result
records the renderer Unity reported (`renderer`). Steam stays on the desktop.

On this host `:99` is the NVIDIA headless X server of
`nvidia-headless-x.service` (RTX 5090). Its screen is 640x480 (no monitor), so
in-game screenshots there show only that corner of the 1280x720 window, and
the shared VNC view does not show the game. Measured on Terra, whole machine:
vanilla at fast speed 421 ms per 200 ms tick with software rendering, 108 ms on
the GPU; under the colony owner Unity runs at about 67 instead of 3 frames per
second, and a frame's interval exceeds its solver time by about 40 ms (frames
10-60).

## Floor arenas

`run --mode colony-vanilla --floor f1` runs a fixed comparison arena of
[docs/base-game-floor.md](../docs/base-game-floor.md) under ONI's own
simulation (the colony owner never starts); `--mode colony-owner --floor f1`
runs it under the owner. F1, F2, F3 and F5 last 120 s after release; F4 lasts
20000 s (100,000 ticks), so it needs `--long-run` and a matching `--timeout`
(about three hours at `--speed 3` on the GPU display). The samples land in the
run's `colony-validation/floor-<id>.txt` and `.json`; `python3
tools/floor_metrics.py <that>.json` prints the metrics.

## Long development runs

Ten ONI cycles are about 30,000 native frames of 0.2 s. `--long-run` is a
separate, explicit request for such a run; it never widens the short fixture
controls. It accepts 1–100,000 frames (default 30,000) and 30–172,800 s
(default 48 h), and only the `snapshot-copy`, `native-observer`,
`native-domain`, `native-liquid-domain`, `native-mixed-domain`,
`native-configured-domain`, `native-configured-water-domain` and
`native-configured-vacuum-domain` scenes.
Fixture flags with exact short windows (`--save-colony`,
`--resave-before-resume`, `--load-save`, autosave, actor, breathing, particle,
I/O and reload controls) are rejected. Saved-colony replay keeps one input
journal per frame and single-save evidence, so it stays a short control for
now. Natural autosaves still occur and use the ordinary paired save path. The
mod enforces the same limits (`ColonyValidationLimits` in
`ColonyValidationConfiguration.cs`); the Python suite checks that both agree.

```sh
python3 tools/oni_headless.py run --package artifacts/probe-linux-new --mode native-domain \
  --long-run --frames 30000 --timeout 172800 --private-display
```

A long run writes **development telemetry**, not acceptance evidence, to
`game-contract-local/colony-validation/development-telemetry.jsonl` beside the
other reports. Each line is one flushed JSON object, written every
`--telemetry-interval-frames` native frames (10–10,000, default 300 ≈ one game
minute) and at least every 60 s of wall time, plus `start`, `final` and
`sim-shutdown` lines. The journal is capped at 20,000 lines; valid limits stay
below 13,000. Fields:

| Field | Meaning |
| --- | --- |
| `Kind`, `SchemaVersion`, `DevelopmentOnly`, `Session`, `Sequence`, `Reason` | `mixed-atmospheres-development-telemetry`, schema 1, always `true`, one id per game process, line number, `start`/`interval`/`heartbeat`/`final`/`sim-shutdown` |
| `NativeFramesProcessed`, `IntervalNativeFrames`, `UnityFrame` | native frames returned by `PrepareGameData` since telemetry started and since the previous line, including world-generation settling and zero-time frames while paused (use `Game` and `NativeDomain` for colony progress); Unity frame |
| `UtcTime`, `WallSeconds`, `IntervalWallSeconds` | host time and monotonic seconds since telemetry started |
| `Game` | game started, `TimeSeconds`, `Cycle`, `TimeInCycles`, paused, speed, live duplicants, `DebugEnabled`, `DebugInvincible` |
| `NativeDomain` | for native-domain scenes: state, session, completed and post-priming validation frames, physical time, last timestep, maximum relative mass/energy error, cumulative and per-line substeps and rejected attempts, publications, native error text |
| `Latency` | per line: `NativeFrameSubmitMs` and `NativeFrameWaitMs` (game-thread wall time inside `SIM_HandleMessage` for `NewGameFrame` and `PrepareGameData`), `ManagedPublicationMs` (owned snapshot, readings, presentation publication and development checks after `PrepareGameData` returns), `PresentationLagMs` (native-domain publication to the first property-texture batch that renders it), `TextureBatchMs`, `UnityFrameMs`; each has `Samples`, `Retained`, `P50`, `P95`, `Max`, `Mean` |
| `Memory` | `GC.GetTotalMemory`, GC counts, resident and peak resident bytes (`VmRSS`/`VmHWM` on Linux, working set elsewhere) |

Percentiles are nearest-rank over at most the latest 4,096 samples per metric;
counts, means and maxima cover every sample. The custom solver runs on the
native simulation worker inside the native frame, and the native ABI exposes no
solver wall clock, so the game-thread wait is an upper bound on solver time
hidden from the game, not a solver measurement.

While the game runs the runner reads only the telemetry file's tail and prints
a compact progress line every 5 minutes; it reads `Player.log` live only in
`menu` mode and reads the complete log once afterwards. The run stops as
`timeout` with `telemetry_stalled: true` and a `telemetry_stall_reason` if the
file has not grown for 900 s, or if a started game's clock (`Game.TimeSeconds`)
has not advanced for 900 s: a modal pause keeps emitting zero-time frames and
lines, so file growth alone does not detect it. A long run that otherwise
passes fails as `failed-long-run-telemetry` unless one complete error-free
session ends in a `final` line whose count matches the colony report.
`result.json` adds `long_run`, `telemetry_stalled`, `development_telemetry`
(line/session/reason counts, first and last line and maxima) and
`development_telemetry_passed`. The runner applies no latency thresholds.
During long runs the native observer report refreshes every 30 s instead of
every second and is still flushed at completion.

**The colony dies without life support.** The harness does not manage the
colony, and a long run fails as soon as every duplicant has died, because ONI
then pauses on its Colony Lost screen and no frame can complete. In the default
disposable colony (seed 147147, no orders) all three duplicants die in the
third or fourth cycle; the vacuum room run loses them earlier, at game cycle
2 / 7,152 owner frames. These unmanaged colonies do not certify ten-cycle
life support; that gate needs a scripted build order or a prepared save.

For the vacuum-owner rerun, the user authorized developer-mode invincibility
on 2026-10-20 to keep the unowned surrounding colony alive. ONI 744825 toggles
it with Alt+F7 after developer mode is enabled. Telemetry reads the game's
actual `DebugHandler.enabled` and `InvincibleMode`; it never sets them. The
colony-loss check stays active. This survival override certifies no life-support
adapter or playable-colony gate. The owned room, vent input, requested frame
count and numerical bounds stay fixed.

**Status.** A native Linux `native-domain` long run (`--frames 10500
--timeout 14400 --private-display`) accepts the opt-in, runs the six-cell
domain for 8,552 post-priming frames and ends as `failed-runtime-validation`
when the duplicants die, with a complete telemetry session (52 lines ending in
`final`). At game speed 1 the colony phase kept real time, 0.208 s of wall time
per 0.2 s frame. Per-line medians of the p95 values: native wait 0.08 ms,
managed publication 74 ms, presentation lag 174 ms, texture batch 7.5 ms;
Unity frame p50 153 ms (llvmpipe). RSS rose from 6.80 to 7.02 GiB in
29 minutes while the managed heap stayed near 1.5 GiB. Mass and energy drift
stayed at 3.5e-15 and 1.1e-12 with no rejected attempts. An earlier run left on
the Colony Lost pause showed that file growth alone misses the pause, which
led to the clock rule and the duplicant check.

A configured gas long run (`--mode native-configured-domain --long-run --frames
8400 --timeout 14400`) passes: 8,401 frames, mass and energy error 2.3e-15 and
2.0e-13, natural autosaves in cycles 2 and 3 admitted and paired, and one
telemetry session of 73 lines ending in `final`. It took 0.365 s of wall time
per frame; per-line medians of the p95 values were native wait 0.083 ms,
managed publication 327 ms and presentation lag 221 ms. RSS went from 6.79 to
6.65 GiB. An earlier attempt failed at frame 6,386 on a body-heat read race,
since fixed ([object heat](../docs/native-domain-physics.md#native-object-heat)).

The configured vacuum request (`--mode native-configured-vacuum-domain --long-run
--frames 8400 --timeout 14400 --private-display`) passes with the authorized
invincibility override: 8,401 owner frames, 1680.200025036931 s, no native fault,
and relative mass/energy errors 8.90e-15/8.61e-19. Both natural autosaves pass paired
readback and pause-state restoration. All three duplicants remain alive;
29 consecutive telemetry lines after activation verify invincibility, in one
complete 34-line session ending in `final`. Geometry, mask, inventory,
rest state, physics and vent mass match the original fixture exactly. The
original request failed on unowned-colony death at 7,152 frames; its failed
result is retained separately. Neither run certifies life-support adapters.
The outer launcher receipt for the rerun was lost. Saved-record revalidation
with the existing classifier, native-domain and telemetry validators passes;
`revalidated-result.json` distinguishes that checker exit from the unrecorded
launcher/player exits. The original wrapper host-load series was not retained;
this run supplies no performance certification.

`tools/configure_visual_demo.py --long-run` prepares the same request for a
manually started scene: 30,000 frames by default, a 48-hour limit and telemetry.

## Inspect and stop

```sh
python3 tools/oni_headless.py screenshot
python3 tools/oni_headless.py input key Escape
python3 tools/oni_headless.py input mousemove 640 360 click 1
python3 tools/oni_headless.py stop-game
python3 tools/oni_headless.py stop
```

Screenshots default to temporary PNGs and may use `--output /path/image.png`.
`input` forwards arguments to the private desktop's xdotool. Stop commands
signal only recorded process groups after checking PID creation identity; a
process that has already exited is not an error. The stop check authenticates
the recorded leader; leader termination alone does not verify termination of
surviving group descendants. Profiles, Steam authentication and evidence
remain available. `setup`, `start`, `stop`, `stop-game` and `run` share one
lock (`<state>/runner.lock`) and read `session.json` only while holding it;
while a run is active they refuse with `Another runner operation is active`, so
interrupt the run to end its game. `status`, `screenshot` and `input` take no
lock. The viewer and desktop run on demand, without a boot service.

## Set up another host

The tools, separate Steam profile and browser viewer live below `<state>`
(default `~/.local/share/oni-headless`). `python3 tools/oni_headless.py setup`
downloads Valve's official bootstrap and extracts distro packages into the
private tool tree, then creates a small websockify virtual environment. It uses
apt's simulation and download operations, never privileged installation, and
records the downloaded package and Steam bootstrap hashes in
`<state>/tools/setup-evidence.json`. The host needs the i386 architecture,
32-bit libc, bwrap, xauth, xdpyinfo, D-Bus, curl, Python and the normal build
tools. Steam uses the system bwrap through its runtime environment options; no
namespace or AppArmor policy is disabled. `--state /path` before the subcommand
selects a different root, although the desktop ports 5907/6080 are fixed and
only one such desktop should run at a time.

## Verified on this host

- **Startup and menu.** On Unity 6000.3.5f2 / ONI 744825 with Mesa llvmpipe /
  OpenGL 4.5, the probe passes all six scoped integration checks (55
  native-message patches, native mixture roundtrip, settings serialization) and
  `menu` returns `passed-scoped-main-menu` with the contract export.
- **Presentation.** `passed-presentation` in a Spaced Out! colony (736×304,
  9 worlds, 3 duplicants): 34 numbered Unity captures, all 18 texture-write
  proofs, the warning/liquid label checks, both appearance comparisons,
  9 container/Core agreements, 5 conservation checks and the container,
  nitrogen, owned-reservoir and shutdown reports. After world generation frames
  took 203 ms p50 and 374 ms p95; that is software-rendering cost, not
  game-scale performance evidence. The account's Workshop subscriptions are
  installed into each fresh profile but stay disabled; only the probe is
  enabled.
- **Native-domain scenes.** See the [sealed native domain](../docs/sealed-native-domain.md#current-evidence).

Pacing pitfall: on llvmpipe, fixture-setup frames took 1.9 s at p50 and up to
16 s, so time-based step pacing gave about one frame per step and the
container-surface coroutine's registrations landed between a texture batch and
its capture. The fixture therefore also waits three Unity frames per step and
for that coroutine before the first texture acceptance, and its 20 s
spawned-surface and 30 s owned-reservoir waits also need 60 frames before
timing out.

Software rendering cannot certify accelerated GPU compatibility or
representative GPU performance. Game-scale performance, the DLC matrix and a
ten-cycle colony remain open; the mod's unfinished gates are in
[implementation status](../docs/implementation-status.md).

## Bounded owner diagnostics

Named scenarios finish at their own fixed acceptance endpoint; `--frames`
does not shorten that endpoint. To inspect only an initial segment, use
`--diagnostic-owner-frame-limit 40` with `colony-owner` or `colony-owner-load`.
The limit is an absolute completed-owner counter, including restored frames.
The host stops its owned game process at the first observed counter at or above
the limit, verifies process-group cleanup before capturing/classifying, and
writes `stopped-diagnostic-owner-frame-limit`, not a scenario acceptance pass.
Real owner/runtime/probe faults still fail. Retained games and completed-scene
inspection cannot be combined with this option.

This is an observed stop, not an exact physical-step cap: publication and
shutdown can overshoot. `result.json` records the requested limit, first
observed counter, final reported counter, overshoot and cleanup verification.
Report those facts; do not describe a truncated scenario as a passed check or
use its timings as completed-scenario acceptance.
