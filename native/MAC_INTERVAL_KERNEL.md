# Retired interval kernel

The first donor-cell ideal-gas interval port (numerical selector 1) is retired.
Its solver, owner cache and admission bridge have been removed. The consolidated
[production kernel](MAC_KERNEL.md) uses the ported [geometric multigrid](MAC_MULTIGRID.md)
and saved-data controls, with broader physical coverage and one production pool.
The strict per-stage reference remains the validation oracle.

Selector 1 is reserved as `MacNumericalKernel::retired_interval_audited` and
rejects on construction. Native checkpoint version 4 and version-5 images carrying
selector 1 reject explicitly as retired; they are never reinterpreted as selector
2. Versions 2/3 and version-5 reference images retain their bytes and continuation.
Production selection 2 uses [version 6](MAC_CHECKPOINT.md#version-assignment).

`ma_mac_kernel_saved_tests` preserves the original seven scientific scenes:
periodic gas, walls, a one-dimensional strip, 10,000 closed steps, 81 species/82
passives, periodic multigrid and disconnected multigrid. Its independent reader
`review_mac_kernel.py` keeps the original strict endpoint inventory, impulse,
U+K and entropy bounds, in addition to checking the production report's counted
stage magnitudes and below-range operand allowance. It rejects seven altered
captures. Cache controls compare fresh-thread storage with changed geometry,
constituent/passive layouts and caloric models, plus concurrent related owners
and continued execution after heat/mass edits.

The former fallback scenes (limited reconstruction, subnormal traces, mixtures
and empty cells) retain their inputs and now exercise production admission.
Unported physical selectors still use the audited reference. Near temperature
or caloric boundaries, ordinary EOS arithmetic requests reference refinement;
a certified exact static state can advance unchanged. A genuine mid-interval
range flag must charge discarded attempts and preserve rollback.

The old saved reader `review_mac_interval_kernel.py` remains available for
historical captures; it never launches a solver. Historical measurements of the
removed port do not certify current active-cell performance. Current reproduction
and evidence are in [MAC_KERNEL.md](MAC_KERNEL.md#validation).
