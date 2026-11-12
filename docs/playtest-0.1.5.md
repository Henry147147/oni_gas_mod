# Private playtest 0.1.5, 2026-11-12

Version 0.1.5 answers the user's recording of 0.1.4
(`Oxygen Not Included 2026.11.11 - 19.10.28.02.mp4`, 40 minutes on
Windows): "the fluid solver is messed up". It keeps everything in
[0.1.4](playtest-0.1.4.md). It is a private candidate; no public release or
Workshop upload was made.

## What the recording shows

Read as one frame every ten seconds by four reviewers, with the flagged
moments checked at one to three seconds:

- **A pool that does not settle (about 22:58 to 24:03).** While duplicants
  dug a shaft under the 35-tile pool below the base, its surface kept a
  half-tile step that travelled from side to side and was still there when
  the camera left, 65 seconds later; the step is also in a paused frame.
- **Steam and mist.** Rooms beside the pool read 2.0-2.4 % steam at about
  22 °C and one air tile held about 1.4 kg of liquid water.
- Nothing else looked wrong: pools at rest were flat, gas read 135-142 kPa
  steadily, game speed held 1.00× with the solver at 3-8×, and there was no
  fault notice.

## Changes

- **Liquid drag.** Liquid had no damping, so a disturbed pool sloshed for
  minutes (this was so before 0.1.4 too). Every face now feels a drag in
  proportion to the liquid it carries, with a one-second time constant
  ([game kernel](../native/MAC_GAME_KERNEL.md#liquid-drag-profile-12-2026-11-12)).
  A 35-tile pool with a half-tile step is flat in ten to fifteen seconds
  and does not swing to the other side; small pools settle faster and very
  wide ones creep level. Liquid falls at up to 9.8 m/s.
- **Acceptance suite.** The clicked new-game step clicks the start dialog's
  OK button where it now is (the dialog gained a line in 0.1.4).

Profile 12 arithmetic changes wherever liquid moves; the checkpoint format
and version 32 are unchanged, and 0.1.4 colonies load.

## Open

- **Going back to ONI's own liquid simulation** (raised by the user with the
  recording) was not done. A study of the code found no per-phase split of
  ownership today; the hybrid would be a large rework close to a new
  ownership architecture at the gas-liquid boundary, would give up mixed
  liquids, pressure-dependent boiling, liquid stiffness and pressure
  readings and mixed liquid pipes, and reverses the decisions of
  2026-10-18/19/20. The drag is offered first; the choice is the user's.
- **The steam.** Its source is not established. Sloshing makes none and a
  pool at rest makes none; room steam condenses within about twenty
  seconds, into mist that stays in the air tile. One candidate is digging
  beside water: an opened cell starts empty, and water boils into it.
- The 1 s time constant is a judgement: 2 s levels wide pools faster with
  one visible overshoot, 0.5 s is slower everywhere. It is a constant, not
  yet a setting.
- Everything open in [0.1.4](playtest-0.1.4.md#open), and the Spaced Out!
  full colony just under 1× on its new save.

## Acceptance

Code revision **`e3666a9`**. Windows ZIP:
`/mnt/data/share/oni/playtest/MixedAtmospheres-0.1.5-e3666a9/MixedAtmospheres-private-windows-0.1.5-e3666a9.zip`,
SHA256 `5c964508b917980d1d2d3ceeeed1d332a0401f657d61706da32d69e095f90ddb`.
Close ONI, extract and run `install_windows.ps1`; start a new colony
(earlier colonies are refused at the menu).

The quick tier of `tools/acceptance_suite.py` ran in full on `e3666a9`
(46 steps, tag `quick-v1`): 44 passed, 2 failed, none skipped. The tier did
not pass.

| Step | Result |
| --- | --- |
| Linux build; Windows (MinGW) build | pass |
| Linux batches 1-12, 14, 15 | pass |
| Nine starting asteroids, 3,000 frames each | pass |
| One cycle on the frozen prepared save | pass |
| Proton batches 2, 3, 4, 6, 7, 10, 11, 12 | pass |
| Proton contact, base source, prepared cycle | pass |
| Proton settings source and load, Spaced Out! source, cold load, menu reload | pass |
| Proton ordinary Spaced Out! new game, save, quit, reload (the step 0.1.4 failed; the suite's click corrected in `1f5c091`) | pass |
| Linux batch 13 | fail: `storage-ledger`, two germ checks (see below) |
| Proton autosave cold | fail: the owner faulted on the first game step (see below) |

`linux-b13`: scenario `storage-ledger` now fails two germ checks; the other
three scenarios pass and the owner is healthy (823 frames, mass error
8.6e-16). The checks:

- "the vented gas brings its germs to the owner once": +100,357 germs
  (stored 90,000);
- "the emptied gas brings its germs once": owner +435,032 germs, still
  stored 0 (stored 80,000).

On 0.1.4 only the second check failed (owner +397,950 germs); the first
passed. It is the open germ item of [0.1.4](playtest-0.1.4.md#acceptance),
here with a second check failing.

`windows-autosave-cold`: on the first game step after loading the cycle-2
autosave written by `windows-prepared-cycle` (owner restored at frame 2760,
30,952 owned cells), the game's power tick threw a NullReferenceException
inside `EnergyGenerator.EnergySim200ms`; the mod contained it as an owner
fault, "Colony owner faulted; fluids are frozen: Adapter: game sim step:
Object reference not set to an instance of an object". The owner frame was
2761 (90.8 ms, flow stage 58.1 ms); the ledgers and pressure solves were
clean to that frame (mass error 9.3e-16, no failed pressure attempts).
The same autosave was then loaded twelve more times (eleven under Proton,
one on Linux; ten of them with a build that keeps a fault's first stack
trace) without a fault. The trace of the one failure starts at the mod's
own clean-up hook on that method, which is where the hook's host restarts
it, so the origin below it is not known
([0.1.6](playtest-0.1.6.md#open)).

The Proton steps are the WindowsPlayer under Proton, not Windows itself.

The timed full-colony loads were not repeated on this revision; the next
candidate's record carries them.

Not established here: Windows itself, and the user's own look at pools in
play.
