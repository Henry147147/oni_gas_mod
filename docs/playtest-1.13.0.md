# Mixed Atmospheres 1.13.0

Version 1.13.0 adds the interface another mod uses to push air, and continues
the speed work on the colony the user played on 2026-11-22. Existing colonies
load unchanged: the save format, the checkpoint and every setting are those of
1.12.2.3.

## Fans: an interface for other mods

[Mixed Atmospheres: Fans](../../oni_fan_mod) is a separate mod with a Wall Fan
and a Swivel Fan. It moves no gas itself. It asks Mixed Atmospheres to push
the fluid on one tile face with a fan curve, and until this version that
request had nowhere to go: the fans stayed out of the build menu.

1.13.0 provides it as the public class
`MixedAtmospheres.Oni.Interop.FaceForces` ([interface, contract and
examples](face-forces.md)). A handle drives one face with

```
F = A · stallPressurePa · clamp(1 − u / freeSpeedMps, 0, 1)
```

so a fan pushes with its stall pressure against still air, less as the air
speeds up and not at all at its free speed. In the solver this is one more
implicit term of the pressure step
([kernel notes](../native/MAC_GAME_KERNEL.md#face-forces-1130)), which is why a
fan between two sealed rooms ends exactly at its stall pressure with no flow,
and why a strong fan on a near-vacuum cannot fling a milligram of gas across
the map.

What the interface guarantees:

- **Without a fan nothing changes.** With no handle the solver takes its old
  path. All solver test and diagnostic outputs equal those of 1.12.2.3, and a
  replay of the November 21 colony ends on the same owner fingerprint
  ([checks](#checks)).
- **The accounts close.** A force's impulse is an external load and its work
  an external energy input. `TakeWork` returns the joules the energy ledger
  booked, so the fan mod can turn the rest of the fan's electrical power into
  heat in the fan.
- **Nothing is saved.** Handles are not in the colony's save and do not change
  its bytes. A loaded colony has none until the fan mod sets them again.
- **Handles end with the colony.** Unloading the colony or a fluid fault
  forgets every handle.
- **A running fan keeps its room simulated**, and wakes one the activity
  controls had paused.

The colony owner's input journal records the driven handles, so a recorded
session with fans replays bit for bit.

### In the game, with the fan mod

The `face-forces` scenario runs in a real colony on game build 764025. It
drives a face through the public class, then builds the fan mod's own
buildings with the harness's `--extra-mod ../oni_fan_mod/dist/MixedAtmospheresFans`:

| Check | Result |
| --- | --- |
| A 120 Pa, 6 m/s handle between two sealed rooms | 119.98 Pa, 0.855 J of work in all and 1.1 µJ in the last tick |
| Refusals, undrivable faces, closing | 8 of 8 invalid calls throw; the rooms equalise to 0.00 Pa after `Close` |
| The fan mod finds the interface | `Connected to Mixed Atmospheres (fan interface version 1)` |
| Swivel Fan, built and powered | runs, holds 29.85 Pa of its 30 Pa (Medium), warms to 300.22 K |
| Removing the Swivel Fan | 0.00 Pa: the handle is released |
| Wall Fan | **cannot be built with the fan mod as it is** (below) |
| Ledgers with a fan running | mass 3 × 10⁻¹⁵, energy 5 × 10⁻¹⁷ |

**The fan mod's Wall Fan throws when it spawns.** This is in the fan mod, not
in the interface: the Wall Fan is a tile (`isKAnimTile`) with
`PermittedRotations.R360`, and the game's `Rotatable.OrientVisualizer`
dereferences a `KBatchedAnimController`, which a tile does not have. Placing
one would throw a `NullReferenceException` in `BuildingDef.Build`. With the
Wall Fan made unrotatable and aimed from its side screen like the Swivel Fan
(a four-line change; the patch is kept beside this version's evidence as
`artifacts/v1.13.0/fan-mod-wall-fan-fix.patch`, and `../oni_fan_mod` itself
was not changed), the same scenario passes in full: the Wall Fan runs and
holds 59.98 Pa of its 60 Pa. The scenario reports a building that cannot be
built as a failed check of that mod.

## Liquids show as liquids

**User decision, 2026-11-23.** A cell that holds any liquid is now a liquid
tile to the game ([the shown phase](colony-owner.md#the-shown-phase)). The
game's grid holds one element a cell while a Mixed Atmospheres cell can hold
air and water together, and until now the grid showed the water only when it
filled half the cell. A puddle under air, a film on a floor and the top cell
of a pool looked like air: the hover said `Liquid volume 3.18 %` over a tile
with nothing to see, and the mop and a liquid pump's intake found nothing.

The grid now shows the liquid from 0.001 % of the cell's volume, about 10 g of
water. That is the share from which the hover reports liquid at all; less is a
trace, named nowhere, as a trace gas is. What a duplicant breathes does not
follow the shown element: it breathes the cell's gas, so a film underfoot or a
droplet at head height takes no air away. Freezing changed in one respect
only: the game freezes a shown liquid by turning its whole cell into a solid
tile, and it may still do that only where the liquid fills at least half the
cell or no gas is beside it. A film in a cold room freezes into debris, as it
did while it was hidden, and never walls off its tile.

**A wet floor does not take a room's air away.** ONI's own machines read the
tile the grid shows, and the user's recording has films of about 0.1 % (a
kilogram of water a tile) inside the base, in the kiln room and at an airlock.
Left alone, a plant in such a film would have found the wrong atmosphere, a gas
vent and an electrolyzer would have judged the film's mass instead of the
room's pressure, and a gas pump would have found no gas. So the game's readers
of the air (plants, gas vents and pumps, over-pressure tests, element emitters,
the space heater, the steam turbine, food storage, a building's exhaust heat,
duplicants' and critters' gas monitors) read the tile's gas wherever gas takes
up more than half of it, exactly the tiles that showed gas before
([the list and the rule](colony-owner.md#the-shown-phase)). What does follow
the visible liquid is what follows a puddle in ONI: the mop and the pitcher
pump find it, a duplicant walking through it gets wet feet, and a building
standing in it exchanges heat with water.

Existing colonies need nothing: the rule is in the projection, not in the
save, and applies from the first frame after loading.

In the game (`shown-liquid`, build 764025, 14 of 14 checks): 30 kg of water
put into one floor tile of a sealed oxygen room spreads into a film 0.38 % of
a tile deep; all 8 wet tiles show Water with the film's mass, none without
liquid does, the mop's test finds all 8, and all 8 still read as oxygen for
breathing. A one-tile plant standing in the film reads its atmosphere as
oxygen and safe; a gas vent standing in 3.76 kg of film over 1.77 kg of gas
stays open under its 2 kg limit; gas let out into a wet tile joins its air and
water let out into a dry one joins the film; mass and energy ledgers stay
closed (10⁻¹⁶).

## Speed on the November 22 colony

The user played 1.12.2.3 for 2.7 hours on 2026-11-22 (owner frames 164656 to
210819, Cycles 56 to 71) with fluids on Every tick and sent the save, the log
and a recording. What the session shows:

- The 2× button was selected throughout; 3× was never used. Over the 252
  unpaused windows at 2× the game ran at **1.46 game seconds per real second**
  (median 1.51, lowest 0.86); 45 % of the windows were below 1.5×.
- The owner tick averaged 111 ms (87 to 207 ms by window), 93 ms of it fluid
  flow. Each tick also waits about 24 ms for the game's own native frame
  (`update_data` 20 ms), so 2× needs an owner tick near 75 ms and 3× one near
  43 ms.
- The slowest stretches (166 to 207 ms) are mass digging into caves that hold
  liquid, and the last ten minutes, where the transfer and space stages grew.
- The picture stands still for about two seconds at each autosave.
- Neither log holds an error or exception of the mod.

`local/perf/nov22-session` is a private replay capture of the final save
(owner frame 210820, 600 journaled frames; `tools/colony_speed.py capture`).
It reproduces the session's last, slowest state without the game: 169 ms a
tick on the test host at 12 workers, against 144 ms for the November 21
capture.

### Where the tick goes

Half the tick is one region and the rest is the owner's own thread:

| Part of the tick (November 22 capture, 12 workers) | ms |
| --- | ---: |
| The regions' flow, bounded by the main base region (12,374 cells, 85 ms on 4 workers of its own) | 91 |
| Owner thread: writing the stepped cells back, sparse lists, audits | 22 |
| Owner thread: trace sink, space and open-sky sinks, transfers | 28 |
| Owner thread: heat edits, germs, activity, conversions | 20 |
| Projection and ledger | 6 |

The main region's time is its pressure solves: each needs 5.3 decades of
residual reduction and takes about 20 multigrid-preconditioned iterations at
0.3 ms a cycle. A red-black Gauss-Seidel smoother was tried in place of the
polynomial one and took 32 % more iterations, so the limit is the coarse
correction, not the smoother; that experiment is not in this version.

### What 1.13.0 changes

Every change below computes exactly what 1.12.2.3 computed: each replay ends
on its 1.12.2.3 owner fingerprint. Owner tick, mean of 300 measured frames at
12 workers, the two builds run in turn twice on the otherwise idle test host:

| Replay capture | 1.12.2.3 | 1.13.0 | Change | Fingerprint |
| --- | ---: | ---: | ---: | --- |
| November 22 session | 168.0, 169.4 ms | 153.8, 152.1 ms | −9 % | `ea4534603137f6be` |
| November 21 late window | 144.0, 144.0 ms | 135.3, 135.3 ms | −6 % | `c658996d69343921` |
| November 17 playtest (5 measured frames) | 274.6, 274.7 ms | 265.6, 243.7 ms | −3 to −11 % | `7cb982dda520682a` |
| Industrial colony, 32 cycles | 38.1, 37.9 ms | 37.2, 36.3 ms | −3 % | `a63fee38ed863346` |

That takes the November 22 colony from about 1.25× to about 1.35× game speed
by the replay estimate; it is not yet the 2× the user played at, and far from
3×. The kept hierarchies cost memory: a colony's resident growth over 120
frames rose from 62 to 114 MB in the largest generated case (peak 591 to
606 MB).

- **Multigrid hierarchies are kept.** A region's set of active pressure cells
  alternates: a liquid surface cell joins the system when a solve stiffens it
  and leaves with the next interval. One cached hierarchy was rebuilt at each
  change, 694 times in 1,476 solves of the November 21 main region. The solver
  now keeps the last four and rebuilt 246 times.
- **The trace sink rewrites only cells that hold a trace.** It used to read
  and rewrite every stepped cell with the values it already held whenever one
  trace existed anywhere, which on the November 22 colony was every tick:
  15.3 → 5.6 ms.
- **The stepped cells are written back a chunk a worker**, each chunk built
  directly instead of copied and then overwritten: 8.3 → 5.0 ms.
- **The whole-world recount after a trace sink runs on the workers.** A world
  the trace sink rewrote keeps no audit totals, so the next sink transfer
  summed every cell on the owner thread.

### What is left

The [speed study of 2026-11-23](speed-study-2026-11-23.md) took this
further: it measures each item below, the accuracy settings, and what was
tried. The list as it stood when 1.13.0 was cut, ranked by what the
November 22 replay still spends:

1. **The main base region's pressure solves** (about 85 ms on 4 workers of its
   own): 20 multigrid-preconditioned iterations a solve. The coarse correction
   is the weak part; a better one, or fewer decades where the residual is
   already below what moves a cell, is the largest lever.
2. **The owner thread's serial stages** (about 60 ms together): heat edits,
   germs, projection, the space and open-sky sinks, writing stepped cells
   back and freeing the previous world.
3. **A second region** (around cell 242,121) whose solves are refused and
   repeated at cell (378,126); the cause was not found before this version
   closed.
4. **Scheduling.** Probing a region's serial cost every 64 intervals instead
   of 16, reusing a tick whose thread share changed by one, and keeping
   worker pools by size were built and measured: 143.5 against 157.8 ms on
   the November 21 capture in one pair of runs, but 165.7 ms for the longer
   probe interval alone, on a host that was busy at the time. The result is
   not conclusive and the change is not in 1.13.0; the diff is kept as
   `artifacts/v1.13.0/scheduling-experiment.patch`.
5. **The two-second stop at each autosave** (116 to 126 frozen frames in the
   recording at three autosaves) is the save itself and was not touched.

## Checks

Solver arithmetic and checkpoint bytes are those of 1.12.2.3; the save format
and settings are unchanged. What the game is shown changes by design (the
shown phase).

| Check | Result |
| --- | --- |
| Native tests, Linux (`ctest`, with the game backend) | 95 of 95 |
| Native tests, Windows cross build under Wine | 96 of 96 |
| `python3 tools/build.py` (native, managed and Python suites) | passed; 593 Python tests, 93 managed/native integration tests |
| Every `ma_mac_*` test and diagnostic output, 1.12.2.3 against 1.13.0 | 62 of 62 equal (timing fields masked); `mac_game_face_forces` is new |
| Owner replays (eight private captures) | all end on their 1.12.2.3 fingerprints |
| `tools/colony_speed.py --regression` | the six replay cases match; the six generated cases' view hashes change with the shown phase and were replaced ([why](performance-acceptance.md#patch-regression)) |
| Game contract, build 764025 | 2,985 entries, 0 differences; 25 game methods added for the readers of the air |
| In game: `face-forces` | interface, Swivel Fan and (with the fan mod patched) Wall Fan, above |
| In game: `shown-liquid` | 14 of 14 |
| In game: 37 gameplay scenarios in seven batches with the new shown phase (terrain, heat, phase, freeze-melt, sublimation, airflow, pumps, pipes, vents, geyser, germs, pacu, plant, breathing, storage ledger and others), run before the readers of the air were added | 37 of 37 |

The release and patch acceptance tiers, the three-build load matrix and the
Windows game runs are recorded with the package
([staged package](#staged-package)).

## Staged package

Built from `7b521cb` on 2026-11-23 and staged on the private share as
`releases/1.13.0` (the Workshop upload folder `MixedAtmospheres`, the manual
ZIP and Steam's single-file form `mod_publish_data_file.zip`) and
`publish/update-1.13.0` (the same with the private Windows installer). Each
folder holds this note, the interface documentation, the change note, the
fan-mod patch and `RELEASE-STATUS.txt` with the test status; `HOLD.txt` stays
until the release checks have passed. Nothing is uploaded: the Workshop serves
1.12.2.3.

| Archive | SHA-256 |
| --- | --- |
| `MixedAtmospheres-1.13.0.zip` | `dde404474debd2c2796bea04fa7de0302a01dd4f20566f623cec07cde9168ac9` |
| `MixedAtmospheres-1.13.0-private-windows.zip` | `4eb38401c9fce762ebf2bbf39666298aa120835e1b2be19d637d59c00ab1ea7f` |
| `mod_publish_data_file.zip` | `1b2c3e4b1494eca5f98ba1da3fab11ff0c63fdba25ab2122bea54d8bfb680337` |

Checks on this revision's packages:

| Check | Result |
| --- | --- |
| Linux compatibility build (Ubuntu 22.04 image) and its tests | passed |
| Windows and Linux acceptance builds | passed |
| Release folder and both ZIPs, file by file; contracts for builds 744825, 763796 and 764025 on both platforms | verified, 0 contract differences |
| Linux game, `shown-liquid` and `face-forces` with the patched fan mod | 14 of 14 and 23 of 23 (Swivel Fan 29.85 of 30 Pa, Wall Fan 59.98 of 60 Pa) |
| Windows player under Proton, `shown-liquid` and `face-forces` (no fan mod) | 14 of 14 and 13 of 13 |
| Workshop upload dry run | the upload file equals the staged `mod_publish_data_file.zip`; nothing sent |
| Load matrix on the three game builds, Linux and Windows player | 6 of 6 |
| Patch acceptance tier, with the batches for plants, vents, electrolysis and the storage ledger | 11 of 11 |
| Release acceptance tier | 47 of 47 steps. 43 passed in the first run; two Windows steps that drive the settings screen with the mouse (`windows-settings-source`: a tab colour check with the pointer resting on a tab; `windows-spacedout-ui`: a click sequence that began while the Spaced Out! intro dialog was open) and the two steps that depend on them passed when run again (`settings-repair`, `ui-repair-2`), as for 1.12.1 |

The hold was lifted on 2026-11-23 14:50 UTC. The upload itself waits for the
user's go-ahead for this version.
