# Incoming inventory storage rounding

The production transport gather observes every binary64 addition of an incoming
shared parcel to a constituent or passive inventory. Stored values, parcel
construction and addition order stay unchanged. Retained-parcel subtractions,
ordinary products, primary U and momentum writes outside pressure remain outside
this ledger. Persistent primary-U and volume remainders are a separate Codex-owned change.

## Interval account

Each fixed row block uses the existing `PrimaryRounding` TwoSum observer and
merges its per-quantity counts, absolute errors and half-ulp charges in block
order. Exact additions contribute no charge. The outward charge and the existing
pressure charges must fit inside the unchanged 128-epsilon interval budget;
the gather's original operand magnitudes are retained. Failed substeps and
intervals publish no entries. Counter aggregation checks overflow.

`MacKernelAudit::rounded_inventory_writes` identifies the quantity's accepted
count. `MacKernelReport::rounded_inventory_writes` sums it, and the closed
owner includes it in `interval_rounded_updates`. These are transient diagnostics;
there is no new checkpoint field or selected physical method.

## Evidence

`ma_mac_kernel_inventory_rounding_tests` uses a periodic 4x2 gas grid,
velocity (1, .5) m/s and a `2^-10` s step. Its explicit donor-cell fixture
independently derives both incoming parcels and retained inventories. Three
constituents and two passive identities include large/small arrivals and
updates below storage resolution. All 80 resulting storage values match;
70 additions are inexact on GNU Release. Cache reuse, discarded intervals and
source preservation pass.

The saved-only `review_mac_kernel_inventory_rounding.py` reconstructs the
shared donor-cell geometry and binary64 writes with exact fractions, derives
counts and half-ulp charges, and checks the original interval budget. The
GNU capture passes 1,431 checks and rejects fourteen altered captures.

The existing 193x173 pressure-storage controls now also check inventory counts
and charges with one/twelve workers, cold/warm reuse, rollback and exact reloads
for midpoint and both moment methods. First-interval GNU counts are:

| Pressure method | Inexact incoming inventory additions |
| --- | ---: |
| Midpoint | 333,418 |
| Real Schur moments | 667,270 |
| Coupled moments | 333,401 |

These are build-specific diagnostics. Six version-6/7/8 endpoint and
continuation images and every refinement counter match `7c69bc7` byte for byte.
All 71 prior MAC component outputs/exits match `c817680` (69 raw text, two
timing-only differences); the default wave checkpoint SHA stays unchanged.
All 62 merged Release CTests pass with the ONI backend and warnings as errors.
Selected kernel, reference-regularization and owner groups pass Debug
ASan/UBSan, ThreadSanitizer and MinGW/Wine. The two moment suites use targeted
checkpoint modes under TSan. All five saved-data readers pass on all four
routes; the inventory and pressure readers also pass after `c817680`.
These checks do not certify Windows OS or a new game package.
Other rounding ledgers, calm-region integration, physics and real-time acceptance
remain open.

```sh
cmake --build build-production --target ma_mac_kernel_inventory_rounding_tests
build-production/ma_mac_kernel_inventory_rounding_tests > CAPTURE
python3 native/tests/review_mac_kernel_inventory_rounding.py CAPTURE
```
