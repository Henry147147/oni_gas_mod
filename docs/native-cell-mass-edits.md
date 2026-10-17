# Native additive cell mass messages

The sealed native owner now intercepts the game's additive `ModifyCell`
message, including the explicit-material `SimMessages.ModifyMass` path used
when `CO2Manager` deposits a breath particle. This connects an actual game
producer to the physical transfer queue. It is part of the direct backend
boundary; complete duplicant breathing and metabolism remain unimplemented.

The inspected Windows/Linux 744825 packet is 28 bytes. The adapter preflights
the complete batch before reserving any transfer. Owned requests require a
positive finite mass and temperature, a supported constituent in the current
phase, callback -1, no replacement, no vertical displacement and no disease.
Padding is ignored. Mixed ownership, malformed packets, unsupported edits or
more than 256 owned requests reject. Entirely native-owned batches retain their
original native dispatch. Canonical compatibility writes remain guarded by the
existing projection-write scope.

Each accepted parcel uses transfer ABI 2, with a completed source frame and the
binary32-origin flag. Admission reserves the whole batch atomically; the native
worker commits it at its next positive physical step. The copied receipt records
actual constituent mass and material energy. Callback -1 needs acknowledgment
but generates no game callback. Rejected owned edits fault the bounded owner
and retain its native exclusions until shutdown.

The `carbon-particle` control creates a real 0.03125 kg, 310 K particle through
`CO2Manager.SpawnBreath`. The game updates and retires that particle normally;
its real deposition message must produce exactly one physical receipt without
native scalar dispatch. The control checks every constituent, carried energy,
producer retirement, unchanged native compatibility cells, actual rendering and
outside native heat. Fixed bounds are 1e-10 kg mass error and 1e-6 J energy
residual. The ordinary paired save is taken after the particle has retired.

```sh
python3 tools/oni_native_matrix.py \
  --runner-source /path/to/frozen/source --package /path/to/frozen/probe \
  --output artifacts/native-carbon-particle-001 --scenes carbon-particle \
  --frames 120 --timeout 1800
```

Linux and Proton expose `native-carbon-particle-domain`; the Windows runner
uses `-NativeDomain carbon-particle`. At least 30 frames are required. The
portable reviewer requires the actual producer and once-only receipt evidence;
generic gas transfer evidence alone cannot pass this control.

Both game-declaration suites pass 111 snapshot/message groups, including late
invalid edits, ownership boundaries, padding, exact packet layout and capacity.
The Python runner/evidence suite passes 190 checks with four platform/optional
skips. The actual Linux source/save and fresh-process reload now both pass in
`carbon-particle-unity-matrix-v2/matrix.json`, using frozen `cfd67c8` and native
compatibility build v11. The real manager creates the particle at physical
frame 6, deposits it at frame 16 and retires it before its component is reused.
At the source save (frame 122 / 24.400000364 s), maximum constituent error is
2.665e-15 kg and carried-energy residual is -3.376e-9 J, within the original
bounds. Native scalar dispatch is avoided, the receipt is delivered once,
and grid/rendering, unchanged native tuples and outside native-solid heat pass.

The ordinary save verifies its complete header and 5,124,062-byte body. The
physical image hash is
`ab4f3f1bc4ea9d41a3b6361fdd76cf8948fe3d0ef81a03849958b6eba05ffb04`
and colony-file hash is
`12ef575a526228bb7f8ab41965a1cb279a0e9a6eae128cad38c0bf6d881a6ee2`.
The fresh-process load passes its paused frame-zero autosave, both native startup
batches and 120 exact physical continuation frames, reaching 48.400000721 s.
Its frame-zero autosave file hash is
`c1edfbd99d92d784d8f5f73977a532e3647fceab123d2fdaca6e8106911989e6`
and final physical image is
`21ec192d847bbb984d763dadf19af7f1e31d0f7f01cd62e7e2a63f3108fc7327`.
The original source file stays unchanged. Windows Unity particle validation
and live-particle persistence remain open.

The retained first Linux `carbon-particle-unity-matrix-v1` source run reaches
frame 121 with exactly one native-manager deposition, 2.22e-15 kg maximum
constituent error and -6.05e-9 J energy residual. It rejects before saving:
the game has already retired the original particle, then reused its pooled
component for a different breath. Checking list membership at save time confuses
those incarnations. The corrected witness latches removal in the native
`CO2Manager.Sim33ms` postfix, before subsequent producer updates can reuse the
component. Pool reuse remains reported separately; a copied physical receipt
and every conservation/retirement check are still required. The failed run is
retained. The reviewer also now follows the actual receipt layout: ABI size and
version belong to the transfer-status envelope, not individual receipt rows.

This path does not implement negative mass edits, replacement, bulk spatial
consumption, native callback-bearing edits, disease, live particle persistence
or production device reservations. A complete breathing provider must select
oxygen from the owned mixture and couple actual consumption to metabolic
products; depositing an independent breath particle does not establish that
conservation account.
