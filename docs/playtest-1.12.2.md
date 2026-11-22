# Mixed Atmospheres 1.12.2

This version targets the Windows playtest colony saved at frame 133995 on
November 21. At 2× the game must advance a 200 ms simulation tick in at most
100 ms of wall time. At 3× selected, 1.12.1 advanced 90.01 game seconds in
110.76 seconds across three consecutive unpaused windows on a Ryzen 9 5900X
running the Windows player under Proton.

Liquid transport can now divide a large set of vertical donor cells among
workers. Each donor still uses its own inventory and the same frozen face
sweeps. Donors that can deliver the full layered parcel skip the calculation
of the unused proportional parcel. The surface correction keeps its species
in sorted order and reuses the thermodynamic reading of a neighbouring cell
when its inventory and energy bits are unchanged. The pressure solver can
reuse a connectivity check while all active cells and open pressure faces
match. Each solve still uses the current conductances and checks the residual.

## Optional fluid update rate

Settings → Gameplay → Region simulation → Fluid update rate controls how often the colony moves
its gases and liquids. Every tick is the default for new and existing saves.
At **1 in 8 ticks**, an awake room advances 0.2 seconds of fluid simulation
per 1.6 game seconds. The other choices are 1 in 4 and 1 in 16. Fluid-carried
heat, fluid-to-fluid conduction, phase changes and removal of traces follow
the selected rate. Duplicants, machines, pipe transfers, solid contacts and
world germ activity keep their game clocks. A pump can empty the tile by its
intake before surrounding fluid flows back to refill it.

The choice takes effect in a running colony. With a reduced rate the ONI
owner writes wrapper 15, including the interval. The saved frame count
determines the next fluid update after reload. Switching back to Every tick
resumes fluid updates immediately and the next save uses the ordinary
wrapper. The embedded MAC solver checkpoint uses the same format.
Measurements use private copies of the submitted save and its input journal.

This build includes the 1.12.1.1 pipe-heat save correction. In a recorded
one-in-eight replay, a drop entering sub-kelvin gas could stay below the
liquid's minimum temperature however far the flow interval was subdivided.
When that prevents any progress, the affected room retries from its opening
state with its scheduled fluid conduction first. Heat comes from neighbouring
fluid and the complete interval still has one conduction step. Both attempts
share the pressure budget and the same mass, energy and temperature checks.

On a tick between fluid updates, a germ or terrain edit can invalidate the
previous inventory audit. The owner now counts the current cells and face
momenta once in fixed row blocks and reuses that result for the following
transfer. With fluids advancing every eighth tick, the 450-tick replay
of the submitted colony retained identical physical state, germs, machine
queues and mass totals. In the 19.5 MB saved owner, only the recorded peak
relative energy discrepancy and its checksum changed. The peak was
3.38348 × 10⁻¹⁴. In that run the median owner tick fell from 50.0 to
37.9 ms. These are native replay times. Game speed includes Unity and is
measured separately in the Windows player.

## Measured game speed on the November 21 colony

The Steam package (`0da7afd`) averaged **2.078 game seconds per real second** with the 3× game speed and full-detail fluids every eighth tick. It advanced 599.91 game seconds in 288.67 real seconds. Version 1.12.1 averaged 0.886× over the same cycle, giving a **2.34× speedup**. The 20 consecutive 30-game-second windows ranged from 1.808× to 2.252× and included the Cycle 46 autosave. The independent host clock measured 288.70 seconds. With the 2× game speed selected, the first 450 measured ticks averaged 1.918×.

The matched full-cycle runs used a Ryzen 9 5900X, the Windows player under Proton and ONI build 764025 at 1920 × 1080. Both retained the save's existing pressure-solve tolerance of 10⁻⁶. Every colony setting shared by the two versions had the same value. They covered every tick from frame 134146 through 137145 after 150 warm-up ticks. The shorter seven-option comparison below used candidate `bb3d6f2`. The final package changed the Pacu test fixture and retained the same native libraries.

Open **Mixed Atmospheres → Settings → Gameplay → Region simulation → Fluid update rate**
in the running colony and choose **1 in 8 ticks**. Under **Advanced → Solver**, full detail uses the standard
**Flow parts per step** value of **2** and enables **Sharpen liquid surfaces**.
The next save retains those choices. Each 600-second cycle advances fluid
movement by about 75 seconds in an active room. Duplicants and machines run
on ordinary game time.

Each speed in the table uses the same first 450 ticks after warm-up
(frames 134146–134595, roughly 90 game seconds). For those same frames,
1.12.1 averaged 0.813 game seconds per real second. The temperature and liquid
columns compare each option against Every tick, full detail, after a separate
90-game-second replay with identical recorded inputs. Each entry is the 95th
percentile of the absolute per-cell difference. Reduced detail uses one flow part per step
and disables liquid-surface sharpening.

| Fluid updates | Game seconds per real second | Speed relative to 1.12.1 | Temperature difference, 95th percentile | Liquid fill difference, 95th percentile |
| --- | ---: | ---: | ---: | ---: |
| Every tick, full detail | 0.849 | 1.05× | 0.00 K | 0.00 percentage points |
| 1 in 4, full detail | 1.697 | 2.09× | 6.02 K | 0.80 percentage points |
| 1 in 8, full detail | 2.075 | 2.55× | 8.69 K | 1.26 percentage points |
| 1 in 16, full detail | 2.326 | 2.86× | 11.39 K | 1.60 percentage points |
| 1 in 4, reduced detail | 1.729 | 2.13× | 6.00 K | 1.30 percentage points |
| 1 in 8, reduced detail | 2.088 | 2.57× | 8.83 K | 1.66 percentage points |
| 1 in 16, reduced detail | 2.323 | 2.86× | 11.30 K | 1.57 percentage points |

The 172 × 236-cell replay area includes the occupied base. Temperature
comparisons include cells with at least 1 g in both runs. Liquid comparisons
include cells whose liquid fill exceeded 0.1% in either run. At the chosen
setting, 353 of 17,993 matched cells had a different dominant material and one
further cell fell below 1 g. All seven replays completed with peak relative mass
and energy discrepancies below 9 × 10⁻¹³ and 4 × 10⁻¹⁴ respectively.

## Full-rate performance and solver checks

With fluid updates set to Every tick, the Windows native replay took 226.8 ms per tick on 1.12.2, down from 252.5 ms on 1.12.1. Linux took 158.4 ms, down from 166.9 ms. Each pair used the same 450 frames of recorded colony inputs, timed the last 300 and produced the same final fingerprint on its platform. These timings measure the native owner. The Windows player measurements above include the rest of the game and the autosave.

All 102 fixed solver tests and diagnostics reproduced the 1.12.1 numerical results and exit codes, including the full M2, M3 and M5 grids and both Riemann reviews. The curved-nodal diagnostic returned its existing nonzero exit on both versions. Timing fields differed in two benchmark outputs. The AddressSanitizer, UndefinedBehaviorSanitizer and ThreadSanitizer checks passed. The final metadata fix changed managed code only. All 12 native libraries shipped across the three supported builds have the same hashes as the libraries used for these checks.

The release tests caught a separate save-reader issue. A malformed record labelled as a reduced-rate save could report a frame before the full owner was checked. The reader now verifies the configuration size and ABI version before reading the frame. The original rejection case and corruption tests against a real reduced-rate checkpoint passed, along with all 228 colony activity checks and 249 save and message boundary scenarios.

## Pacu stranding test

A Linux run reported zero exposure four seconds after a test Pacu was moved
onto its dry ledge. A repeat with the same package recorded 3.92 seconds.
With the fish's position and habitat sampled, one of 24 moves put a swimmer
back into water. The ledge remained dry, but the navigator finished a swim
that had started before the test moved the fish. At the check, the fish was
submerged and could breathe. Stopping its outstanding path before placing
it on the ledge kept all 24 subsequent checks dry. The fixture now stops
that path before moving the fish and reports its actual cell at the
exposure check. The colony still uses the game's ordinary suffocation,
rescue and death timers.

Two additional Linux runs followed Pacu for the full four-cycle suffocation period at the selected fluid rate. Private scenario code placed one fish in a dry chamber and another in a pool, then read the game's aquatic suffocation timers. In the equipped colony the dry fish reached 2,400.16 seconds and died. The submerged fish had zero exposure and all three duplicants survived. In an initial unprovisioned colony, the whole-colony survival check stopped when a duplicant died. A fresh process loaded that colony's natural Cycle 4 autosave. The same dry fish resumed at 1,748.57 seconds of exposure and died at frame 12039 after reaching 2,400.13 seconds. The wet fish remained alive with zero exposure. Both fish kept their ordinary navigation and survival clocks throughout.

## Save and release checks

The packaged Windows player restored the submitted Cycle 49 file at frame 143777, saved the selected update rate and wrote the natural Cycle 50 autosave. A fresh player opened that autosave at frame 146782 and saved again at frame 146866. The 187 existing storage bins held 100,000 kg at 5×, then 200,000 kg after a cold load at 10×. The pump and receiving reservoir both transferred 500 kg over 50 game seconds with the selected fluid rate. Windows settings and the Linux status bar were inspected at 640 × 480.

All seven patch steps and nine targeted test groups passed. The release suite completed 27 of its 47 steps. Its ten-cycle test was stopped after five cycles to investigate the subsequent Cycle 55 crash. The Windows player had stopped during fluid movement with Every tick selected. See the [1.12.2.1 hotfix](playtest-1.12.2.1.md).
