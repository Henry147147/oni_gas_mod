# Native physical pressure readers

ONI's `Grid.Pressure` accessor derives a pressure-like scalar from cell mass.
For the native-owned development domain it now reads the completed physical
mechanical pressure and converts pascals to the accessor's kPa scale. This
covers mixed gas and filled compressed water. Native-owned cells retain their
original accessor result. Invalid, overflowing or underflowing positive
projections reject instead of acquiring a pressure floor.

The adapter patches the accessor and rewrites all five inspected game callers,
covering the property-pressure texture, debug pressure/danger/error views and
developer rendering. Rewriting the actual call sites also covers the tiny
value-type accessor when a runtime might inline its old body. Both pinned game
assemblies contain the same complete five-method set; the declaration tests
independently scan their IL metadata and compare the full set with the plan.
Every planned method must still call the expected accessor before patching.
The wrapper preserves the by-reference value-type receiver and original native
fallback.

Texture workers use the same pinned immutable presentation frame as the other
mixture render jobs. The pressure source is the supplied physical reading;
neither density nor the dominant compatibility element reconstructs it.
This extends the existing mixture pressure overlay. Legacy devices whose
settings explicitly use kg/m³ retain those density units; the reader change
does not implement new mechanical forces or pressure damage.

The `pressure-read` and `water-pressure-read` controls invoke the actual
accessor for every owned cell, compare its binary32 kPa result with completed
mechanical pascals and distinguish it from the old scalar formula. An outside
native granite cell must retain its original result. The gas control also
requires actual owned reads from rewritten game callers. Filled liquid's
normal gas-opacity job need not read liquid pressure; its control requires
the actual liquid accessor and all five installed caller patches.

```sh
python3 tools/oni_native_matrix.py \
  --runner-source /path/to/frozen/source --package /path/to/frozen/probe \
  --output artifacts/native-pressure-reader-001 \
  --scenes pressure-read water-pressure-read --frames 120 --timeout 1800
```

Linux and Proton expose `native-pressure-read-domain` and
`native-liquid-pressure-read-domain`. The Windows runner uses
`-NativeDomain pressure-read` or `-NativeDomain water-pressure-read`.
Both adapters compile, all 121 snapshot/message declaration groups pass per
platform, and 194 Python runner/evidence checks pass with four optional/platform
skips. Actual Unity source/save and fresh-process reload results remain pending.

These bounded reader controls do not establish whole-colony ownership, general
materials, free surfaces, mechanical fluid/solid coupling or production-device
pressure semantics.
