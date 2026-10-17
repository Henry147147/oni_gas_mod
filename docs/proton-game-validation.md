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
