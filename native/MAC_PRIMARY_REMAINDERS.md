# Primary rounding remainders

Persistent primary U and volume remainders belong to Codex (user decision,
2026-10-19). U now carries a rounding remainder in both kernels and the physical
checkpoint. Volume remainders remain open. No physical selector changes.

## Stored U and writes

A cell's conserved internal energy is `mixture.energy + energy_remainder_j`.
The high part remains binary64; the normalized low part uses binary64 normally
and an authoritative `MacScaled` value only when its projection rounds. The
production arrays carry a scalar `U_remainder`; flagged inputs use the reference
fallback. EOS readings use the nearest high part. Energy accounts include both
parts; entropy accounts include the low-part caloric correction.

Reference writes use `add_mac_cell_internal_energy` or
`store_mac_cell_internal_energy` with `MacPrimaryRoundingCounts`. Production
writes use `add_internal_energy` and each computational block's `EnergyRounding`.
Pressure (midpoint and both moment methods), reconstruction/transport, gravity,
compression, shock heat, conduction, viscosity, diffusion and external heat or
material transfers retain the low part. Rollback and checkpoint continuation
carry it. Parallel heat relaxation accumulates counts per block before the fixed
ordered reduction. Legacy non-MAC material closures keep their former arithmetic
and reject carried U unless explicitly requested.

`add_mac_primary` uses TwoSum for ordinary writes. The range counterpart rounds
exact additive expansions, resolving ties from all retained terms. Both retain a
second part, count inexact high writes and audit any discarded third part against
half a low-part quantum. The production interval still uses the original
128-epsilon operand audit. Small positive heat can now reach the low part;
`unresolved` counts changes unresolved across both parts.

Reconstructed void prototypes scale their carried U with their material share.
The reference transport path subtracts and adds one shared energy transfer at
both endpoints, including when a retained mass fraction rounds to one.
Conduction gathers one antisymmetric face transfer before storing each endpoint.

## Checkpoints and owner framing

Nonzero U corrections select physical version 10. It stores complete physical
settings, the occupied-volume sidecar, and one canonical correction per cell,
including flagged significands/exponents. Unknown selectors, malformed projections,
negative scalar zero, overlapping parts, missing coverage and redundant version-10
images reject. Versions 2–9 restore zero corrections; retired selector 1 still
rejects. Zero-correction writers keep the earlier version and bytes.

The managed owner framing admits version 10 without changing outer payload bytes.
Native owner restore still rejects unsupported void-phase settings and volumes.
Seven zero-remainder controls for versions 2/3/5/6/7/8/9 remain byte-identical.
The default reference wave intentionally changes from the historical
`4d250703f98fd1b00af3a7da505aaac1e7a7aa5405f24644e9b299893e8cf763` to
`8be4350458bb702b838cb01b2320d51b81d7f40ee1676ac1023bb9a87ea4c87e`:
carried U changes subsequent reference arithmetic. Version-10 images and moment,
coupled and huge-range continuations match byte-for-byte on Linux and Wine.
An old reader rejects version 10 explicitly.

## Controls and current evidence

The arithmetic components retain their 264 normal additions and 1,022 range
sums. Their exact-fraction saved readers check composed ties and discarded parts.
`ma_mac_primary_state_tests` adds 186 signed small-heat records across gas,
liquid and mixed cells in both kernels, checkpoint continuation with one/twelve
workers, rejected-batch atomicity, huge exponents, malformed states, void parcels
and occupied-volume checkpoint preservation. Saved captures encode a nonzero U
correction as `high~mantissa~exponent`; updated reviewers use the complete value
and retain their original numerical bounds.

The unchanged 123-frame public production water-heat reproducer passes with
`+3.814697265625e-6 J` drift against `0.0059458472553467583 J`, zero rejected
attempts and 4,956 conductive iterations. The reference control also passes.
Release and Ubuntu 22.04 each pass 69 CTests with the ONI backend; the Release
build treats warnings as errors. Debug ASan/UBSan passes all 69 tests plus the
updated capture producers and saved-kernel reader. All 17 selected ThreadSanitizer
runs, 34 selected Wine suites and 20 capture-producer Wine reruns pass. Both Linux
libraries meet glibc 2.34. Both licensed-game adapter builds, 198 snapshot scenarios
and 300 Python tests (four skips) pass.

All 75 previous MAC executable exit codes are unchanged; 29 stdout captures are
byte-identical. The U changes intentionally affect the other reference receipts.
The updated saved kernel reader passes 108,977 checks and seven corruptions; the
U-state reader passes 3,184 checks and twelve corruptions. The standard saved-reader
sweep passes 45/47: the strong-flow and legacy vacuum entropy-allowance failures
also occur in the baseline and remain open.

Thermal diagnostic ABI 2 now distinguishes certified equilibrium intervals from
physical substeps. The exact stage equation is `2*(substeps-certificates)`;
positive physical heat and entropy remain required. The unchanged Linux water-heat
source/save passes at frame 122 with zero rejected attempts, zero projected
endpoint energy residual against 0.00594580462278261 J, and maximum relative
energy error 7.12279e-20. Its fresh-process reload resaves at paused frame zero
and matches 120 continued frames exactly. WindowsPlayer under Proton also passes
the source/save (frame 121) and fresh-process reload with 120 exact frames.
Both source runs have zero projected endpoint residual and the same maximum
relative energy error; actual Windows OS validation remains open.
The fifteen isolated Linux domain controls also pass; the small-water-heat
control retains its inputs/bound and now observes positive conduction plus a
byte-exact version-10 U-state round trip. The diagnostic fix leaves all 77 MAC
exits and scientific outputs unchanged (75 stdout captures are byte-identical;
two differ only in timing). Volume remainders, local subcycling, performance,
the remaining current-package game scenes and milestone B remain open.

```sh
build-production/ma_mac_primary_state_tests > U_CAPTURE
python3 native/tests/review_mac_primary_state.py U_CAPTURE
build-production/ma_mac_primary_water_heat_tests
```
