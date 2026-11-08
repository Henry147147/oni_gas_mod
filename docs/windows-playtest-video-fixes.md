# Windows playtest follow-up, 2026-11-08

The user's 29:35 Windows recording of the latest private candidate showed a
persistent empty cell in an existing colony, a status panel covering management
screens, and oversized mixed-pipe hover with positive quantities rounded to
zero. The follow-up fixes those independently of the numerical solver. The
recording and detailed screenshots remain private under ignored artifacts.

## Changes

- **Saved empty-cell holes:** supply actual registered-world membership after
  start and restore, and refresh it through rocket-world lifecycle changes.
  Native vacuum alone cannot distinguish an inherited owner wall inside a room
  from an empty cluster slot. The existing custody path can now repair the
  former while excluding actual cluster gaps and retired/quarantined rocket
  slots. Base-game import uses the same layout/world-ID criterion. Invalid
  membership batches refuse atomically. The restored-owner regression keeps
  the old inferred-mask route as a control that reproduces the hole.
- **Management screens:** read the actual active Research, Skills, Starmap or
  Cluster Map screen. The supported game's fullscreen helper retains empty
  toggle entries and misses those screens. Hide both the mod panel and its
  Settings click blocker while they are open.
- **Mixed-pipe inspection:** identify a connected pipe packet by flow manager
  and cell, so overlapping building/pipe cards show it once. Separate building
  storage remains separate. Hover groups mass shares without dropping species;
  selected details retain individual masses, traces and germs. Positive tiny
  quantities use scientific notation rather than zero. Ordinary mass units,
  Celsius/Fahrenheit/Kelvin preferences and selected significant figures are
  retained.

`MacClosedWorld` arithmetic, numerical defaults, fixed gate inputs/bounds and
checkpoint encoding are unchanged. The world-membership API is additive and
requires a matching native/managed bundle. Its mask is reconstructed session
metadata; setting it changes no checkpoint bytes. Trajectories change where a
previously excluded room cell is repaired or genuinely unregistered margins
are excluded from import.

## Physical investigation

The diffuser cell (88,125) in the recording changed from 10.79 °C to 19.35 °C
in half a second, with simultaneous mass/pressure changes. A later vent cell
also showed a cold pulse and independently refreshed panel/hover readings.
These establish visible transients, not an energy-accounting defect. The real
diffuser's minimum output temperature is 303.15 K; a simple cold output setting
is not their explanation. Source/flow pressure work must be considered before
changing the numerical behavior.

The vanilla Breathability diagnostic averages duplicants' oxygen-provider
status over 50 game seconds. It does not measure the fraction of map tiles
colored breathable. Its dip to 13% at 27:20 warrants checking actual head-cell
and provider history; the recording's cursor was elsewhere. No matching save
or Player.log for this recording was supplied, so the exact recorded event
cannot yet be replayed.

Two explicit diagnostic scenarios, `playtest-diffusers` and
`playtest-pump-vent`, exercise real machines with positioned duplicants. They
record per-frame cell temperature, mass, pressure, breathing selection,
provider state, machine operation, coherent regional U/K/G observations and
the existing owner conservation checks. They add no ordinary-game work and
replace no fixed acceptance gate.

The controls reproduce cold pulses: 10.9227 °C at a diffuser and 4.7499 °C at
a vent. Across 2,640 probes, owner and presentation temperatures agree within
2.92e-5 K. The source contract injects gas at rest with thermal internal energy,
`dm*(offset+cv*T_in)`, while subsequent pressure work and transport redistribute
energy. During one pulse, a diffuser cell cools 7.907 K while its approximate
thermal internal energy increases 17.186 kJ as mass rises. This supports a
source/flow explanation; it is not a demonstrated numerical energy loss.
Stored kinetic energy alone is too small to explain the changes. Exact regional
heat/work closure was not captured, so detailed local thermodynamic correctness
remains open. Supplying flow enthalpy instead would be a separate source-model
decision, not a justified accounting repair from this evidence.

All four stationed duplicants retain 100 breath, with no provider-empty or
out-of-oxygen samples and no breathing/presentation disagreements. The original
13% diagnostic dip remains unreproduced. Global maximum relative mass/energy
errors are 8.37e-16/4.99e-17. The full physical analysis is private evidence at
`artifacts/windows-video-fixes-2026-11-08/physical/CONCLUSION.md`.

## Verification record

The full build passes all 93 Linux native tests, the complete managed suites,
and 516 Python tests (505 pass, 11 platform skips). A fresh Windows cross-build
passes all 93 native tests under Wine, compiles the managed adapter and passes
the installed-game hook controls. Both DLLs import only Windows system libraries.
The final Linux and Windows game contracts each check 2,768 dependencies with
zero differences. Both managed builds and relevant inspection/hook controls
were rerun after the status-format correction and UI fixture addition; the
Python suite also passes on the final code revision. Those follow-ups change
no native code.

Actual-game evidence is scoped as follows:

- Linux physical controls pass at speed 3 using a provisional pre-commit package
  of the same native changes. They are diagnostic observations, not final-package
  Windows or representative-colony performance acceptance.
- Clean `01d7c32` Linux package passes terrain order, doors, falling, breathing,
  breath particles, deoxidizer and mixed-pipe readers, plus an ordinary save.
- Final clean `edc362a` WindowsPlayer under Proton passes registered-world,
  rocket creation/ports/retirement/slot reuse/flight, terrain-order and mixed-pipe
  scenarios, then an ordinary save at owner frame 220. A fresh process restores
  that save and completes a same-process main-menu reload with a new owner
  session, preserved flags 29, observed shutdown and matching restored-image
  SHA256 `932cce25d4041e208b63bd5727b674a75dd66d11557e07ba2737122764797059`.
  This checks restored images, not byte-exact active continuation.
- The final WindowsPlayer UI fixture passes. Inspected Research and Skills
  screenshots show the mod panel hidden. Linux checks additionally select a
  roster entry and confirm the panel returns on close. A full-client Windows
  hover shows a single connected packet card, `1E-16 kg`, all three constituents
  and positive fractional germs within the viewport. Some automated menu clicks
  missed their targets; only the inspected aligned captures support menu claims.

An independent review of `b6c26ef`..`dcfd6a0` found no defect to fix. It
repeated the checks on fresh builds: 93/93 native tests, 516 Python tests
(11 skips), world-layout, hook and inspection suites. It also restored a
saved colony, which the checks above did not: the clean `edc362a` Linux
package loaded `full-colony-base-v1` (saved at `102a6d9`, before either
hole fix). On the first frame, custody reopened nine inherited closed cells
that ONI held as vacuum. The run then completed 300 frames with no native
mass exchange and relative mass/energy errors of 3.8e-15/1.0e-15. The Spaced
Out full-colony save was not rerun. Steam on this host had been signed in on
another computer, so Spaced Out content could not activate.

## Delivered private candidate

Code revision: `edc362a97f87986c4aa6a449d636c89a508f5b0a` (clean manifest).
The later verification-document commit does not alter this bundle.

Private delivery directory:
`/mnt/data/share/oni/playtest/MixedAtmospheres-beta-edc362a/`.
The ZIP is `MixedAtmospheres-private-windows-edc362a.zip`, SHA256
`64af9f0d3c5d2a645466b4c0e939a925efd9b9bac56b8989a576c10c7e0743a3`.
It includes the existing Windows installer, both matching native DLLs, the
managed adapter, checked contract and third-party notices. ZIP entries and all
manifest hashes are verified before copying; the delivered copy has the same
hash. No public release or Workshop upload was made.

Evidence is under `artifacts/windows-video-fixes-2026-11-08/` in the main
project folder, with `REPORT.md` and `verification/summary.json` as indexes.
The package remains a private playtest. WindowsPlayer was tested through
Proton/Wine on Linux; actual Windows OS acceptance, the exact recorded save,
long-run acceptance and representative-colony speed are not established here.

## Memory growth in the edc362a playtest

The user's new Spaced Out! colony on `edc362a` (Always simulate, two solver
steps per tick, evaporation below boiling on; 66,029 owned cells) reached
52 GB of RAM after about 1,800 owner frames, roughly 26 MB per frame; its
Player.log records no error. The `held` field of the `perf frames` lines is
unrelated: it counts the game's 1/60 s sub-ticks that pacing held while the
owner was busy.

Cause: the Windows owner DLL's allocator never retired the heap of a thread
that ended, so everything such a thread had allocated stayed committed. The
MinGW build links mimalloc, whose thread-exit hook runs at
`DLL_THREAD_DETACH` and reads the thread's heap from a `__thread` variable.
MinGW emulates `__thread`, and winpthreads frees that storage before the
loader's detach callbacks, so the hook saw no heap and did nothing (a probe
callback at the same point no longer finds the thread's heap). The game kernel
ends threads every frame: the bounded regional tick starts a coordinator
thread for each solver interval that runs a dominant region on inner
workers, and a rebuilt tick replaces its worker pools. Linux uses glibc and
is unaffected. The fix builds mimalloc with `MI_WIN_INIT_USE_FLS`, whose
fiber-local callback receives the heap itself
([backend integration](native-backend-integration.md)). Solver arithmetic,
numerical defaults and checkpoint bytes are unchanged.
