# Mixture inspection checks

This host compiles the adapter's inspection and hover sources against the
installed licensed ONI assemblies, then checks them against the game's actual
declarations and original IL. No game process or Unity engine call runs.

```sh
dotnet run --project tools/MixtureInspectionTests -c Release \
  -p:OniGameDir="$ONI_GAME_DIR" -- "$ONI_GAME_DIR"
```

On Windows, use `C:\Users\henry\.dotnet\sdk10\dotnet.exe` with
`D:\SteamLibrary\steamapps\common\OxygenNotIncluded`. Add `-p:NoWarn=MSB3277`
to hide the game's known reference-version warnings.
`python3 tools/build.py --game-dir <root>` runs the suite as well. Each group
prints `PASS` or `FAIL`, and the host exits 0 only if all pass. All 37 groups
pass against the Windows and Linux declarations.

The checks cover:

- **Hover.** The injection keeps every vanilla instruction and block, and the
  details panel goes in before the single vanilla `Commit`. The contents list
  shows every non-trace gas with no cap and no selection. It applies the trace
  cutoff exactly and leaves percentages unchanged. Hazardous-trace warnings and
  full details stay.
- **Contracts.** Unknown contracts reject, and all trace and contaminant
  readings survive formatting.
- **Containers.** Pipe, storage, bottle, equipped-suit and vent adapters use the
  installed APIs. Duplicants resolve equipped items by slot. Storage lists
  contained items, so a bottle's mixture can appear inside a container.
- **Failure and cleanup.** The UI cannot create fluid inventories. A failing
  provider shows `unavailable`, never a substituted native packet.
  Unregistered native packets show as single-material packets, with no
  invented pressure. Unregister paths exist, and teardown clears the hover
  observer.

The binding API is in
[inspection bindings](../../docs/mixture-presentation.md#inspection-bindings).
This host does not draw the UI. Visual acceptance comes from the four-case
tile-hover fixture: `tools/oni_headless.py --mode tile-hover`, or
`tools/run_colony_validation.ps1 -TileHover`, with an operator receipt per case;
see the
[tile-hover acceptance procedure](../../docs/mixture-presentation.md#tile-hover-acceptance-procedure).
That fixture uses native ONI physics and synthetic inventories, so it does not
certify fluid ownership.
