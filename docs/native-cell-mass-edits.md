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
The Python runner/evidence suite passes 189 checks with four platform/optional
skips. Real Unity particle and save/load results remain pending.

This path does not implement negative mass edits, replacement, bulk spatial
consumption, native callback-bearing edits, disease, live particle persistence
or production device reservations. A complete breathing provider must select
oxygen from the owned mixture and couple actual consumption to metabolic
products; depositing an independent breath particle does not establish that
conservation account.
