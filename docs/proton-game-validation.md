# Windows game validation from Linux

`tools/oni_proton_headless.py` runs the licensed **Windows Unity player** through
an installed Proton tool on a private Xvfb display. It uses the same native
scene and save/load acceptance checks as the Linux and Windows runners. This
provides real WindowsPlayer execution on Linux. Testing on the Windows OS,
including the MSVC build and Windows-specific system behavior, remains a
separate gate.

Start the private Steam desktop with `tools/oni_headless.py start` and sign in
with an account that owns ONI. Install a complete official Proton tool; pass its
directory containing `proton`, `version` and `files/bin/wineserver`. Build the
managed adapter against the Windows game assemblies, and package both Windows
native DLLs with `tools/package_probe.py --native ... --native-observer ...`.
The runner verifies the package hashes and pinned game assembly/SimDLL hashes.

```sh
python3 tools/oni_proton_headless.py \
  --proton /path/to/proton-experimental \
  --game-dir /path/to/Windows/OxygenNotIncluded \
  --package /path/to/windows-probe \
  --mode native-liquid-gravity-domain --frames 120 --save-colony \
  --output artifacts/windows-water-gravity-001
```

The runner holds the Linux runner's lock for the complete test. Finish or pause
a test orchestrator between game processes before switching targets. A running
game causes the new invocation to decline. Each invocation creates a fresh
Wine prefix, Windows Documents/mods/save profile, protected X display and log
directory. The existing Steam session and licensed installation are mounted
read-only. Proton's own directory is writable for its `dist.lock` and tool
fixups. Steam license checks remain active. The runner never changes the shared
Steam desktop's display or signals another prefix's Wine processes.

`result.json` records the actual target, Linux host, Proton version, package
revision and hashes, raw mod reports, logs and acceptance result. A successful
source save exports `host_saved_file` and its verified SHA-256 for the next
invocation. Use that host path with `--mode native-load-domain --load-save ...
--resave-before-resume` to check paused frame-zero saving and exact fresh-process
continuation. Add `--reload-in-process` to repeat through the main menu in the
same Unity process. Input saves are copied and checked for mutation. C: and Z:
paths in reports must resolve to real files inside this invocation's directory;
UNC paths, traversal and symlink escapes reject.

The receipt also records a limited set of nonsecret compatibility settings:
`MONOMOD_RunningOnWine`, `MONOMOD_DMDType`, `PROTON_USE_WINED3D` and `WINEDEBUG`.
Unset overrides stay absent; the inherited environment is never exported.
`PROTON_USE_WINED3D` keeps its default `1` and now honors an explicit inherited
value, so a recorded renderer comparison actually applies that setting.
Runtime comparisons must retain the tool's exact version and download hashes
separately. Public GE-Proton builds are alternative Wine/Proton experiments,
not evidence for the corresponding Valve build or for the Windows OS.

A main-menu boot alone does not pass a scene. Passing a scene requires the
Windows integration probe, main-menu contract export, completed colony report,
native ownership, rendering and physical conservation checks. Failures remain
in the evidence directory, including prefix initialization and cleanup failures.
All first-playable and whole-colony completion fields remain false.

`--mode menu` is a separate startup control, accepting a core-only probe or an
observer package. It requires the scoped integration checks and successful
main-menu contract export, then retires its own game process. Its success is
`passed-scoped-main-menu`; it never passes native-domain ownership. This lets
startup failures be compared with the optional native observer disabled.

The initial licensed-player bootstrap reaches the U59-744825-S main menu and
exits normally with Proton Experimental `experimental-11.0-20261014-x86_64`.
That bootstrap did not load the mod; native feature verification follows
separately.

The first public runner attempt retained a missing compatibility-root failure;
the runner now creates that directory before Proton takes `pfx.lock`. The next
attempt loads the Windows mod and passes its six scoped probe checks, then
fails game startup at native tag creation and contract export. Its failed
reports and screenshot remain retained; it supplies no passed physical scene.
The tag adapter now rewrites only the inspected enum stringification and
checks real helper calls at startup. Both old and new adapters pass a separate
experiment with the licensed Windows Mono DLL under Wine; that experiment
does not reproduce the Unity failure or certify the repaired player. The
WindowsPlayer/Proton retry is tracked independently.


The later GE-Proton10-34 control uses version `1774238111 GE-Proton10-34`,
with release archive SHA-256
`51c580b66a833c73998fe00f0717eeac57197654040a2f2ed5189e3ee68d773d`
verified against the author's checksum. Its default
`ge10-native-water-gravity-reload-v1` still fails the tag startup check.
With the explicit `MONOMOD_RunningOnWine=false` diagnostic override,
`ge10-windows-runtime-false-reload-v2` reaches the actual restored colony at
physical frame zero and delivers both native startup batches. The ordinary
SaveActive pre-animation then stalls. A controlled stop retains the failed raw
runner result; no resumed physical frames or successful load is claimed.

During that stall, Unity's `unscaledTimeAsDouble` stays at about
65,358,361,937 s and `unscaledDeltaTime` at about 10 microseconds while realtime,
Stopwatch and UTC advance normally. This is near the signed IntegerTime limit;
clock saturation is an inference, not a diagnosed root cause. Unity documents
[the maximum representable seconds](https://docs.unity.com/en-us/engine/6000.3/script-reference/unity/integertime/discretetime/maxvalueseconds).

The independent `ge10-independent-clock-control-v2` repeats the frozen unscaled
clock at the main menu, with no Harmony patches installed by the diagnostic
mod, no custom physics invoked and neither custom native library loaded.
Its 20 samples cover ordinary time scale 1, pause and resumed time scale 1;
unscaled time stays fixed even while resumed scaled time and realtime advance.
The clock-only experiment completes and quits, but its public runner correctly
reports `failed-integration-probe` because this diagnostic supplies none of the
mod's normal contract evidence. It does not pass a native feature. These
controls isolate an engine/runtime problem without bypassing save callbacks,
forcing animation completion or inventing physical time. A supported runtime
and successful fresh-process continuation remain required.


`experimental11-independent-clock-api-control-v1` and
`ge10-independent-clock-api-control-v1` extend that independent diagnostic with
actual Win32 and NT timer calls. Both complete normally (19 and 18 samples),
with custom physics and both native libraries absent. QueryPerformanceCounter
and NtQueryPerformanceCounter advance at the reported 10,000,000 Hz; GetTickCount64,
multimedia time, realtime and UTC advance normally. Unity unscaled time remains
fixed near 65,358,361,937 s in both tools. The public raw runner results remain
`failed-integration-probe`; timer diagnostics do not supply integration evidence.
The normal API counters narrow the investigation to their use by the engine;
they do not establish a root cause or a supported runtime repair. The retained
comparison is `clock-api-after-pressure-workflow-v1.json`.


The subsequent constant-origin experiment changes only the coordinate origin
of `CLOCK_MONOTONIC_RAW`, preserving its nanoseconds and timer rate. Native
controls verify elapsed-time agreement and reject invalid/future origins. The
GE10 clock-only run confirms the intended Win32 counter shift (about 646 s
instead of 142,487 s), with the same 10 MHz frequency and advancing realtime.
Unity unscaled time remains frozen. A following unmodified baseline reproduces
it. This rejects the large-counter-origin hypothesis; the temporary diagnostic
shim is excluded from the mod and normal runtime configuration. Evidence is
`clock-origin-after-pressure-workflow-v1.json`.

`clock-window-manager-after-private-linux-workflow-v1.json` then compares the
same clock-only DLL with and without private Openbox. Both complete, report
`Application.isFocused` and `runInBackground` true throughout, and retain the
frozen Unity unscaled clock while realtime advances normally. Missing window
focus/window-manager startup does not explain the failure. Both public raw
results remain failed integration probes, and neither control loads the native
mod libraries or advances custom physics.

The read-only `ge10-clock-native-getters-control-v1` additionally resolves the
actual Unity time getters without patching them. Its 21-sample independent
diagnostic completes: unscaled time remains frozen, although scaled time,
realtime and the OS counters advance. The fixed-unscaled cache also starts near
the IntegerTime limit outside a fixed update. This further narrows observation;
it does not establish the underlying initialization error.

The subsequent `ge10-clock-fixed-step-control-v1` sets the existing nominal
0.03-second fixed step once in the independent menu diagnostic. It completes
22 samples across pause/resume, but unscaled time stays frozen. Reinitializing
that setting does not repair the clock. Neither experiment installs mod Harmony
patches or invokes custom physics; both public results remain failed integration
probes. These diagnostic changes are excluded from the shipped mod.

`ge10-clock-dxvk-control-v1` tests the explicit renderer override
`PROTON_USE_WINED3D=0` with the host's software Vulkan ICD. It exits at graphics
startup with a DXGI/D3D11 failure before reaching the menu or collecting clock
samples. It is a failed renderer control, providing no clock comparison and no
feature acceptance. The normal WineD3D default remains unchanged.
