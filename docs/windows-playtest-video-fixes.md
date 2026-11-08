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

## Physical observations to investigate

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
replace no fixed acceptance gate. Regional energy observations do not invent a
regional body-heat ledger from the whole world's account.

## Verification record

The full build passes all 93 Linux native tests, the complete managed suites,
and 516 Python tests (11 platform skips). A fresh Windows cross-build passes
all 93 native tests under Wine, compiles the managed adapter and passes the
installed-game hook controls. Both DLLs import only Windows system libraries.
The generated Linux and Windows game contracts each check 2,764 dependencies
with zero differences.

The actual Linux game passes `playtest-diffusers,playtest-pump-vent` at speed 3,
including real machine operation, stationed-duplicant breathing agreement and
the existing owner ledger limits. These bounded controls do not identify the
cause of the exact recorded transient or certify representative-colony speed.

Evidence is under `artifacts/windows-video-fixes-2026-11-08/` in the main
project folder. Actual menu/hover checks, cluster lifecycle/save checks and
final immutable-package delivery are tracked separately. The package remains
a private playtest; historical full-release acceptance and actual Windows OS
acceptance are not claimed for a new revision by inheritance.
