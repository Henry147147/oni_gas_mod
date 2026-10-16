# Native Linux ONI playtesting

`oni_headless.py` runs the licensed Linux game on an authenticated private
Xvfb display with Mesa software rendering, Openbox, screenshots and keyboard /
mouse input. A physical monitor is unnecessary. Keep rendering enabled:
`-nographics` crashed this Unity build in the earlier investigation, and Unity
batch mode does not accept interactive input. This runner uses a normal
windowed player on the virtual desktop.

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

`--frames 30` sets the snapshot acknowledgement target. `--timeout` accepts
30–7200 seconds. `--output /absolute/new/directory` selects an evidence location.
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
