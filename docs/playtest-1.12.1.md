# Mixed Atmospheres 1.12.1

The November 21 playtest supplied a 23-minute Windows recording, a player
log and a Spaced Out! colony saved at owner frame 133995. The recording shows
storage bins set to 5×. The compact status panel also clips the end of its
title at the settings button. Across 23 complete unpaused timing windows,
the median game speed was 0.99× on the player's machine.

## Gameplay and interface

- Pacu, their young and their variants use the game's aquatic suffocation
  monitor. Prolonged stranding kills them after four in-game cycles. Putting
  a fish back into substantial liquid lets it recover at four times the
  exposure rate. The game also protects a fish in transit inside storage.
- Container multipliers apply when a colony loads, when a building is
  constructed and when the player changes the preference. Delivery sliders
  at the previous maximum follow the new capacity. Smaller custom limits
  stay in effect up to the new maximum. Lowering capacity leaves stored
  material in place. Pending deliveries update immediately and reservoir
  inlets resize with their storage.
- The status bar measures its title in the game font and reserves room for
  Settings, collapse and close. Compact buttons have explicit text padding.

Build 744825 exposes the pickupable as a component of sublimating debris.
The mod now reads that component when a sublimation preference is enabled.
The package contract also checks fields Harmony injects into patch methods.
This catches an adapter that would fail while opening the oldest supported
game build.

## Simulation cost

Frequent terrain edits in the submitted colony changed the outline of the
13,062-cell fluid region. Losing its timing history repeatedly put that region
back onto one worker. A small edit at the same region anchor now preserves
its worker estimate. Unchanged regions can retain their solver workspaces
after another region changes. Sleeping regions allocate theirs when needed.
Worker decisions and temporary workspaces stay outside the physics checkpoint.

Two isolated 100-tick replays per version, excluding the first ten ticks from
timing, averaged 249.7 ms per owner tick on 1.12.0 and 199.5 ms on 1.12.1.
The averages of the two p95 measurements were 502.7 and 341.8 ms.
All four runs ended at the same fingerprint `a3ec44600d94410f` and the same
maximum relative mass and energy errors (8.782 × 10⁻¹³ and 3.379 × 10⁻¹⁴).
These timings measure the native simulation on the captured inputs.

The Windows game under Proton exercised swimming, stranding, rescue and death
at expiry. The fixture moved the exposure timer to one second before expiry
for the death check. It also changed capacity on a built bin and gas
reservoir, checked an explicit 1,234 kg delivery limit, and exercised both
sublimation switches. At 640 × 480 the rendered status panel displayed the
complete title and all three buttons.
