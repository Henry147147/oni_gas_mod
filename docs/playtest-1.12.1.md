# Mixed Atmospheres 1.12.1

The November 21 playtest supplied a 23-minute Windows recording, a player
log and a Spaced Out! colony saved at owner frame 133995. The recording shows
storage bins set to 5×. The compact status panel also clips the end of its
title at the settings button. Across 23 complete unpaused timing windows,
the median game speed was 0.99× on the player's machine.

## Gameplay and interface

- Pacu, their young and their variants use the game's aquatic suffocation
  monitor. Stranded fish show the suffocation status and die after four in-game cycles. Putting
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

## Existing colony and player log

The submitted save restored at frame 133995. With bins and gas reservoirs
at 5×, all 187 bins reported 100,000 kg for both physical capacity and the
delivery slider. The three gas reservoirs each reported 5,000 kg. After
saving, restarting the game and loading at 10×, the bins reported 200,000 kg,
the 49 liquid reservoirs 50,000 kg, and the three gas reservoirs 10,000 kg.
The original save's SHA-256 remained
`c59a2df1726e476cebc193efe05f8503fcb0d84b8c6d92bdf0763fd777364f1f`.

A separate 1,200-tick Linux run from the submitted save reached frame
135195, produced the natural cycle 46 autosave, and wrote a manual save.
An in-game delivery test first reproduced an outstanding 20,000 kg granite
order after the bin grew to 60,000 kg. With the fix the pending order
immediately requested 60,000 kg. Shrinking a bin below its contents kept
all 45,000 kg and preserved a custom 1,234 kg delivery limit.

The player log's six duplicate pipe endpoint warnings came from the gas,
liquid and solid outputs of the two Spaced Out! warp conduit receivers.
At each cell the old and new registrations named the same receiver, position
and conduit type. The older package emitted the same six warnings while
restoring this save. ONI also logged two obsolete meteor season IDs and a
missing PufferFish prefab with the Aquatic content disabled.

The eleven warp-route checks passed under the Windows player. Gas and liquid
each carried both constituents between both pairs of worlds. All four routes
emptied their input and shared storage. The mass and energy accounts closed.

## Live performance

Across the 23 unpaused windows in the submitted log, the game advanced
689.9 seconds in 781.4 wall-clock seconds (0.883× overall). The median of
the individual windows was 0.992×. The player selected the second speed
button in 21 of those windows. Unity and wall time agreed at a median
15.9 ms per rendered frame. The median simulation tick cost 170.1 ms,
with 139.8 ms in fluid flow.

On the Ryzen 9 5900X test host, the Windows player under Proton ran the
same save on each packaged version. The table uses frames 134146–134445,
two complete unpaused windows per run. Actual speed divides game time
advanced by the total wall time across both windows.

| Selected speed | Actual speed on 1.12.0 | Actual speed on 1.12.1 | Mean solver time on 1.12.0 | Mean solver time on 1.12.1 |
| --- | ---: | ---: | ---: | ---: |
| 2× | 0.600× | 0.585× | 213 ms | 210 ms |
| 3× | 0.592× | 0.605× | 209 ms | 204 ms |

Flow in the main base region still took most of the solver's time. Unity's
frame and the game's handoff added substantial time outside the solver.
An instrumented unpaused 15-second window recorded 162 rendered frames,
a 26 ms median and a 620 ms 95th percentile. No managed collection occurred
in that window. This diagnostic ran on half the host's cores.

The Windows native benchmark under Wine replayed the first 100 ticks twice
per version. Excluding ten warm-up ticks, the mean fell from 350.8 to 294.7 ms
(16%) and the average 95th percentile from 659.2 to 475.2 ms. All four runs
ended at `dd67ef9650092d40`. Later in the same colony (frames 134146–134445),
mean time was 251.1 ms on 1.12.0 and 252.5 ms on 1.12.1. The worst tick
fell from 685.7 to 501.7 ms. Both Windows versions reached `1535aaab54634db1`
with the same relative mass and energy errors.

On Linux, a longer replay measured frames 134146–134445 after 150 warm-up ticks.
With the machine reserved for the benchmark, two runs per package averaged
175.4 ms per tick on 1.12.0 and 170.9 ms on 1.12.1. The average of the two
95th percentiles rose from 210.2 to 220.9 ms. The average worst tick fell from
548.0 to 398.3 ms. All four runs reached fingerprint `c658996d69343921`
at frame 134445, with identical mass and energy errors. The early 20% gain
was concentrated just after loading and during frequent terrain edits.

A second Windows-player comparison ended validation reporting after five
ticks and let the colony run on the 2× button. Across the same
3 unpaused windows (frames 134146–134595),
1.12.0 achieved 0.784× and 1.12.1 achieved 0.781×. These timings were collected
on Linux with the licensed Windows player under Proton. On 1.12.1 the three
windows averaged 256.0 ms between simulation ticks. Each tick advances
200 ms of game time. Fluid flow averaged 189.9 ms per tick. The game's
data update and copy back into the game averaged 25.1 ms and 4.7 ms
respectively. In the later native replay, the 13,167-cell base region spent
49.3 ms per tick solving pressure and 21.3 ms sharpening interfaces.
Two additional runs used the same 450 ticks and changed only the Performance
tab's worker count. Each count ran once. Eight workers achieved 0.770× and 16 achieved
0.809× on this 12-core host. The default used 12 workers.
Sustained 1× on this colony and the 3× target remain open.

## Release qualification

The packaged source is `08faa66`. Both players loaded, saved and reloaded
on ONI builds 744825, 763796 and 764025. Each of the six adapter contracts
also detected all 15 deliberate compatibility errors. The frozen platform
builds passed 94 Linux and 95 Windows native tests. The managed suites,
565 Python cases and 12 fixed colony comparisons completed. Four of the
Python cases were Windows-only and passed separately with Windows CPython
and PowerShell under Wine.

All seven patch acceptance steps passed. The release sweep passed 45 of 47
steps on its first run. The ordinary Spaced Out! new-game driver clicked the
old Settings close position and missed the stock Mod Events dialog. After
correcting the driver, both remaining steps passed with the same package.
The driver started a new colony through the menu, saved it, quit and
cold-loaded the manual save. The settings source and reload controls also ran again with
a visual check that each screenshot shows the requested tab. The corrected
harness passed the Python suite with 566 cases, including the new negative
control for screenshots left on the wrong tab. The four Windows-only cases
use the same independently tested utility code.

These suites use the platform-specific harness packages built from the release
revision. The combined ZIP ships the Ubuntu 22.04 compatibility build on
Linux. The six platform and game-build combinations and submitted-save checks ran on that combined
package separately. The 15 Linux gameplay batches covered 69 scenarios and 326 checks.
The eight Windows gameplay batches covered 41 scenarios and 214 checks.
Each of nine new asteroid colonies completed 3,000 ticks at 0.997–0.998×
actual speed with 1× selected. The ten-cycle Linux run restored frame 33,
reached frame 30033 and wrote the cycle 11 autosave. Maximum relative mass
and energy errors were 2.76 × 10⁻¹⁵ and 7.96 × 10⁻¹⁵. Windows checks included
the prepared colony, autosave, Spaced Out! and menu reload scenarios. The submitted save also
passed cold migration on Linux and in the Windows player. Both players passed
all 13 storage and five Pacu checks. The oldest Linux adapter passed those
18 checks on build 744825. The compact UI was inspected at 640 × 480 and
2560 × 1440.

The 102 native solver test and diagnostic cases reproduced the previous
release's numerical outputs and exit statuses, including full M2, M3, M5 and
both Riemann runs. Only two logs differed, each solely in wall-clock timings.
The independent Riemann reader passed 3,201,611 saved-state checks on each run.
The historical point-velocity curved-remap diagnostic
reports its existing failure in both versions. Its nodal-conversion control
passes in both. The production game-kernel control also passed the address,
undefined-behavior and thread sanitizers.

The private step reports are under
`artifacts/acceptance/08faa66/runs/{patch-v1,release-v1,ui-repair-v1,settings-repair-v1}/summary.json`.
The numerical comparison, with only the benchmark and oracle wall times
removed, is in `artifacts/v1.12.1/mac/physics-verified.json`.

Both release ZIPs were CRC-checked. The combined ZIP and the private
Workshop upload folder contain the same 40 files. The previous release and
the original player save remain available.
