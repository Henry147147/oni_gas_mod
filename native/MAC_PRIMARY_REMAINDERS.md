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

The primitive is unselected. Flagged range values still need a corresponding
remainder path, and both kernels' U writes, energy audits, checkpoint continuation
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

All 63 Release CTests pass with the ONI backend and warnings as errors. The
component and saved reader pass on Release, ASan/UBSan, ThreadSanitizer and
MinGW/Wine. All 72 preceding MAC executable images and physical outputs remain
unchanged, including the default wave checkpoint SHA.

This component evidence does not close persistent-field, water-heat, vacuum,
local-subcycling, performance or milestone-B acceptance.
