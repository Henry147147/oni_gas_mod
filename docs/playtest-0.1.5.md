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

Pending.
