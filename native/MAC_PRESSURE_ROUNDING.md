# Pressure momentum storage rounding

The production midpoint, real-Schur moment and coupled-moment pressure stages
count inexact binary64 additions to canonical face momentum. The original
`J1 = J0 + impulse` result is retained. This ledger covers storage additions;
pressure products, wall/body reductions and other stages' normal storage
ledgers remain open. Persistent primary-U and volume remainders are a separate Codex-owned change.

## Interval accounting

`PrimaryRounding` observes each addition with TwoSum. Exact additions add no
count. An inexact addition records its absolute error and half an ulp of its
stored result in deterministic double-double sums. Invalid projections,
odd nearest-even ties, the smaller gap toward a power of two, nonfinite
observations and counter overflow have explicit controls.

The accumulated charge is rounded outward with a double-double error
envelope. The interval checks that the recorded errors fit this charge and
that the charge fits the existing 128-epsilon absolute-operand budget.
The conservation bound is unchanged. Checks publish with the interval audit;
discarded substeps and intervals contribute no accepted entries.

`MacKernelAudit::rounded_primary_writes` and `rounding_allowance` expose each
quantity's count and charge. `MacKernelReport::rounded_primary_writes` sums
accepted counts, and the owner's `interval_rounded_updates` includes them
alongside input projections and below-range transfers. Checkpoint layouts,
physical selectors and stored trajectories are unchanged.

## Component evidence

`ma_mac_kernel_pressure_rounding_tests` uses 1,213 prescribed additions,
including 1,088 inexact writes: erasures, ties, signed cancellation, normal
power boundaries and the subnormal grid. Its fixed generator explicitly
sequences random words, independently of compiler argument evaluation order.

Three 193×173 periodic gas scenes use pressure
`100000 + 20 cos(2 pi x) + 10 cos(2 pi y)` Pa at 300 K, velocity (1, .5) m/s
and .002 s intervals. Both momentum axes count rounding. One/twelve workers,
cold/warm reuse, rollback and exact checkpoint continuation agree. The real
Schur method's existing first-interval refinement is retained.

| Pressure method | Accepted inexact writes |
|---|---:|
| Midpoint | 66,713 |
| Real Schur moments | 133,498 |
| Coupled moments | 66,737 |

These GNU Release counts are diagnostics, not cross-compiler trajectory
identity. Six version-6/7/8 endpoint and continuation images and all refinement
counters match a Release build of `3fbccfb` byte-for-byte, after integration
of its user-authorized gravity entropy credit.

`review_mac_kernel_pressure_rounding.py` independently checks exact fractions
of the saved operands, nearest-even storage, counts, half-ulp charges,
outward sums and unchanged interval budgets. It passes 10,974 checks and
rejects eighteen altered captures on Release, Debug ASan/UBSan, ThreadSanitizer
and MinGW/Wine. The full affected production and owner suites pass these
routes (targeted moment checkpoint modes under ThreadSanitizer). All 55
Release CTests pass. All 68 preceding MAC
executables retain physical output and exits (66 raw, two timing-only), and
the reference wave SHA is unchanged. These controls do not close the remaining
rounding, local-subcycling, physics or real-time gates.

```sh
cmake --build build-production --target ma_mac_kernel_pressure_rounding_tests
build-production/ma_mac_kernel_pressure_rounding_tests > CAPTURE
python3 native/tests/review_mac_kernel_pressure_rounding.py CAPTURE
```
