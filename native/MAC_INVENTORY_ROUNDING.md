# Constituent and passive storage rounding

Production transport observes every binary64 subtraction of a shared outgoing
parcel from its retained constituent/passive inventory, any accepted exhausted-
field parcel adjustment, and every incoming inventory addition. Stored values,
parcel construction and operation order stay unchanged. Ordinary products and
momentum writes outside pressure remain outside this ledger. Primary U uses its
separate persistent correction and write accounting.

## Interval account

Each fixed row block uses the existing `PrimaryRounding` TwoSum observer and
merges per-quantity counts, absolute errors and half-ulp charges in block order.
Exact writes contribute no charge. Reconstruction trials reset their candidate
ledger; only an accepted decomposition reaches the interval. Failed substeps
and intervals publish no entries. Counter aggregation checks overflow.

The outward charges must fit inside the original 128-epsilon interval budget.
The gather's existing operand magnitudes and all endpoint bounds are unchanged.
`MacKernelAudit::rounded_inventory_writes` identifies each quantity's count;
`MacKernelReport::rounded_inventory_writes` and the owner's
`interval_rounded_updates` include retained and incoming writes. These are
transient diagnostics, with no checkpoint field or physical selector.

## Evidence

The unchanged periodic 4x2 donor-cell fixture uses velocity (1, .5) m/s and a
`2^-10` s step. Three constituents and two passive identities include large/small
arrivals and updates below storage resolution. Independent operands now cover
160 writes: 56 retained subtractions and 70 incoming additions are inexact.
Every stored endpoint is unchanged. Cache reuse, discarded intervals and source
preservation pass.

The saved-only `review_mac_kernel_inventory_rounding.py` derives shared donor
geometry, all writes, counts, half-ulp charges and the original interval budget
with exact fractions. It passes 2,368 checks and rejects fourteen corruptions.
It also reads historical 80-addition captures with their original accounting.
The four selected inventory, pressure, carried-U and transfer-rounding suites
pass ASan/UBSan, ThreadSanitizer and MinGW/Wine, with identical saved scientific
records. In a clean Release build, 79 of the 80 MAC component outputs match the
preceding build; the other is this test's own capture. All 73 CTests pass.

The existing 193x173 pressure controls cover actual one/twelve-worker execution,
midpoint and both moment methods, cold/warm reuse, rollback and exact reloads.
Six version-10 endpoint/continuation images match the preceding build byte for
byte. GNU interval counts in those unchanged controls are:

| Pressure method | Incoming only | Retained plus incoming |
| --- | ---: | ---: |
| Midpoint | 333,418 | 666,841 |
| Real Schur moments | 667,290 | 1,334,590 |
| Coupled moments | 333,401 | 666,807 |

Full physics, other rounding ledgers, local subcycling and real-time acceptance
remain open. Wine supplies Windows-target evidence; actual Windows OS runs remain open.

```sh
cmake --build build-production --target ma_mac_kernel_inventory_rounding_tests
build-production/ma_mac_kernel_inventory_rounding_tests > CAPTURE
python3 native/tests/review_mac_kernel_inventory_rounding.py CAPTURE
```
