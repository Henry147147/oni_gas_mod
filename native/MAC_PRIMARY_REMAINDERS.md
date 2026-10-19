# Primary rounding remainders

Persistent U and volume remainders belong to Codex (user decision, 2026-10-19).
They are not yet part of solver state or checkpoints; production water-heat still
fails its original endpoint energy gate. This work changes no physical selector.

## Normal arithmetic primitive

`add_mac_primary(value, remainder, increment)` uses TwoSum to retain the
rounding correction in a normalized second binary64 part. Its returned
`discarded` value accounts for a third part that cannot be retained, and
`high_rounded` identifies an inexact high-part addition. Consumers must audit
that discarded part. The helper checks canonical finite inputs and rejects
overflow before publishing a result. The batch entry must check the usual MAC
nearest-even, FTZ-off environment. Exact low-part sums avoid extra renormalization.

The arithmetic remains unselected. `store_mac_primary_remainder` supplies the
reference/flagged-range counterpart: a nearest high part and a normalized low
part, with an authoritative range value only when the low part's binary64
projection rounds. It returns every discarded expansion component for the
caller's unchanged operand audit. Range rounding compares the retained exact
expansion with adjacent significands, resolving composed-sum ties before storage;
`MacEnergySum::value()` alone is only an estimate. Malformed range projections and overflowing
high parts reject. Both kernels' U writes, energy audits, checkpoint continuation
and owner save framing remain to be connected. The new format version has not
been allocated. Existing reference trajectories and checkpoint bytes stay intact.

## Component controls

`ma_mac_primary_remainder_tests` checks 264 additions with exact expansions,
including cancellation, ties, subnormals and discarded third parts. A
100,000-update control retains heat increments that ordinary binary64 addition
loses; overflow and malformed pairs reject. The saved-only
`review_mac_primary_remainders.py` checks 3,716 assertions with exact fractions
and rejects ten altered captures without launching the solver.

```sh
build-production/ma_mac_primary_remainder_tests > CAPTURE
python3 native/tests/review_mac_primary_remainders.py CAPTURE
```

All 64 Release CTests pass with the ONI backend and warnings as errors. Both
components and saved readers pass on Release, ASan/UBSan, ThreadSanitizer and
MinGW/Wine. All 73 unrelated MAC executable images, physical outputs and exit codes remain
unchanged (only two timing outputs differ). The range component intentionally
extends its saved capture with the eight composed-sum controls and fourth operand. The default wave checkpoint SHA
remains `4d250703f98fd1b00af3a7da505aaac1e7a7aa5405f24644e9b299893e8cf763`.

This component evidence does not close persistent-field, water-heat, vacuum,
local-subcycling, performance or milestone-B acceptance.

`ma_mac_primary_range_tests` covers 1,022 full-range sums, ties, cancellation,
subnormal highs and corrections below binary64. Eight added controls retain a
term smaller than a low-part rounding tie; the original 1,014 arithmetic records
remain numerically identical. Every discarded result passes a half-low-quantum
check, in addition to the unchanged 128-epsilon operand account. Its saved-only exact-fraction
reviewer checks 25,341 assertions and rejects thirteen altered captures. The
normal and range components and both readers pass ASan/UBSan, ThreadSanitizer
and MinGW/Wine. These are arithmetic controls, not persistent solver state.

```sh
build-production/ma_mac_primary_range_tests > RANGE_CAPTURE
python3 native/tests/review_mac_primary_range.py RANGE_CAPTURE
```
