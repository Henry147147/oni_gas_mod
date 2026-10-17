# Swept first-moment material reconstruction

The opt-in `MacMaterialReconstruction::swept_first_moment` selector repairs a
reproducible moving-cell coordinate error. It is persisted as value 2. The
existing donor-cell and index-space linear selectors retain values 0 and 1,
and the default remains 1. This change does not select a new game backend.
The strong-flow and exact-vacuum gates remain open.

## Reproduced defect

The pressure step changes material volumes before returning material to the
fixed grid. The previous linear reconstruction uses centered differences in
cell-index coordinates and a slab coordinate normalized by material volume.
Those coordinates do not generally describe the moved material centroids.

A constant-density nitrogen control supplies normal face displacements
`[0, 1, 4, 5, 7, 4, 0] / 64` and an affine passive concentration
`2 + x/8`. The selected material cell has volume `65/64`, center `2.5703125`,
and outgoing volume `5/64`. The swept slab center is `3.0390625`.
Its exact passive amount is `0.1859283447265625`.

The existing selector produces `0.18598116361177885`: an error of
`5.2818885216354694e-5` against an operand allowance of
`1.0570303980396754e-14`. The limiter is exactly one and the source is unchanged.
Both axes reproduce the same numerical records on Windows/MSVC and WSL/GCC15.
The difference agrees with an independent exact-fraction prediction of the
index/volume metric error. Windows and Linux raw files retain their respective
CRLF and LF endings; their raw hashes differ.

This isolates a reconstruction defect. It does not establish the cause of the
previous collision refinement failure.

## Coordinates and reconstruction

Let `V0` be the fixed cell volume. Coordinates below use grid spacings as the
length unit. Each face's signed normal displacement is `q/V0`. Its transverse
displacement is the mean of the adjacent cells' mean transverse sweeps divided
by `V0`. The time-centered swept face position is its original position plus
half that displacement vector. Both face owners use that same position.

For each cell, require the original volume closure
`V = V0 + qR - qL + qT - qB`. Its moved centroid offset is

```
D = sum_faces(sign * q * (original_relative_face_position + displacement/2)) / V
```

This is a discrete first-moment model, without an inferred interface polygon.
For one-dimensional deformation it gives the actual interval center. Uniform
two-dimensional translation gives `D = (qx, qy)/V0`, recovering the original
transverse half-step coordinate. Using a full transverse displacement in both
the centroid and the slab would count that term twice; this implementation
uses the shared time-centered face position above.

Inverse-distance least squares uses the moved neighbor-center offsets.
The stencil includes the four cardinal neighbors and diagonals only when both
two-step paths exist through fluid cells. It does not cross walls. Neighbor
inventories are expressed at the donor volume, as in the existing method.

One limiter scales every species, actual stored U, the positive
thermal-plus-elastic proxy, and every passive quantity. Normal-face extents,
outgoing slab positions, and the retained centroid constrain the limiter.
These extents are bounds for this reconstruction, not a general free-surface
geometry. Prototype, normalized parcel and retained EOS checks and common
slope backtracking remain in use. Positive quantities are never clipped or
discarded. Unresolved coordinates/stencils flatten the complete donor and
are reported; unrepresentable positive transfers still reject atomically.

## Completed controls

The R3 cohort passes 160 standalone groups on each platform: nine groups
exercise the new selector and 151 are selected regressions. The new tests
include eight affine cases across both axes, signs and length scales; four
oblique translation/shear cases; zero-flux/wall identity; invalid geometry and
selector rejection; and positive passive underflow rejection. Both selected
affine passive fields agree exactly with their binary64 expected amounts in
the saved records, also meeting the original 128-epsilon diagnostic allowance.

The owning interval dispatches the new reconstruction, and all 24 gas/liquid,
compression and heat setting combinations retain complete checkpoints and
exact same-platform continuation. Newly written selector-2 images are read
on the opposite platform without changing any bytes. Six retained selector-1
images, covering enabled, disabled and quadratic heat/compression settings,
also pass opposite-platform reads. Checkpoint format stays 2; existing selector
values keep their meanings. An older reader that rejects unknown selector 2
must not reinterpret it as an older method.

The original five reconstruction controls were also compiled with only the
reconstruction function selection changed. Their original contact, cold-EOS,
trace, rollback and scalar-wave bounds stay intact. N16/32/64 scalar errors are
`0.003407536314`, `0.001021121138` and `0.0002517850251`; both ratios satisfy
the original 0.45 bound.

The new saved-data reviewer performs 27,015 exact-fraction checks per platform
and rejects four altered records. The uniformly translated contact/cold/wave
review performs 13,341 checks and rejects three alterations. The other selected
saved-data reviews also pass. Thirteen legacy native output streams per
platform remain byte-for-byte equal to their preceding accepted captures.
The heat-owner comparison uses its later liquid-conduction integration capture.

Linux ASan/UBSan passes 19 selected groups with fatal findings, leak detection
and empty stderr. Compile-command inspection verifies instrumentation of the
new reconstruction/tests, owner, checkpoint, parcel/transport and EOS code.
These sanitizer outputs are byte-identical to the reviewed Linux release
outputs. This is functional evidence, without a performance measurement.

The public standalone target is `ma_mac_swept_reconstruction_tests`; it is
excluded from the default build. Its raw stdout is reviewed by
`native/tests/review_mac_swept_reconstruction.py`. The separate
`review_mac_swept_controls.py` reviews the original five controls after explicit
function substitution, requiring a uniformly translated periodic mesh.
The checkpoint target is now explicitly available as `ma_mac_checkpoint_tests`.

Private evidence is retained under `local/mac-swept-first-moment-r3`.
Source-freeze SHA256 is
`1bf15dd816e7294a0d81692aca3f498232835c652266d27014640b57966e3796`;
terminal-review SHA256 is
`f73830fdac507c16e28a5041acdd23482bb3b12c580acc034a18c54e1749fd90`.
The preceding diagnostic review is
`c1a6117f2e0e9fc67d2711d3231159006a5db3eaf0f7bca65d610384e9145dd8`.

Earlier setup failures remain retained: R1 requested a nested Visual Studio
target at the harness root; R2 lacked a declared checkpoint target in the
dependency list. R3 repairs those build declarations. One saved heat review
omitted its `--matched-heat` flag; the corrected command passes on the same
native bytes. No native result was replaced to correct these harness errors.
The baseline diagnostic's first reviewer compared platform line endings
literally; its corrected reviewer preserves both raw files and compares their
records after newline normalization.

No strong-flow retry, exact-vacuum case, new Unity launch, production colony,
phase/interface acceptance, performance acceptance or independent-agent review
is claimed. The default game DLL and installed ONI SimDLL remain unchanged.

## Merged backend build

After merging the native-inventory admission work, the checkpoint test target
is defined once in the root project. Backend test builds retain it in their
default ALL target. Both new standalone targets also receive sanitizer compile
and link flags through the normal `MA_SANITIZERS` option.

A fresh Windows backend build passes the native domain/import, observer ABI,
detour, new reconstruction and checkpoint controls. Managed checks pass nine
nitrogen groups, 106 snapshot/message scenarios and 31 inspection groups.
The adapter compiles with the same two pre-existing MSB3277 reference warnings
for System.IO.Compression and System.Net.Http. Linux's sanitizer option alone,
without injected global compiler/linker flags, builds and passes the new
reconstruction and checkpoint targets; their bytes match the earlier reviewed
sanitizer output. The three affected Linux runner suites pass 85 unit tests.

The first runner-unit invocation used a source archive without its own Git
metadata, so its checkout-identity test failed. The unchanged, hash-matched
tests pass from the actual checkout. The unused first Python plan placed its
stages outside the stage table and launched no unit tests; its corrected plans
and all first outcomes remain retained. The initial closing reviewer also
assumed zero adapter warnings; comparison with the preceding Windows capture
confirms the same two reference warnings above. No native capture was repeated
for these review corrections.

Evidence is `local/windows-swept-import-merge-r1`, source-freeze SHA256
`081948a9e9dade2a7abcfbe81b6bae8d47b0f31251522a21415e945dd7080ee5`,
terminal-review SHA256
`15b28fcf0175668bbaf4a25eb3f84a1155c71e0928a076f7cf967da570978080`.
This checks the combined build and helpers; actual Unity inventory-import and
full-colony acceptance remain separate requirements.
