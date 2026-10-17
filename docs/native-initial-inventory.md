# Native inventory admission

Direct backend integration needs an initial inventory as well as a timestep.
The additive initial-inventory ABI now lets a sealed development domain start
from a completed native `GameDataUpdate`, replacing its synthetic initial
mixture only in the explicit `native-import-domain` and
`native-liquid-import-domain` controls. General world/geometry import remains
unfinished.

The managed adapter waits for the room's native flow fence, reads every cell
from that same completed packet and resolves native element indices to stable
material identities. Each native scalar cell contributes exactly its actual
mass to its actual constituent. Absent constituents stay zero. The adapter
does not replace the source cells when activation succeeds. Subsequent owned
snapshots contain the custom physical publication, while the original native
tuples must remain unchanged under the existing exclusions.

The complete row-major inventory crosses `ma_oni_domain_start_from_cells`.
Each ABI-1 `MAOniDomainInitialCell` is 56 bytes: size/version, five binary64
masses in the existing nitrogen/oxygen/helium/water/carbon-dioxide order, and
temperature. Native binary32 masses/temperatures are promoted exactly. The
new entry point requires precisely width times height cells, within the
existing 256-cell limit. Unsupported materials/phases, empty cells, invalid
numbers or temperatures, and invalid EOS states reject before activation.
All allocations, EOS initialization and projection checks precede publication
of the new owner. A failure in a later managed verification stops that owner
and retains its exclusions until shutdown.

Initial momentum is explicitly zero. Internal energy is initialized at the
native temperature in the declared reference EOS; compressed water includes
that EOS's elastic energy. This is not a claim to recover native ONI momentum
or preserve energy across a change from ONI's caloric/material model. Production
migration still needs its own physical model and conversion accounts. Disease,
radiation, passive quantities, vacuum, free surfaces and mixed gas/liquid cells
are outside this admission path.

The report records the six source cells, native element indices, exact admitted
masses/temperatures and a SHA-256 over the little-endian 56-byte input rows.
Before the first physical frame, actual copied cells must match every admitted
constituent and temperature with zero resolved kinetic energy and momentum.
The portable validator reconstructs the hash and compares the source native
binary32 values. Normal worker conservation, unchanged native cells, rendering,
an actual physical change from the source, outside native-solid heat and save
guards remain required. The import controls use their actual admitted state;
synthetic-fixture pressure/mass constants do not describe these inventories.
Ordinary paired
saves and exact continuation use the existing physical checkpoint identity;
the initial source packet is not a replacement for that checkpoint.

```sh
python3 tools/oni_native_matrix.py \
  --runner-source /path/to/frozen/source --package /path/to/frozen/probe \
  --output artifacts/native-import-001 --scenes import water-import \
  --frames 120 --timeout 1800
```

The Windows runner exposes `-NativeDomain import` and
`-NativeDomain water-import`. Proton uses the same explicit scene names and
continues to report WindowsPlayer on a Linux host separately from native
Windows OS evidence.

Both adapters compile, all 186 Python checks pass, and the Linux Release native
suite passes all 13 CTests. The new native controls include late-cell atomic
rejection, unchanged constituent inventories/temperature/zero motion, physical
advancement and exact checkpoint continuation for gas, water and water with
gravity. Sanitizer, Windows-under-Wine and real Unity import runs are pending.
No playable-colony or whole-colony acceptance follows from the helper tests.
