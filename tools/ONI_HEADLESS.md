# Native Linux ONI playtesting

`oni_headless.py` runs the licensed Linux game on an authenticated private
Xvfb display with Mesa software rendering, Openbox, screenshots and keyboard /
mouse input. A physical monitor is unnecessary. Keep rendering enabled:
`-nographics` crashed this Unity build in the earlier investigation, and Unity
batch mode does not accept interactive input. This runner uses a normal
windowed player on the virtual desktop.

## Separate test display

The four-case real mouse-hover scene is also available as `--mode tile-hover`.
Its [Linux instructions](../docs/linux-tile-hover.md) require reviewing each
actual PNG before publishing the bound continuation receipt.

For a completed physical scene, `--inspection-seconds 120` keeps its paused
colony visible for two minutes of actual mouse/visual inspection, then retires
the game and private display. The host monotonic clock measures the hold from
the final passing colony report; loading time does not consume it. A failed
scene ends immediately. Use 1–300 seconds with a colony mode and automatic
cleanup. The hold records its requested/observed duration; it does not itself
attest any image or hover result. `--keep-open` remains a separate shared-desktop
option for retaining a process indefinitely.

Add `--private-display` to `oni_headless.py run` to put the native Linux game on
a separate authenticated Xvfb/Openbox display. Steam and its login UI remain on
the shared VNC desktop. Each test retains its own display process identity,
authority-file path, screenshot and cleanup receipt. The game stops before its
window manager and X server; setup or game-launch failures retire only those
owned resources. The normal shared-desktop mode remains available for manual
`--keep-open` sessions.

`oni_native_matrix.py --private-display` applies this to both processes in every
selected source/save and fresh-process reload pair. It rejects missing display
isolation or retirement receipts. The matrix now hashes its own orchestration
script alongside the runner, reviewer and package. All 200 Python checks pass
with four optional/platform skips. Actual Linux `private-linux-display-native-reload-v1` now passes using frozen
`2db8758` and the `6d7422f` pressure package. The game runs on :123 while Steam
stays on :121. Its frame-zero autosave, two startup batches and all 120 exact
continued frames pass; both private Openbox and Xvfb identities are verified
retired. The resumed physical image is
`10c95fb39894b455343909a486092fa6909a4582600eca25f72ca681c98bb75e`,
matching the shared-display continuation. The complete 5,123,268-byte autosave
body passes readback, with colony-file hash
`6d9b637c5e88ccd876282ddd1a2880288690bdcc3c4bb45819e391910d2bf393`.
The original input remains unchanged. Proton continues to use its existing private display by default.

## Current machine: 2026-10-16

`native-domain` and `native-liquid-domain` test a sealed custom physical owner
on ONI's actual native timestep, with original-flow controls, attempted native
phase exclusions, immutable owned snapshots and continuing native solid heat. They
require an observer-enabled package. See [scope and commands](../docs/sealed-native-domain.md).
The corrected gas run `20261016T194104Z-564b3b` passes 120 native frames after
projection priming, all-six-cell game-grid projection, rendering and save
rejection. It fixes native/public index confusion caused by the native halo;
prior failed runs remain retained. The earlier compressed-water
equilibrium run `20261016T191955Z-858237` passes 30 native frames after projection
priming, actual grid/rendering checks and save rejection. These actor-free
development modes do not certify a playable colony or moving liquid transport.
The corrected water rerun `20261016T194433Z-dc7013` passes 120 frames after
priming with the padded-index filter and nonuniform temperature control.
`native-heat-domain` adds real game heating/cooling messages, their committed
external-energy account, and both managed/native scalar-save rejection. The
runner requires those facts in addition to the normal domain checks.
`native-transfer-domain` additionally injects and selectively consumes oxygen
through the real game APIs. It requires both actual callbacks exactly once,
acknowledged completed-frame receipts, carried kinetic energy, conserved mass
and U+K, and no original native transfer-packet dispatch. Run
`20261016T202909Z-bef415` passes all these checks over 120 post-priming frames.
`native-liquid-transfer-domain` uses real water emission and consumption in
filled cells. The extraction follows a positive native interval, so its receipt
must carry kinetic energy. It requires both actual callbacks, acknowledged
receipts and mass/U+K accounts, including stored compression energy. Use
`--save-colony`, then reload that file with `native-load-domain
--resave-before-resume` to check complete physical continuation. See the
[liquid transfer contract](../docs/sealed-native-domain.md#filled-liquid-point-sources).
`native-actor-domain` tests registered actors with a real Unity `ElementConsumer`,
changed rates, emission intervals, actual blocked/unblocked callbacks, and
disable/removal. Run `20261016T204654Z-021e13` passes 120 post-priming frames,
32 actual consumer callbacks and 40 emission samples, with no actor transfers
after removal and conserved mass/energy. These are explicit gas point boundaries;
producer reservations, spatial/bulk actors and ordinary colony saves remain open.
`native-liquid-actor-domain` uses filled-water consumers and emitters, with the
same rate/batch/blocked-callback/removal requirements and an 80-frame minimum.
The optional ordinary paired save occurs after both actors have been removed;
it does not persist live actor state.
`native-liquid-heat-domain` applies +2 MJ / -0.5 MJ to filled water, then requires
warming of the source and a conductive neighbor, positive represented heat
redistribution and entropy, and the unchanged conservation gates. The isolated
`sealed-water-small-heat` control checks sub-ULP accounting separately; it does
not certify conduction progress. See [rounding](../native/MAC_CONDUCTION_ROUNDING.md).
`native-conduction-domain` verifies internal heat redistribution and entropy
on the native clock, seeded by the same +2,000 J / -500 J game heat messages.
It requires matching thermal session/frame diagnostics and exactly two thermal
stages per solver substep, along with the existing physical/grid/render checks.
`native-gravity-domain` enables downward gravity, checks vertical force/momentum
and includes physical potential in the conservation balance. Actual gas
injection/consumption callbacks must carry the matching boundary potential.
`native-checkpoint-domain` requires a lossless physical restart image whose
independent continuation exactly matches the actual next worker frame. It
records binary images/hashes while retaining the ordinary colony-save guards.
`native-paired-checkpoint-domain` also captures native cells and the physical
owner at one completed native epoch, verifies the challenged native tuples and
validates their combined file. It retains the ordinary save guards until load
binding and complete managed/actor persistence are implemented.
`native-carbon-domain` uses the five-species domain/transfer ABI 2 and verifies
CO2 injection, selective consumption, both actual callbacks, chemical/internal
energy, carried kinetic energy, complete mass accounts and transported CO2 in
the completed presentation reading. Run `20261016T205811Z-f4c02b` passes 120
post-priming frames with a U+K residual of -3.33e-9 J. This supplies physical
CO2; metabolism, scrubbing process reservations and aquatic chemistry remain open.

The optional native SimDLL forwarding observer has a separate `native-observer`
mode. Build/package with `--native-observer`, then use that mode to validate
hook progress and byte-identical snapshots in a disposable colony. It requires
its own observer report and always leaves physics with native ONI. It does not
waive the stricter `snapshot-copy` material-import gate. Instructions and current
evidence are in [the backend contract](../docs/native-backend-integration.md#build-and-observe).

The Windows counterpart is `run_colony_validation.ps1 -NativeObserver`, using
an explicitly opted-in Windows observer package. Windows V60 passes the same
ten-native-timestep/physical-update gate with 47 byte-identical snapshot
channels. The wrapper restores original mods, preferences and generated-world
files after closing its disposable colony. Full Windows instructions and the
retained failed timing control are in the same backend contract.
It now also accepts `-NativeDomain gas|water|heat|transfer|actor|carbon|conduction|gravity|checkpoint|pair` with a
Windows native package. Domain runs use the shared portable evidence checker
and require Windows Unity reports. All six Windows-binary forwarding cases and
three native-domain controls execute successfully under Wine here; those
isolated results remain separate from the actual Windows Unity gate.

The user signed the private Steam client in and installed ONI through it at
`<state>/steam-home/.local/share/Steam/steamapps/common/OxygenNotIncluded`
(Steam build 24423041, game build U59-744825). Its `Assembly-CSharp.dll`
(`6db65299…2ee1`) is byte-identical to the depot copy at
`/mnt/data/share/oni/OxygenNotIncluded-linux`, which remains the runner's
default; pass `--game-dir` (or `ONI_GAME_DIR`) to use the Steam install. That
copy updates with Steam, and the mod's inspected-build gates will reject a
newer game until re-inspected.

Both runs used the Steam install and a package built at 933f0d2. The second
package included the uncommitted pacing fix below.

- `menu`: `passed-scoped-main-menu`. All six probe checks passed, the main
  menu was reached, the contract export completed and there were no mod
  failure lines. Evidence: `<state>/runs/20261016T000301Z-c5a190`.
- `presentation`: `passed-presentation` in a Spaced Out! colony (736×304,
  9 worlds, 3 duplicants). It produced 34 numbered Unity captures; all 18
  texture-write proofs, the warning/liquid label checks, both appearance
  comparisons, 9 container/Core agreements, 5 conservation checks, the
  container, nitrogen, owned-reservoir and shutdown reports all passed, with no
  mod failure lines. Evidence: `<state>/runs/20261016T001005Z-a5b1a6`.

The first presentation attempt (`20261016T000350Z-5d338d`) failed at
`01-normal-air-world.png` with `Fresh: false`. Every written, read-back and
expected byte matched, but the write came from publication generation 19 and
the capture from 25. On llvmpipe, frames during fixture setup took 1.9 s at
p50 and up to 16 s. The 1.5 s step pacing therefore gave about one frame per
step. The container-surface coroutine's six registrations and
unregistrations then landed between the texture batch and the capture. The
fixture now also waits three Unity frames per step and for that coroutine
before the first texture acceptance. The 20 s spawned-surface and 30 s
owned-reservoir waits now also need 60 frames before timing out. At normal
frame rates the seconds still decide. A later fresh-world Windows fixture run
also passes the updated pacing: 34 captures and 18 exact CPU texture-write
proof records, with nine worlds and three duplicants. Its earlier cached-world
startup attempt failed before fixtures; both results are recorded in
[the presentation evidence](../docs/mixture-presentation.md). After world generation the passing Linux run
measured 203 ms p50 and 374 ms p95 per frame. That is software-rendering cost,
not game-scale performance evidence.

The account's 18 Workshop subscriptions are installed into each run's fresh
profile but remain disabled; only the probe is enabled. During the first
presentation run Steam showed "Refresh sign in · This account logged in on
another computer", and Klei's account service returned `E_INVALID_TICKET`.
Steamworks still initialized for the next launch, but a later launch may need
the account signed in again through the viewer.

## Earlier machine state: 2026-10-15

The tools, separate Steam profile and browser viewer are installed below
`/home/henry/.local/share/oni-headless`. No privileged packages, system policies,
personal saves or personal mod settings were changed. The Linux game remains at
`/mnt/data/share/oni/OxygenNotIncluded-linux` and is mounted read-only for tests.
The desktop has been stopped and restarted through the checked-in runner; Steam
is left at its sign-in screen for the user to authenticate later.

The current mod compiled against the Linux declarations with zero errors and
the two previously documented Unity reference warnings. An actual LinuxPlayer
run on Unity 6000.3.5f2 / ONI 744825 passed all six scoped integration checks,
including 55 native-message patches, native mixture roundtrip and settings
serialization. The renderer was Mesa llvmpipe / OpenGL 4.5. The runner returned
**3**, `blocked-steam-authentication`, because Steam's client was not signed in;
the game reported `connect to global user failed` and exited before the main
menu. No colony ran. The shutdown also aborted an outstanding YAML-loading
worker; that message alone does not establish a content defect.

Runs before 17:29Z used a private PID namespace and `/proc`, so Steamworks also
logged `SteamAPI_IsSteamRunning() did not locate a running instance of Steam`
although the private client was running. The 17:29Z run shares the host PID
namespace. That line is gone; `Player.log` now has
`[S_API] SteamAPI_Init(): Loaded '.../steam-home/.local/share/Steam/linux64/steamclient.so' OK.`
followed by `[S_API FAIL] SteamAPI_Init() failed; connect to global user failed.`
That run used `artifacts/probe-linux-runner-verify`, whose files are
byte-identical to `artifacts/probe-linux-agent` and whose manifest records
`source_revision` 82096cc with `source_dirty: true`. It returned **3** with all
six probe checks passing and no mod failure lines. Main-menu behaviour is
still unverified because no Steam account is signed in.

Private evidence:

- `/home/henry/.local/share/oni-headless/runs/20261015T172914Z-a4b488/result.json`
  (host PID namespace) and the earlier
  `/home/henry/.local/share/oni-headless/runs/20261015T122528Z-bf9a5b/result.json`
- Each directory's `Player.log`, `console.log`, `desktop-final.png`, fresh
  profile, enabled-mod file, package manifest and `integration-probe.json`.
- `tools/setup-evidence.json` below the private state root records downloaded
  package and Steam bootstrap hashes. On 2026-10-15,
  `python3 -m unittest discover -s tools/tests` ran 99 tests, 20 of them runner
  guards, and all passed.

This verifies the current startup boundary. Linux main-menu operation, actual
colony screenshots, container fixtures, save/reload, the DLC matrix, long runs
and game-scale performance remain pending. Software rendering cannot certify
accelerated GPU compatibility or representative GPU performance. The mod's
broader unfinished gates remain in [implementation status](../docs/implementation-status.md).

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
current QR code using Steam Mobile. Passwords and Steam Guard codes belong in
Steam, not in command arguments or reports. The state directory has mode 0700;
the viewer and VNC listener accept only loopback connections. Steam's remembered
session remains in its private profile for later runs. `status` checks processes;
it does not assert that an account is authenticated.

Windows Steam at `192.168.1.202` cannot supply this Linux game's local Steamworks
context. The WSL SSH endpoint also refused connections during this setup. Valve
requires a running client in the same OS user context with an account that owns
the App ID; see [Steamworks initialization](https://partner.steamgames.com/doc/sdk/api?language=english).
Windows testing can continue through that machine when accessible, but it
does not verify native Linux execution. Account/session files were not copied.

## Build and run a disposable test

Build against the Linux installation and package into a new empty directory:

```sh
python3 tools/build.py --game-dir /mnt/data/share/oni/OxygenNotIncluded-linux --build-dir build-headless
python3 tools/package_probe.py --native build-headless/libmixed_atmospheres.so --output artifacts/probe-linux-new
python3 tools/oni_headless.py run --package artifacts/probe-linux-new --mode menu
python3 tools/oni_headless.py run --package artifacts/probe-linux-new --mode presentation --timeout 1800
```

Rebuild and package after changing the mod. `package_probe.py` records the
source tree's `source_revision` (git HEAD), `source_dirty` and `source_status`
(`git status --porcelain`) in `package-manifest.json`. These describe the tree
at packaging time; they do not prove the copied binaries were built from it.
The runner requires both `source_revision` and `source_dirty`, so packages made
before this field existed, including `artifacts/headless-linux-20261015` and
the original `artifacts/probe-linux-agent`, are rejected with exit 2; repackage
them. The runner also checks every manifest hash, requires the Linux native
library, rejects unlisted files and symlinks, and rejects packages contaminated
with earlier test reports or configuration.

Each invocation creates a new profile and disposable colony name below
`<state>/runs/<timestamp>-<random>/`. The game runs under `bwrap` with a
read-only root, a fresh writable home and XDG directories, a writable evidence
directory and `--die-with-parent` (omitted with `--keep-open`), so personal ONI
saves and settings stay outside the test. The game shares the host PID
namespace and `/proc`, `/dev` (including Steam's `/dev/shm` IPC objects) and
`/tmp`: Steamworks reads `~/.steam/steam.pid` and checks that process, and
`steamclient.so` reads its `/proc` entries. The run's `~/.steam` is a symlink to
the private profile's `.steam`, so `steam.pid`, `root`, `steam` and `sdk64`
resolve to the private Steam install, which stays read-only to the game.
Steam's initialization and account/ownership checks are unchanged. A thin
writable executable directory references the original read-only game data and
contains the ordinary development `steam_appid.txt`. Game-owned files are never
packaged with the mod or added to this repository.

Modes:

| Mode | Evidence and limits |
| --- | --- |
| `menu` | All six scoped checks, the main-menu log marker and, after it, the mod's `Native contract captured without native initialization` line; no colony |
| `presentation` | Existing automatic disposable-colony presentation/container/nitrogen fixtures and actual Unity captures; native ONI still owns surrounding physics |
| `snapshot-copy` | Disposable generation, staging and snapshot checks; currently expected to expose the documented custom-state import/roundtrip failure |
| `load-rejection` | Not available on Linux: rejected with exit 2 before launch. The mod's load boundary requires the Windows `Assembly-CSharp`/`Assembly-CSharp-firstpass` SHA-256 hashes (`OwnedReservoirLoadBoundary.cs`, `OwnedReservoirLoadValidationCounters.cs`); use the Windows machine |

In `menu` mode the runner waits after `-- MAIN MENU --` for the mod's contract
export to log success, `Native contract export failed` or `Native contract
diagnostics capture failed` (at most 120 s), then 5 s more for trailing
exception lines, before stopping the game. Either failure line fails the run.

`--frames 30` sets the snapshot acknowledgement target (1–10,000). `--timeout`
accepts 30–7200 seconds. Larger values need the explicit `--long-run` below.
`--output /absolute/new/directory` selects an evidence location.
`--keep-open` retains a successful test's game for further UI inspection.
The runner does not acknowledge license agreements or arbitrary dialogs.
First-login or content dialogs can require interaction in the viewer. Active
DLC IDs are recorded by colony reports; test required base/Spaced Out!/pack
configurations separately. Their gameplay and performance gates are not
automatically completed by these fixture modes.

Every mode fails (`failed-mod-log-errors`) if `Player.log` has a line that
names the mod (`Mixed Atmospheres`, `MixedAtmospheres` or the run's mod folder)
together with fail/error/exception, a Harmony `Patching exception` /
`HarmonyException`, or an exception whose stack contains a mod,
`HarmonyLib` or Harmony-patched (`DMD<...>`) frame. Game-only errors and
warnings such as `Missing Anim`, DPI, `kGfxThreadingMode` and the game's YAML
worker exception whose own header contains `Thread was being aborted` at exit
do not fail a run. An adjacent abort cannot suppress an explicit mod/Harmony
failure or an independently attributed exception.

The unit module runs on Linux; other platforms skip it before importing the
Linux-only runner. The current Linux discovery passes 113 tests, including all
34 headless checks. Windows discovery passes with the Linux module explicitly
skipped. Regression cases retain real failures beside shutdown aborts, their
priority over Steam blocks, and the known worker-abort noise handling.

The copied launch package is verified against the original manifest before
configuration is added or a game is launched. Post-spawn bookkeeping and
evidence-write failures enter cleanup; `--keep-open` commits retention only
after writing the result. A failure to record a freshly spawned child's
identity also attempts bounded direct-child cleanup and reports incomplete
cleanup explicitly. These checks have mocked fault coverage; they do not
establish complete descendant-process containment.

Exit status is 0 for the requested scoped gate, 2 for failure/timeout, and 3 for
a Steam authentication block. A mod failure or a present-but-failing
integration probe takes precedence over the Steam block (exit 2), so it is
never hidden. `result.json` records `steam_blocked`, `integration_probe_failed`,
`mod_failure_lines`, `main_menu_contract_export` and `timed_out` separately,
plus the integration and colony reports, observed process exit, game/package
hashes, command and limits. `source_revision`, `source_dirty` and
`source_status` come from the package manifest (what was packaged);
`runner_git_revision` and `runner_git_status` describe the runner's checkout
at run time. A failed colony report or missing main menu never becomes a
successful startup/colony claim. The runner always
keeps `full_mod_completion_verified` and
`authoritative_colony_simulation_ready` false: broader completion requires the
[accepted plan's acceptance evidence](../docs/completion-plan.md).

## Long development runs

Ten ONI cycles are about 30,000 native frames of 0.2 s. `--long-run` is a
separate, explicit request for such a run; it never widens the short fixture
controls. It accepts 1–100,000 frames (default 30,000) and 30–172,800 s
(default 48 h), and only the `snapshot-copy`, `native-observer`,
`native-domain`, `native-liquid-domain` and `native-mixed-domain` scenes.
Fixture flags with exact short windows
(`--save-colony`, `--resave-before-resume`, `--load-save`, autosave, actor,
breathing, particle, I/O and reload controls) are rejected. Saved-colony replay
keeps one input journal per frame and single-save evidence, so it stays a short
control for now. Natural autosaves still occur and use the ordinary paired save
path. The mod enforces the same limits (`ColonyValidationLimits` in
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
| `NativeFramesProcessed`, `IntervalNativeFrames`, `UnityFrame` | native frames returned by `PrepareGameData` since telemetry started and since the previous line; Unity frame |
| `UtcTime`, `WallSeconds`, `IntervalWallSeconds` | host time and monotonic seconds since telemetry started |
| `Game` | game started, `TimeSeconds`, `Cycle`, `TimeInCycles`, paused, speed, live duplicants |
| `NativeDomain` | for native-domain scenes: state, session, completed and post-priming validation frames, physical time, last timestep, maximum relative mass/energy error, cumulative and per-line substeps and rejected attempts, publications, native error text |
| `Latency` | per line: `NativeFrameSubmitMs` and `NativeFrameWaitMs` (game-thread wall time inside `SIM_HandleMessage` for `NewGameFrame` and `PrepareGameData`), `ManagedPublicationMs` (owned snapshot, readings, presentation publication and development checks after `PrepareGameData` returns), `PresentationLagMs` (native-domain publication to the first property-texture batch that renders it), `TextureBatchMs`, `UnityFrameMs`; each has `Samples`, `Retained`, `P50`, `P95`, `Max`, `Mean` |
| `Memory` | `GC.GetTotalMemory`, GC counts, resident and peak resident bytes (`VmRSS`/`VmHWM` on Linux, working set elsewhere) |

Percentiles are nearest-rank over at most the latest 4,096 samples per metric;
counts, means and maxima cover every sample. The custom solver runs on the
native simulation worker inside the native frame, and the native ABI exposes no
solver wall clock; the game-thread wait is therefore an upper bound on solver
time hidden from the game, not a solver measurement.

The runner reads only the file's tail while the game runs and prints a compact
progress line every 5 minutes. If the file has not grown for 900 s, the run
stops as `timeout` with `telemetry_stalled: true`. While the game runs, the runner
reads `Player.log` only in `menu` mode; the complete log is read once
afterwards. A long run otherwise passing fails as `failed-long-run-telemetry`
unless one complete error-free session ends in a `final` line and its count
matches the colony report. `result.json` adds `long_run`, `telemetry_stalled`,
`development_telemetry` (line/session/reason counts, first and last line and
maxima) and `development_telemetry_passed`. The runner applies no latency
thresholds. During long runs the native observer report refreshes every 30 s,
not every second, and is still flushed at completion.

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
process that has already exited is not an error. The current stop check
authenticates the recorded leader; leader termination alone does not verify
termination of surviving group descendants. Profiles, Steam
authentication and evidence remain available. `setup`, `start`, `stop`,
`stop-game` and `run` share one lock (`<state>/runner.lock`) and read
`session.json` only while holding it; while a run is active they refuse with
`Another runner operation is active`, so interrupt the run to end its game.
`status`, `screenshot` and `input` take no lock. The viewer and desktop run on
demand, without a boot service.

For another Ubuntu host, `python3 tools/oni_headless.py setup` downloads Valve's
official bootstrap and extracts distro packages into the private tool tree,
then creates a small websockify virtual environment. It uses apt's simulation
and download operations, never privileged installation. This machine already
has the i386 architecture, 32-bit libc, bwrap, xauth, xdpyinfo, D-Bus, curl,
Python and the normal build tools. Other hosts may need those prerequisites.
Steam uses the system bwrap through its runtime environment options; no
namespace or AppArmor policy is disabled. `--state /path` before the subcommand
selects a different private tool/profile root, although the desktop ports
5907/6080 are fixed and only one such desktop should run at a time.
