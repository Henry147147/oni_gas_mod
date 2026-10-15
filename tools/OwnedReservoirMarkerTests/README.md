# Reservoir marker and footprint checks

This focused host runs production inert-marker and copied-footprint validation
against a locally licensed ONI installation. It does not start Unity, install
Harmony, save/load a colony, mutate native objects or activate ownership.

```powershell
& C:/Users/henry/.dotnet/sdk10/dotnet.exe run --project tools/OwnedReservoirMarkerTests -c Release -p:OniGameDir=D:/SteamLibrary/steamapps/common/OxygenNotIncluded -- D:/SteamLibrary/steamapps/common/OxygenNotIncluded
```

`-p:MarkerContractOnly=true` builds only the linked production files as a
netstandard2.1 library, checking the game's target contract independently of
the .NET 10 host. Use an isolated `--artifacts-path` for this alternate target.

The marker exposes no assignment API or lifecycle adoption. Seven private
primitive fields are read strictly; complete empty defaults are Unassigned,
while partial, unknown-version or malformed data is Invalid. Tests use a private
reflection boundary to populate explicit fixture fields. They execute the
licensed native serializer, directory roundtrip and external-type lookup;
the test does not infer that component instantiation/loading in Unity works.
An additional local-template/mapping roundtrip preserves an unrelated global
load mapping. The actual game fixture should use that local approach, never
clear/reinitialize the game's serialization manager for a diagnostic.

Geometry-only pair validation allows default unassigned markers and reports
`ManifestValidated=false`. Manifest validation requires two exact assigned
records, native colony/owner/token/full allocation matches, the exact completed
Core allocation references, zero world inventories and coverage of every
nonempty allocation. Empty unbound rows remain intact. This is data validation,
not a native save permit or live eligibility lease.

Native capture accepts explicit instances and immutable geometry/optional full
handles only. It reads all 15 cells, definition/occupancy/building caches,
native building-layer ownership and both utility/connection/pipe cells. It
does not repair caches, move objects, call native item mutation, or derive world
fluid inventories. Component Enabled flags are diagnostics. A live caller must
still hold and revalidate the ownership lease before/after capture; copied
observations are historical facts and carry no lasting permission.

Checks include distinct anchors with overlapping footprints, row wrap and
integer bounds, every world/occupancy cell, stale building/rotated/utility
caches, native packets/identity/capacity/construction, immutable input copies,
full-handle/cross-colony mismatch and metadata-only unbound inventories. IL
checks verify licensed method/field wiring and absence of repair/fluid calls.
Actual prefab cloning, spawn, Grid occupancy and in-game serializer evidence
belong to the disposable Windows acceptance driver.
