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

The actual Linux `in-process-native-reload-v1` cohort passes with the frozen
`466bde0` runner/managed package and compatibility native build v7. One Unity
process loads twice, native sessions advance from 1 to 2, and both sessions
match all 120 physical frames. At frame 120 / 48.200000718 s they retain the
same physical SHA-256
`08c0e874f1af2940b42076735958c46f77e65a38a832843ac632bef96d3fb382`.
Both initial autosaves and full header/body readbacks pass. The staged input
remains
`32c4e18a57522e6013ff9f2c4e23ce3236131716655ccf2b0ca59a881c86510f`;
the second autosave is
`eb9073928b4923f438d6b568b983e0e630500fc248fca8e63ddca4617936da5a`.
Both-platform builds and all 101 declaration/snapshot scenarios pass, and
the 161 Python checks retain four explicit skips.

The actual Windows `windows-native-current-reload-r1` cohort also passes with
the clean `7f29783` package and current conservative liquid conduction. One
Unity process loads the same compressed-water colony twice, observes native
shutdown and the returned main menu, and advances native session 1 to 2. Both
sessions pass their paused deferred autosave, full header/body readback and
all 120 exact continuation frames. At frame 120 / 48.000000715 s, both retain
physical SHA-256
`d6f2a232c3c990951ceb78397ef148c501b2146992b2131697b349c871c2b294`.
The source and staged input remain
`1fd556e7ba9f85ba0929ed69bae0d593a0ac736b63af75cd409d46b20a6e8fef`.
Both autosaves remain separate. The runner exits successfully, removes its
temporary mod and restores all 13 original profile files byte-for-byte.
Private terminal review SHA-256:
`fad61e5fc7a1850c348c4ec6198b1c2fd80c104d9fbbf13112c61fcb326543e1`.

The saved colony retains its GUID and three duplicants. ONI still reports a
missing KPrefabID reference (246809) during resaving; general managed-reference
integrity is not proved. Actual liquid inspection is retained separately from
liquid fill rendering, which remains unaccepted.

This remains a drained six-cell development domain.
Live actor/outbox persistence, the Load screen's file-selection UI, arbitrary
colony ownership and first-playable acceptance remain open.
