# Natural day-rollover autosaves

The paired save boundary is installed on the ordinary `SaveLoader.Save`
entry, so a scheduled game autosave uses the same paused native/physical
capture, complete header/body readback and pause-count cleanup as a menu save.
The new long control exercises the game's own day-rollover scheduling.

```sh
python3 tools/oni_headless.py run --package /path/to/frozen/linux-probe \
  --mode native-domain --verify-natural-autosave --frames 3300 \
  --private-display --timeout 7200 --output artifacts/natural-autosave-001
```

The control sets this disposable colony's autosave interval to one cycle and
lets normal simulation reach the next day. It never changes game time or
invokes the save directly. The actual `GameClock.DoAutoSave` request must be
observed once, at cycle one or later, and the completed ordinary save must be
marked automatic. Its native cycle/time, source physical frame/image,
complete file readback and restored pause depth are recorded. Simulation must
continue after the save. The control independently advances the saved physical
image through every subsequent worker interval and requires exact equality
with the final live image.

`--verify-natural-autosave` requires the mixed-air `native-domain`, 3100–5000
frames and no `--save-colony`. The bound keeps this control to one natural
day-rollover request. The Proton runner exposes the same flag; its runtime
clock issue remains a separate gate. A successful Linux source can be loaded
normally with `native-load-domain --load-save ... --resave-before-resume`.

Both adapters compile, 122 actual-declaration groups pass per platform, and
203 Python runner/evidence checks pass with four optional/platform skips.
The actual Linux `natural-day-autosave-workflow-v1` passes both the natural
source run and fresh-process continuation, using frozen `e325fd8` and native
compatibility build v11. ONI schedules exactly one save at cycle index 1
(the displayed filename says Cycle 2). Its paired capture occurs at native
game time 602.0255126953125 s and physical frame 2730 / 546.000008136034 s.
The physical owner started after colony startup; its clock does not claim the
earlier native intervals.

The scheduled save verifies its complete header and 5,147,076-byte body and
restores pause depth 0. Its source physical hash is
`bd4dd91454fa63884738c38eacd097e3624f5a6a43a6044992289e11030213b1`
and colony-file hash is
`6a46489554b21d2397b359d14181f17d8f6104a28483ba6183cd67dc35268e98`.
Normal simulation continues for 572 further physical frames. Independent replay
of all 572 intervals exactly matches the final live image at frame 3302 /
660.400009840727 s, with physical hash
`35eef92979c6e84b562f7076cf47bb1db5a979868385dddf49aa39f27f2e8cc0`.
Maximum relative mass and energy drift are 4.24e-15 and 1.65e-13.

A fresh Linux process restores the exact saved physical image, passes the
paused frame-zero autosave with complete 5,148,020-byte body readback, delivers
both native startup batches and matches all 120 resumed physical frames.
Pause depth remains 1. Its autosave file hash is
`25ce98aa6c6badc48496a27dec05ac3757d599fb8390f922a5d16ff4f65ee848`
and final physical hash is
`1fbd0c1fc001c8fc7842926ee79b132ec4e49d77d067bdfa6dd32ac33cd0ad6a`.
The original scheduled save remains unchanged. Both private displays retire
normally, and neither run logs a mod failure.

This control retains the actor-free domain limit. Windows scheduled-save
execution, live actors, overwrite/crash durability and whole-colony ownership
remain open.
