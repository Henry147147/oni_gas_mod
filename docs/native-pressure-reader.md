# Native physical pressure readers

ONI's `Grid.Pressure` accessor derives a pressure-like scalar from cell mass.
For the native-owned development domain it now reads the completed physical
mechanical pressure and converts pascals to the accessor's kPa scale. This
covers mixed gas and filled compressed water. Cells outside the owned domain
retain their original accessor result. Invalid, overflowing or underflowing positive
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
skips. All four actual Linux cases now pass in
`pressure-reader-unity-matrix-v2/matrix.json`, using frozen `6d7422f` and native
compatibility build v11. At gas source frame 121, all six actual accessors
return about 100.499–100.501 kPa from physical pressure; scalar approximations
would return 117.323–118.411 kPa. The game worker performs 806 owned reads,
all five caller patches are installed, and the outside native result retains
exact binary32 bits `1204148736` (101300 kPa) without an owned-reader increment.
The filled-water source returns 220000 kPa from about 220 MPa mechanical
pressure, with zero gas pressure; its scalar approximation is 111430 kPa.
Both source controls complete ordinary header/body-verified saves at physical
frame 121 / 24.200000361 s. Both fresh-process loads pass paused frame-zero
autosaves, two native startup batches and 120 exact continued physical frames.
The original source files stay unchanged.

| Control | Source physical SHA-256 | Source colony-file SHA-256 | Continued physical SHA-256 |
| --- | --- | --- | --- |
| Gas pressure | `2da3a0344561a0042ade3ddf5c8239260b53b4bacf56b058c17f8ef361b7b06e` | `4691cfa657c36388f039b06ae906c095c1f55a1c26f66ef90ae638a9f6bf24d1` | `10c95fb39894b455343909a486092fa6909a4582600eca25f72ca681c98bb75e` |
| Water pressure | `8ad441db4453c5db10ffaa6555952861b11f9fcf7c3b84678b326258f65ec289` | `4374b1ed5d5a0de6888156e0ff1a73525498642c983342dce4b519eaba33c351` | `46f8994e67552ca0146adc2980320995eef7f2ee1c01cc9ff33482a98b267d67` |

Complete source body readbacks cover 5,123,063 gas bytes and 5,122,729 water
bytes. Frame-zero autosave file hashes are
`b95ce8354ca031f42c3141ffded778d712e1722b4342d7aba139ad66cc154f1c`
and `3930e6f3cb8ebffb8160e097513c5148f47af26f5473e7b5ab4be69b55480711`.
Gas loads also perform 822 actual rewritten worker reads. The source controls
explicitly verify the six accessors; Windows Unity coverage remains open.

These bounded reader controls do not establish whole-colony ownership, general
materials, free surfaces, mechanical fluid/solid coupling or production-device
pressure semantics.
The later [restore checks](native-reader-restore.md) explicitly exercise pressure
and breathing readers again after loading and same-process session reset.


The retained `pressure-reader-unity-matrix-v1/pressure-read-save` reaches
physical frame 122. All six actual accessor readings match 100.465–100.535 kPa
physical pressure instead of the 117.353–118.381 kPa scalar approximation;
825 rewritten game-worker reads and all five installed caller patches are
witnessed. It rejects before saving at the outside-native-cell comparison.
The original comparison mixes a returned binary32 scalar with an intermediate
Mono multiplication. The retry records both binary32 bit patterns and compares
those exact patterns, retaining the unchanged outside-read counter requirement.
No pressure or physical conservation tolerance is widened. The first failure remains retained; the subsequent exact-bit rerun above passes.

## Windows declaration preflight

After merging the reader and owned-display runner work through `2db8758`,
Windows passes nine nitrogen-registration, 121 snapshot/persistence/boundary,
and 31 inspection groups against the installed ONI assemblies. The adapter
build passes with no C# warnings or errors; its two existing MSB3277 reference
conflicts remain recorded. The updated WSL runner suites pass 72 headless,
13 matrix and 14 Proton tests, without skips. These checks include breathing
selection and pressure-accessor declarations and the inspected caller plan.

The merge preserves all 157 checked native C++/CMake inputs from the completed
physics capture. That capture's strong density gate remains failed. These are
managed declaration, build and runner results; Windows Unity coverage of the
incoming hooks remains open. The protected default DLL and installed SimDLL
are unchanged. Private evidence: `local/windows-quadratic-readers-merge-r1`;
terminal-review SHA256:
`c2026e661287bb92cd0560f2e3a7ccb138d2d9f2ee3aebeaacd932e45da2dc33`.

The subsequent [actual WindowsPlayer matrix](native-windows-feature-matrix.md)
passes both gas and compressed-water sources, their native outside controls,
complete saves, and fresh-process 120-frame exact reloads. The gas scene
observes 824 game call-site reads; the water scene exercises the six physical
getter samples without observing a game call-site read. This closes the bounded
WindowsPlayer pressure-reader gate, with the stated Proton/OS limits.
