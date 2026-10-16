# Mixture inspection checks

Run against the local licensed ONI installation, with no game process required:

```powershell
& C:\Users\henry\.dotnet\sdk10\dotnet.exe run --project tools/MixtureInspectionTests -c Release `
  -p:OniGameDir=D:\SteamLibrary\steamapps\common\OxygenNotIncluded -p:NoWarn=MSB3277 `
  -- D:\SteamLibrary\steamapps\common\OxygenNotIncluded
```

These tests read the actual installed managed declarations and original IL.
They verify the terminal hover bypass preserves every vanilla instruction,
the full panel is appended before its single Commit, unknown contracts reject,
all trace/contaminant readings survive text formatting, and each native pipe,
storage, bottle, equipped suit and vent adapter uses the installed APIs. They
also check the UI cannot create fluid inventories and that unregister/error
paths are present. They do not execute Unity native engine calls or claim
visual runtime acceptance.

The 29 current Windows groups include an uncapped four-gas hover contents
list, the exact configurable trace cutoff, unchanged percentages after trace
omission, retained hazardous-trace warnings/full details, and clearing the
private hover observer on session teardown. The first-playable criterion
requires every non-trace gas directly on tile hover, without selection.

The separate Windows runner accepts `-TileHover` to start the private native
four-case fixture. It waits for three actual hover frames per case and then a
pixel-review receipt bound to the held case/cell/revision/publication and screen
dimensions. Each receipt confirms that all non-trace rows and hazardous-trace
warnings are visible, traces are omitted from contents, and no selection is
required. The fixed deadline is 180 seconds per case. This opt-in fixture uses
native ONI physics and synthetic display inventories; it does not certify
production fluid ownership. V52's final receipt missed that deadline, so its
overall result remains failed. See [the native evidence](../../docs/mixture-presentation.md#verification-and-limits).

After reviewing a held native screenshot, publish its receipt with
`tools/write_tile_hover_receipt.ps1 -ReviewedReport <saved-held-report.json>
-OutputDirectory <the-live-fixture-output> -ExpectedCase <0..3>
-AllNonTraceRowsVisible -TraceRowsOmitted -HazardWarningVisible -NoSelection`.
Those four switches are the operator's explicit pixel-review assertions.
The producer validates the held observation and closes its temporary writer
before renaming the complete receipt into place. Direct writes to the final
receipt caused V53's native sharing violation; that failed run remains retained.
The protocol-only checks run with `python -m unittest discover -s tools/tests
-p test_tile_hover_receipt.py -v` on Windows with PowerShell 7 available;
`MA_PWSH` can select its executable. They do not perform pixel review.

Windows V54 completes all four actual native hover cases with that producer;
root reviews every native tooltip and saved Unity image before continuation.
The full report remains a display-fixture pass with native ONI physics, and
does not establish Linux hover or first-playable colony acceptance. V52 and
V53 retain their failed outcomes.

In the disposable Unity visual fixture, register a real container owner with
`MixtureInspection.RegisterInventory(owner, id, provider, volume, label)` or
bind an already registered adapter with `RegisterInventoryOwner`. After a
Unity frame boundary, `GetContainerInspectionText(owner)` uses the exact
resolver used by hover and the selected-object panel. The shared runtime pins
inventory callbacks once per frame. A failing provider must show `unavailable`
and must not show a substituted native packet. Use
`UnregisterInventory(owner, id)` for symmetric provider/owner cleanup.

Selected duplicants also resolve equipped items through their actual equipment
slots. Reservoir/machine storage enumerates contained item owners, allowing a
bottle's own registered mixture to appear inside another container. Unregistered
native fluid packets are labelled as native single-material packets with mass
and temperature; their unknown container pressure is not invented.
