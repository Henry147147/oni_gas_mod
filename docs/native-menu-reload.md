# Same-process native colony reload

The private load runner can exercise two saved-colony sessions in one Unity
process. After the first paused autosave and exact physical continuation, it
uses ONI's normal quit-to-main-menu entry, observes simulation shutdown and
the returned menu, then loads the same staged input again. The second session
must pass another paused autosave and reproduce the first session's complete
physical image at the same completed native frame.

```sh
python3 tools/oni_headless.py run --package /path/to/probe \
  --mode native-load-domain --load-save /path/to/paired-colony.sav \
  --resave-before-resume --reload-in-process --frames 120 --timeout 1800 \
  --output artifacts/same-process-reload-001
```

Windows uses `tools/run_colony_validation.ps1 -NativeDomain load -LoadSave ...
-ResaveBeforeResume -ReloadInProcess`, with its ordinary package/output options.
Both runners stage a separate input copy in the disposable run directory.

Native shutdown retires snapshots, callback bindings, paired tokens and held
restore events. The managed domain then clears its per-colony publication,
replay, save, actor and validation state. Process-wide reload evidence remains
separate. The persistent validation driver binds to the new main menu instead
of adding a second driver. The second paused autosave gets a distinct file, so
the first save and input remain available for inspection.

The first native/colony reports and physical image are retained separately.
Final `InProcessReload` evidence requires the same process ID, a later native
session ID, observed shutdown/menu boundaries, both initial autosaves, an
exact matching physical milestone and an unchanged input hash. The existing
complete saved-state replay, rendering, native-cell and conservation checks
also apply to the second leg. A first passing load alone cannot pass this mode.

Implementation and declaration checks are in place; actual same-process Unity
verification is pending. This remains a drained six-cell development domain.
Live actor/outbox persistence, the Load screen's file-selection UI, arbitrary
colony ownership and first-playable acceptance remain open.
