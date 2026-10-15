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
