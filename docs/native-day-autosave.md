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
Actual natural day-rollover execution remains pending. This control retains
the actor-free domain limit; live actors, overwrite/crash durability and
whole-colony ownership remain open.
