# Owned snapshot and adapter-helper tests

This .NET 10 host compiles the adapter's pure snapshot, persistence, message and
presentation helpers against the declarations of a locally licensed ONI install.
It never launches Unity or initializes the native simulator. Fixtures use
synthetic unmanaged records and explicitly owned immutable fluid inventories.
Game assemblies are referenced in place, never copied, and load only from the
supplied install.

```sh
dotnet run --project tools/OwnedSnapshotTests -c Release \
  -p:OniGameDir="$ONI_GAME_DIR" -- "$ONI_GAME_DIR" [--frame-coordinator-benchmark]
```

The build fails without `ONI_GAME_DIR` or `-p:OniGameDir`.
`python3 tools/build.py --game-dir <root>` also runs this suite. The host prints
one `PASS` per scenario and exits nonzero on any failure.
`--frame-coordinator-benchmark` adds a timing of the synthetic four-cell
protocol: registration, staging, owned copy, paired publication and
acknowledgement. It times no native, actor or physics work.

The scenarios cover:

- **Snapshots.** All 47 `GameDataUpdate` channels, 29 of them events, are
  deep-copied at their actual record sizes. Covers owned storage, publication
  lifetime, atomic failure, custom-event merge, missing headless textures and
  malformed metadata.
- **Persistence and messages.** The save envelope, fluid-message router, frame
  coordinator, batch commands and frame-input journal. Also actor registration
  and checkpoint images, cell-mass edits, transfer plans, transient particles
  and binary32 wire values. Configured payloads (versions 4 and 5) check the
  `MADCFG01` framing, header/wrapper agreement and native-world mask faults;
  four legacy payload SHA-256 values pin versions 1–3 to the unmodified codec.
  Solver checkpoint version 4 passes through legacy, actor and configured
  wrappers for full native diffusion-property admission; unknown versions reject.
- **Domain helpers.** Readings, configured-domain layout, orbital references,
  breathing and pressure reads, and validation limits.
- **Presentation.** The adapter runtime, GPU readback plans, tile-hover receipts
  and telemetry. Ownership is explicit, there is one capture per Unity frame,
  worker reads are pinned and stale inventories reject. Opacity follows
  density, not pressure, hazards do not depend on tint, and non-gas pixels are
  transparent.
- **Game entry points.** The menu-reload and day-autosave entry points have the
  inspected signatures.

These are memory-boundary and pure-policy checks. They do not prove that
Unity's readers are quiescent at publication, native source coverage, fluid
ownership, shader output or gameplay. The runtime can opt into
`SnapshotCopyValidation.CaptureAndValidate` before returning the original native
pointer. Publishing an owned copy is a separate step, and the copy must be kept
until the next safe publication or shutdown. The helper installs no patches.
