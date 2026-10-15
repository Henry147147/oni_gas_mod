# Owned native-snapshot memory tests

Compile and run the original copy/overlay/publication code against the actual
locally licensed ONI declarations without launching Unity or initializing the
native simulator. Fixtures contain synthetic unmanaged records and explicitly
owned immutable fluid inventories.

```powershell
dotnet run --project tools/OwnedSnapshotTests -c Release `
  -p:OniGameDir=D:\SteamLibrary\steamapps\common\OxygenNotIncluded -- `
  D:\SteamLibrary\steamapps\common\OxygenNotIncluded
```

The test target is .NET 10, matching the repository's managed test runtime. Game
assemblies are referenced in place and are not copied into the output. Runtime
resolution reads them only from the supplied licensed installation.

The scenarios exercise all snapshot channels, actual event record sizes,
independent owned storage, retained publication lifetime, atomic failure,
custom-event merge, missing optional headless textures, and rejected malformed
metadata. The same executable checks the save envelope, staged fluid-message
router and shared presentation adapter. Presentation cases cover explicit
fixture ownership, one capture per Unity frame, pinned worker reads, stale
inventory rejection, density-based visual opacity independent of pressure,
trace hazards independent of tint, and transparent non-gas pixels.

This is memory-boundary and pure presentation-policy evidence. It does not prove that Unity's
readers are quiescent at a proposed publication boundary, native source
coverage, authoritative fluid ownership, actual shader output, or gameplay behavior.

The parent runtime may opt into `SnapshotCopyValidation.CaptureAndValidate`
before returning its original native pointer. Publishing an owned copy is a
separate action and requires retaining it until the next safe publication or
shutdown. The helper itself installs no patches.
