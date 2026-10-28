# Compact texture regression controls

Run with the inspected local game declarations; no Unity scene, game singleton,
or native solver is started:

```sh
dotnet run --project tools/CompactTextureTests -c Release -p:OniGameDir="$ONI_GAME_DIR" -- "$ONI_GAME_DIR"
MA_COLONY_COMPACT_TEXTURES=0 dotnet run --project tools/CompactTextureTests -c Release --no-build -- "$ONI_GAME_DIR"
```

`tools/build.py --game-dir ...` runs both modes in isolated processes. The test
resolver uses that explicit argument or `ONI_GAME_DIR`; no licensed DLL, save,
capture or extracted game code is committed. All reading inputs are generated
in the test sources from synthetic gas/liquid catalogues and fixed random seeds.

The rich constructor and color loop remain the literal original implementation
at `fef29c2`, independent of the compact analyzer. Embedded source SHA fixtures
and deliberate validation/color mutations reject changes to that oracle. The
separate `ReadingEquivalenceTests` suite checks the original public-property and
rejection goldens; it is always run by `build.py`, including without game DLLs.
Portable golden values exclude only platform C-runtime color differences. This
suite compares unquantized color/scalar bits and exact exception type/messages
against the unchanged rich implementation in the same process.

Cases cover native ordering and equal-ID permutations, 76/79-species catalogues,
2..257 constituents, invalid inputs, cached-null rejection counts, model and
coordinate errors, opacity options, collisions, busy writers, stale/reused
leases, old models, sessions, nested worker frames and concurrent readers.
Viewport preflight rejects colliding/unsupported/large views before scalar work.
Optional range lookup/binding/camera failures fall back without hiding sampling
errors. Destination-byte checker negatives remain independent of renderer
validation probes, which intentionally use the rich route.

.NET warmed raw/cached allocation assertions use a verified runtime allocation
counter. Unity's game Mono counter is unsupported; a zero value there is not
allocation evidence. Private Mono and actual-game proofs are recorded separately
in the performance documentation. Optional `--benchmark` output is diagnostic,
not a correctness or performance acceptance threshold.
