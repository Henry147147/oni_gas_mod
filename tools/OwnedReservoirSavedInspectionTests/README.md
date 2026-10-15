# Original saved-directory and KSAV section inspection

This standalone host links two original pure managed source files. It has no game, Unity or Core dependency and does not install hooks, execute saved types/callbacks, invoke native readers or write a save. The .NET Standard 2.1 build checks the same product code's compilation contract.

The parser owns copied immutable bytes, reads bounded little-endian values, parses saved descriptor trees without resolving their type names, frames every top-level root/component before known-layout decoding, and descends through recognized Storage packet records. Marker, KPrefabID, Tag and inherited Storage fields are read in their saved order. Unknown component bytes and the body tail remain unchanged. Structural failure throws a typed exception and returns no report. Marker/identity conflicts are explicit semantic diagnostics; no winner is selected.

Every successful report still says `WholeFilePreflightProven=false`, `GeneralDescendantCoverageProven=false` and `OwnedRestorePermitted=false`. The caller supplies the KSAV start; this tool does not derive that anchor from the body prefix or search for magic/component strings. Arbitrary custom-detail payloads may contain further roots and remain outside recognized Storage traversal. It does not validate native Sim contents, reconcile a checkpoint owner manifest, assign a marker, quarantine a prefab or permit an owned load/save.

Synthetic fixtures are independently authored byte streams, not calls to native KSerialization or the parser's own writer (the parser has no writer). Checks cover saved-order inherited fields, nested positive native packets, descriptor trees, zero/null/assigned markers, exact Int64 values, raw float bits, opaque bytes containing misleading magic/type strings, duplicate later identities, malformed/truncated framing, strict UTF-8, all resource budgets and immutable copy/collection behavior. Ordinary positive native packets must remain present; they are never dropped or rewritten to fit custom ownership.

```
dotnet build tools/OwnedReservoirSavedInspectionTests -c Release --artifacts-path local/ksav-inspector-build
dotnet local/ksav-inspector-build/bin/OwnedReservoirSavedInspectionTests/release/OwnedReservoirSavedInspectionTests.dll
dotnet build tools/OwnedReservoirSavedInspectionTests -c Release -p:InspectionTargetFramework=netstandard2.1 --artifacts-path local/ksav-inspector-contract-build
```

An optional exact v33 ordinary-save calibration accepts only the pinned 1,143,798-byte file with SHA256 `ac6146317872e088b41e20dd96ee0d4c5f84f75897f86845d9f8485df2f6941e`. Its independent actual native directory-return witness is offset 30035. The test wrapper checks that ordinary fixture's fixed v1 header and parses the directory from the header-derived start to this observed exact end. It reports saved known-member descriptors. This is actual saved-directory byte evidence, not KSAV body/offset or successful owned restore evidence. The fixture remains local licensed game data and is not tracked.

```
dotnet local/ksav-inspector-build/bin/OwnedReservoirSavedInspectionTests/release/OwnedReservoirSavedInspectionTests.dll --ordinary-fixture local/colony-runs/load-rejection-windows-v33/disposable-colony/save_files/ordinary-unmarked.sav --directory-end 30035
```

A separate optional capture mode consumes the pinned closed Windows-v38 observer report, its complete directory/body byte images and the exact ordinary save. It verifies the input hashes, all three separate native scalar-witness stages, the observed reader identity/content/normal-return declarations, exact bounded independent zlib inflation, directory range 398..29965 and KSAV range 9472651..10456036. It requires complete top-level/recognized Storage framing, unique expected parent/packet IDs (247004/247005), a default unassigned marker and byte-for-byte preservation of all framed component payloads, the complete native packet, prefix and tail. The mode executes the same unchanged two product parsers; offsets come from the captured actual native entry/normal return, with no byte-pattern search. It does **not** decode PrimaryElement's mass, temperature or disease: those native scalar witnesses are explicitly separate evidence.

```
dotnet PATH/OwnedReservoirSavedInspectionTests.dll --actual-capture local/colony-runs/ordinary-ksav-observer-windows-v38/mod/game-contract-local/colony-validation/ordinary-ksav-observer --ordinary-save local/colony-runs/ordinary-ksav-observer-windows-v38/disposable-colony/save_files/ordinary-unmarked.sav
```

This capture is an ordinary native save/load with inert unassigned markers, not an owned restore or live load preflight. The observer files and save stay local licensed game data and are excluded from the tracked tool.

Default limits cap the image at 1 GiB, directory at 64 MiB, 65,536 templates/members/groups, 262,144 total recognized roots, 4,096 components per root/1,048,576 total, 64 Storage levels, 32 descriptor levels/262,144 nodes, 1,048,576 total Tag elements, 16 KiB type/component/group names, 4 KiB member names and 1 MiB value strings. Caller limits may be stricter and are constructor validated. Counts must also fit enclosing byte ranges before allocating records. These are bounded compatibility limits, not a guarantee for every possible ONI/mod save.

The saved layout facts were researched against licensed ONI Windows build 744825 (Assembly-CSharp SHA256 `a58e04d0ffdf89b86fb28b71ad900625b3b539db30d67f8c6269f73a9f5ae599`, firstpass `72d21f51cda136443bfa1a2f6eadb08167a4c508c3d03f558b8cdb60160f249c`). No licensed source or fixtures are included here. Pure Windows/Linux host checks establish only managed parser behavior; they are not Linux Unity, native restore or rendering tests.
