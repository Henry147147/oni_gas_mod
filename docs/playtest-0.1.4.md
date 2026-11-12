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

Code revision **`60030e4`** (the tree of `152cb75`, which adds notes only;
clean manifest). Windows ZIP:
`/mnt/data/share/oni/playtest/MixedAtmospheres-0.1.4-152cb75/MixedAtmospheres-private-windows-0.1.4-152cb75.zip`,
SHA256 `0498802e95c67da934d3ac0c0230bf87b4891ef54f83f2027fedba5c8e72970f`.
Close ONI, extract and run `install_windows.ps1`; start a new colony (earlier colonies are refused at the menu).

The quick tier of `tools/acceptance_suite.py` ran in full on `152cb75`
(46 steps, tag `quick-v1`): 40 passed, 3 failed, 3 were skipped. The tier
did not pass; the table gives the reason for each step that did not.

| Step | Result |
| --- | --- |
| Linux build; Windows (MinGW) build | pass |
| Linux batches 1-12, 14, 15 | pass |
| Nine starting asteroids, 3,000 frames each | pass |
| One cycle on the frozen prepared save | pass |
| Proton batches 2, 3, 4, 6, 7, 10, 11, 12 | pass |
| Proton contact, settings source and load, Spaced Out! source, cold load, menu reload | pass |
| Linux batch 13 | fail: `storage-ledger`, one germ check (see below) |
| Proton base source | fail: the player exited, exit code 5, about a second after the owner started (see below); passed on the rerun |
| Proton ordinary Spaced Out! new game, save, quit, reload | fail: the suite clicked the start dialog's old button position (see below) |
| Proton prepared cycle, autosave cold | skipped: dependent of base source |
| Proton ordinary Spaced Out! load | skipped: dependent of the Spaced Out! new game step |

`linux-b13`: scenario `storage-ledger` fails one check of its group, "the
emptied gas brings its germs once" (owner +397,950 germs, still stored 0,
stored 80,000). The other three scenarios pass and the owner is healthy
(823 frames, mass error 2.9e-16). It is the same open item as in 0.1.3
(393,797 germs on 0.1.2, the delivered package then).

`windows-base-source`: the player exited about a second after the owner
started, and the log ends at that line, with no error. It passed on the
rerun below. One-off; the cause is not established.

`windows-spacedout-ui`: the suite clicked the start dialog's OK button at
its old position (the dialog is one line taller since 0.1.4), so no embark
or save happened; the owner had started. The suite was corrected in 0.1.5
(`1f5c091`). Not a mod defect.

The three skipped steps were dependents of those two failures
(`windows-prepared-cycle` and `windows-autosave-cold` of base source,
`windows-spacedout-ui-load` of the Spaced Out! new game step).

Rerun `quick-v1-rerun1` (same revision and package; Windows build, base
source, prepared cycle, autosave cold): base source and prepared cycle
passed. Autosave cold failed only the check `clean_exact_runner`, because the
working tree was edited while the run was in progress; its other checks
passed (owner healthy, 2,880 frames, mass error 4.5e-16, no failed
pressure attempts, no mod failure lines).

The fix for the clicked step and a full passing run of those steps came with
0.1.5's run: see [0.1.5](playtest-0.1.5.md#acceptance).

The Proton steps are the WindowsPlayer under Proton, not Windows itself.

Before the tier, on the working tree of `cc56605` (masses only): the 15
Linux batches passed as with 0.1.3 (`storage-ledger` failing its one known
check with 397,951 germs), and the game log showed the new masses (a
40-cell starting room of 63.7 kg oxygen: 1,991 mol, about 122 kPa).

The vented-base build (`full-colony-spacedout`), which no acceptance step
runs: 5 of 5 runs stopped before the fixes (four with 0.1.4's masses, one
with the 0.1.3 package), 10 of 16 while the fixes were partial, 6 of 6
complete on `60030e4`, and the colony then ran two
more cycles (7,123 owner frames, ledgers 4e-15) to become
`full-colony-spacedout-v3`.

Speed in game (Linux harness, GPU display, one run each; one cycle of 3,000
frames at speed 3 loaded from the prepared saves; 0.1.3 on the saves it
could load, 0.1.4 on the ones made again, so the colonies are alike but not
the same states):

| Save | 0.1.3 `d9dd945` | 0.1.4 `152cb75` |
| --- | ---: | ---: |
| Industrial base, 32 duplicants (`-v3` / `-v4`): game seconds per real second | 2.189 | 2.255 |
| ... warmed windows: owner / flow ms a tick | 61.5 / 49.6 | 58.3 / 46.4 |
| Spaced Out! full colony (`-v2` / `-v3`): game seconds per real second | 1.021 | 0.974 |
| ... warmed windows: owner / flow ms a tick | 170.6 / 150.0 | 182.0 / 162.5 |

The Spaced Out! colony is just below the 1× beta bar on its new save. Its
flow stage costs 10 % more and the colony's whole history has 13 % more
flow intervals; the new save also holds 15 % more gas (17.0 t against
14.8 t). The cause is not established: the two saves are different states,
and neither version loads the other's. One candidate is the halved
pressure itself: the vented second base sits at 1-2 kPa instead of 2-4,
where water boils near 10 °C.

Not established here: Windows itself, the user's own colonies, and a human
look at the overlays, the layering line and the refusal dialog.
