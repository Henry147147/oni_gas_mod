# Private playtest 0.1.4, 2026-11-11

Version 0.1.4 gives oxygen, hydrogen and chlorine the masses of their
molecules (user decision 2026-11-11, on the open item of the
[0.1.3 review](playtest-0.1.3.md#open)). It keeps everything else in 0.1.3.
It is a private candidate; no public release or Workshop upload was made.

## Changes

- **Molecular masses.** The game's element table gives oxygen, polluted
  oxygen, hydrogen and chlorine the mass of one atom; the mod now uses the
  molecule's, twice that
  ([material catalogue](material-catalogue.md#molecular-masses-014-2026-11-11)).
  Normal base air (1.6-1.8 kg of oxygen in a tile) reads about 125 kPa; it
  read about 250. Carbon dioxide, natural gas, steam and every other gas
  read as before.
- **Layering.** Gases layer by molecular weight: natural gas, steam and sour
  gas now rise above oxygen, and chlorine sinks below carbon dioxide.
  Hydrogen stays on top and carbon dioxide below oxygen, as in ONI.
- **Breathing.** The oxygen minimums stay 15 % and 15 kPa. A tile at 20 °C
  now needs about 197 g of oxygen for 15 kPa (it needed 99 g). When switched
  on, the CO₂ limits default to 4 % and 5 kPa (2 % and 5 kPa in 0.1.3): the
  same carbon dioxide mass as before, since oxygen now counts half as many
  moles. Strict breathing uses 2 % and 2.5 kPa.
- **Gas pressure overlay** runs from vacuum to 250 kPa with normal base air
  mid-scale (500 kPa in 0.1.3).
- **Start dialog and How it plays** say base air is about 125 kPa and add the
  layering line.
- **A base opened to space stopped the simulation** (also with 0.1.3; found
  while rebuilding the Spaced Out! benchmark colony, whose second base vents
  to space). All nine runs of that build faulted before the fixes (one of
  them with the 0.1.3 package); six of six complete after them. Two causes,
  each with a control that reproduces the game's refusal without its fix:
  - When ONI deleted a space-exposed cell's gas, the mod drew the mixture's
    gases in turn and the Compensated source energy rule returned the
    draw's work to whatever the draws had left: a mist droplet took 333 J
    and read 1.3e9 K. A draw's work now returns only as far as the
    temperature its gas left at, a room warms by at most a quarter in one
    batch, and the rest stays with the gas that left
    ([game kernel](../native/MAC_GAME_KERNEL.md#source-energy)).
  - A rounding-sized energy correction held by the owner was released as
    heat into the nearest cell with mass, which beside a vented room is a
    trace: 1e-83 kg read 9e59 K. A correction now goes only into a cell
    whose energy it barely changes
    ([owner](colony-owner.md)).

  Two rounding hazards of the same family are closed as well (what an
  extraction leaves as a trace, or after a deep draw, takes its energy from
  the temperature it was read at).
- **Saves.** Colonies saved by 0.1.3 or earlier are refused at the menu with
  "Start a new colony with this version" and left unchanged (save material
  model 4): their gas would read half its saved pressure, and there is no
  migration.

The molar masses change no solver code: the solver receives different
values for seven elements, and a colony's checkpoint records its catalogue,
so 0.1.4 checkpoints differ from 0.1.3's in that identity. The space fixes
change arithmetic only where a returned draw would warm its room by more
than a quarter, where an extraction leaves a trace, or where a trace cell
would have taken a held correction; the checkpoint format and version 32
are unchanged.

## Prepared saves

Every prepared save holds material model 3 and no longer loads, so the ones
the checks use were made again with this version (private, in `local/saves`):

- `base-defaults-p12-v1` replaces `base-defaults-p11-v1` as the acceptance
  suite's frozen save: the same recipe
  ([harness](../tools/ONI_HEADLESS.md)), made from `cc56605`; game profile 12.
- `large-industrial-base-32-v4` replaces `-v3`: the same private builder
  (kept beside the save as `large-industrial-base-32-v4.builder.patch`),
  32 duplicants, 34,450 owned cells.
- `full-colony-spacedout-v3` replaces `-v2`: the committed
  `full-colony-spacedout` scenario, built and settled for two cycles with
  `tools/full_colony_benchmark.py`.

They are new colonies with the same construction, not the old states
converted, so their timings compare with 0.1.3's only as like for like
workloads.

## Verification

- Linux: `tools/build.py --oni-native-observer` passes (93/93 native tests,
  516 Python tests with 11 platform skips, every managed suite: 50
  presentation tests with the rule's control, 451 settings checks, 242
  distribution checks that also pin how model-3 saves are refused).
- Every `ma_mac_*` invocation against 0.1.3's outputs: 62 of 62 pass, 60
  identical, the production oracle differs in timing fields only and
  `mac_game_kernel` names its new controls on its source energy line.

In-game results are recorded in [the acceptance section below](#acceptance).

## Open

Carried from [0.1.3](playtest-0.1.3.md#open): the germs above pools check,
the mist front, batched transfer rounding, plants and critters reading the
main gas by mass, and the unseen overlays and slowdown sign.

New with this version:

- Phosphorus and sulphur gas keep the game's atomic masses (their vapours
  are mixtures of molecules that change with temperature).
- The oxygen minimum of 15 kPa now takes twice the oxygen mass. Halving it
  (7.5 kPa) would keep 0.1.3's requirement; it is a setting (**Advanced →
  Breathing thresholds**) and the default is the user's to change.
- Colonies from earlier versions cannot be continued; a converter was not
  written.

## Acceptance

Pending.
