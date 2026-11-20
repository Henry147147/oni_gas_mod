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
