# Mixed Atmospheres 1.11.0

## Installing and moving saves

One mod installation supports ONI 744825, 763796 and 764025 on Linux x64 and
Windows x64. ONI selects the matching adapter from the package. Keep the same
mod ID when updating an existing installation. A colony saved on 744825 can
continue on either newer supported build. A colony saved on a newer game build
still needs that build or a later supported one.

Build 744825 removes rocket interiors directly. The newer builds queue the
removal. Each adapter uses its game's API and keeps the same barrier around
saving, retiring the interior and recovering its fluids. Each checks the game
build and the dependencies it uses before activating the native backend.

The maintainer procedure and the checks for preserving older builds are in
[game-update resilience](game-update-resilience.md#keeping-an-earlier-build-working).

## Solver changes

New scientific MAC worlds use monotone compression and a gas expansion
correction. Where neighbouring gas velocities are separating, the remap can
return part of the numerical mixing heat to directed motion. It applies equal
and opposite impulses to the cell's two normal faces and debits the work from
the same cell. Worlds with saturation phase change use the existing momentum
and stress laws for boiling and condensation. Existing scientific checkpoints carry their chosen numerical
methods. Colonies keep their saved game profile. Older colony profiles also
seed new regions with their original transport and compression laws. See [face momentum reconstruction](../native/MAC_MOMENTUM.md#gas-expansion-work).


## Legacy colony accounting

A cycle-40, eight-asteroid playtest colony exposed a rounding residue of
about 10⁻¹⁸ kg in its account for gold vapour. The physical inventory was
zero. Both 1.10.2 and the first 1.11.0 candidate reported a larger relative
error after loading because the old file retained only net phase change.
The reader now recovers the minimum historical phase turnover that the
saved accounts prove. A 300-frame replay kept every physical checkpoint
byte and held the reported mass error at its prior value of 8.782 × 10⁻¹³.
The existing save format and audit limits continue to apply.

## Pumps sharing a pipe

Playtest report on 1.10.2: two gas pumps on one pipe did not pump
steadily, "but the vent kept getting clogged". In a reproduction of that
layout the vent was not the cause. ONI runs a pump only while its outlet
pipe cell is empty. The mod keeps pipe contents exactly while ONI moves
each packet as a single-precision mass, so a few micrograms stayed behind
when a packet moved on. The cell read 0 g, was not empty, and ONI stopped
the pump for that pipe tick.

A packet that ONI moves whole now takes its whole contents with it. The
receiving cell may then hold up to 0.075 mg (gas) or 1.2 mg (liquid) more
than its capacity; it reads exactly full. All other pipe moves keep their
exact limits. See [throughput](mixed-pipes.md#throughput) for the rule and
for the cases in which a pump can still wait one tick.

The in-game scenario `vent-duty` runs the layout in the mod and in ONI's
own simulation. Over 50 pipe ticks ONI's vent releases 25.0 kg. The mod
released 13.0 kg on 1.10.2's pipe code and releases 25.0 kg now. With the
two pumps in series on one pipe ONI itself holds the first pump's packet in
its outlet, so this layout passes one pump's flow (0.5 kg/s) with or
without the mod. The user's colony itself was not examined in the game.

## Storage ledger check

The `storage-ledger` scenario compared what the owner lost to a mop, a
pitcher pump, a demolished storage or a released chunk with the change of
ONI's water items in the whole world. In acceptance batch 13 a carbon
dioxide scrubber of the preceding `mixed-pipe-colony` base keeps running
elsewhere on the map: it turns 1 kg of its stored water into polluted water
at a time and refills from its pipe one pipe tick later. Whenever exactly
one of those steps fell inside a measuring window, the world-wide sum was
off by 1 kg and the check failed although the mop was exact: about half of
the batch runs on the 1.11.0 candidates, none of six on 1.10.2, whose
scenario code is the same (which change moved the base's timing onto the
failing frames was not determined). The check now counts the items inside
its own arena (user decision, 2026-11-20). Its frames, inputs and
tolerance are unchanged.

## Verification

The compatibility candidate loaded a colony created on 744825 in all three
Linux players and all three Windows players under Proton. Each player saved
and reloaded the colony again in the same process. The Windows player under Proton also
retired a landed rocket interior and an in-flight one on each build. The adapter tests
checked both platform contracts for each build, including rejection of
altered contracts.

With the repaired scientific solver, both the reference and binary64 production kernels pass
all nine fixed shock-tube, collision and expansion profiles. The collision
density refinement ratio is 0.74723 (limit 0.85). The finest separating-gas
velocity error is 0.058333 (limit 0.06). The independent saved-state review passed 3,201,611 checks and
rejected all five corruptions for each kernel. The reference run took 96 seconds
against the 180-second limit. See the [strong-flow measurements](../native/MAC_STRONG_FLOW.md).

The hot-air hazard test reads ONI's active scalding state at its fixed
observation frame. The game shows that state as a status item and applies
damage periodically. A duplicant can therefore be scalding while still at
full health. The test also records the monitor's state, average exposure and
temperature threshold alongside the cell temperature and health reading.

### Staged package `ff95138`

The package staged for upload on 2026-11-20 is built from `ff95138`
(`/mnt/data/share/oni/publish/update-1.11.0`; nothing was uploaded by the
project). The user asked for it to be staged before the acceptance run
finished. That run was stopped at 19:40 UTC, part-way and without a
failure, when the test environment was restarted for a shorter test suite.
The acceptance of this revision is therefore incomplete.

Passed on `ff95138`:

- 94 of 94 native tests on Linux and 95 of 95 for Windows under Wine, the
  pipe hook tests on both, and the full offline suite against each of the
  three game builds.
- The game-free pipe tests (`tools/ColonyConduitTests`), which include the
  cases raised in three review passes of the pipe change.
- All 14 altered game contracts refused, on each build and platform.
- The eight-asteroid 1.10.2 playtest colony: loads, runs 300 frames, saves
  and reloads from the menu on the Linux player and on the Windows player
  under Proton (764025), with its mass error unchanged at 8.782 × 10⁻¹³.
- A 744825 colony loads and reloads from the menu on the 744825 Linux and
  Windows players and the 763796 Linux player (three of six players ran).
- Landed and in-flight rocket interiors retire on the 744825 and 763796
  Windows players (four of six runs; 764025 did not run).
- Acceptance batches 1 to 3 on 764025 and batch 1 on each of 744825 and
  763796, on Linux.
- The reference and production shock-tube runs of the Windows build under
  Wine (reference 150 s against the 180-second limit).

The solver evidence is that of `a2c9193`, which no native file separates
from `ff95138`: gates M2, M3 and M5, the Linux shock-tube runs (reference
96 s) and the comparison of every registered solver test with the first
1.11.0 candidate. On the pipe code before its final comment and document
corrections (`2cefeb9`), Linux batches 5, 6, 7, 9 to 13 and 15 passed on
764025, and `vent-duty` passed in the mod and in ONI's own simulation.

Not run on `ff95138`: acceptance batches 4 to 15, asteroid generation, the
one-cycle and ten-cycle runs and every Windows acceptance step on 764025;
everything after batch 1 on the two older builds (for which the user chose
a reduced set without asteroid generation and the ten-cycle run); the
744825 colony on the remaining three players; rocket retirement on 764025.
Windows was exercised under Proton and Wine only.
