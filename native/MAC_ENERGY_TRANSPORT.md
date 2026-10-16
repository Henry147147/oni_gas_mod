# Wide derived energy in MAC transport

Four integrated controls and all prerequisite regressions pass on Windows and
Linux. The passing
[range arithmetic component](MAC_ENERGY_RANGE.md) is now used by kinetic
parcels, positive variance heat, cell heat sharing and closed-owner energy
accounts. This is a scoped numerical result. The recorded
[strong-flow failures](MAC_RIEMANN.md) remain current evidence.

Every face retains full derived old/new/transported K, mixing heat and wall heat
as significand/exponent values. Legacy binary64 report fields are projections,
with a per-face mask and aggregate count for rounding. Accounting uses the full
values, including local transported-K to resolved-K/heat closure at the original
128-epsilon actual-operand bound. Primary constituent/U/passive inventories and
canonical face J remain binary64; their positive-transfer checks are unchanged.

Cell heat adds to the retained primal-U components before one explicit stored-U
projection. A missed update increments the existing unresolved-heat counter
against 32 epsilons of the actual requested heat. Nonzero heat below binary64
range remains in the reports and global accounts. No density, constituent,
momentum or heat threshold is introduced.

Pressure/composed/interval energy balances retain wide K and total-potential
terms and compare exact additive expansions. Per-stage allowances keep their
original definition and sum into the interval allowance. Pressure impulse,
material-volume, shared limiter, timestep, energy reference and EOS tolerances
are unchanged. Gravity source/internal-work fields retain their existing
binary64 treatment; this integration does not claim every gravity intermediate
has wider range.

Four new controls pass:

1. Periodic gas transport with normal momenta near 1e-200, positive K and heat
   below binary64 range, and explicit unresolved report/U updates.
2. The original kinetic-parcel scale with a 3.619730186e-151-kg mass flux and
   representable incoming momentum. The below-range K parcel, target K and
   actual finite energy residual must remain nonzero in the wide accounts.
3. Warm gas cells with nitrogen masses 1e-200/2e-200 kg, unchanged formation
   energies/Cv and 300-K temperatures. At dt=1e-104 s, choose initial canonical
   J as minus half the actual pressure impulse. The pressure step reverses J
   with zero midpoint volume sweep. It exercises subrange K in constructor,
   candidate and atomic interval accounts without an unrepresentable primary
   parcel. Owner clock/revision/source identity and report projections must agree.
4. A positive denormal passive parcel must still reject atomically. The energy
   repair does not weaken primary quantity preservation.

The saved-data reviewer reads full face/summary significand-exponent records,
checks their legacy projections/masks, and reconstructs dual J/K, positive heat
and U accounts with exact fractions. Gas pressure, temperature and entropy are
computed independently at 80 digits. The warm thin pressure reversal has zero
volume work and a checked wall impulse. Altered primary trace, wide heat and
unresolved-update records must reject.

The seven energy-range, seven original transport, five parcel, eight closed-owner
and four smooth-gravity groups also pass with their separate reviews.
Scientific gravity initialization/source, inputs, grids, timesteps and bounds
stay unchanged. Existing core/occupied regressions remain pinned: their numerical
source and compiler settings do not change. No older failed physical gate is
replayed as a regression. The Linux strong-flow retry after these prerequisites
is described below. The Windows collision failure needs
a relevant accuracy repair before repeating its failed physical gate.

Fresh strict Windows/MSVC and Linux/GCC15 builds/results are retained, with
aggregate build 180 s, new native/review 60 s each, original transport/range
30 s, parcel 180 s, closed/gravity 60 s and 512 KiB per raw pipe. These bounded
correctness runs are not production performance measurements. No strong-flow,
free-surface, phase, game or shipping acceptance follows from these controls.

The first integrated Windows/Linux builds both failed before native execution:
momentum validation still called the removed binary64 kinetic helper. The call
now uses the wide kinetic value and explicit projection. Those first compile
results remain retained; no physical criterion, source inventory, energy
reference or timestep changed to correct this wiring error.

## Integrated results and remaining primary transfer failure

Fresh R2 strict Windows/MSVC and Linux/GCC15 builds pass all scoped controls.
Each separate saved-data reviewer rejects three altered records:

| Control | Native groups | Saved checks per platform |
| --- | --- | --- |
| Wide transport/owner | 4 | 4,530 |
| Range arithmetic | 7 | 21,019 |
| Original transport | 7 | 3,888 |
| Material reconstruction | 5 | 13,325 |
| Closed pressure/transport | 8 | 6,530 |
| Smooth gravity | 4 | 16,462 |

The added encoded-energy records/checks explain the increased saved-check
counts. Existing scientific gravity source, initialization, pressure/entropy
limits and grid/timestep sequence are unchanged. The original kinetic parcel
scale is retained through transport, including positive target K and an actual
energy residual whose binary64 projection is zero. The 300-K thin-gas reversal
passes constructor, candidate and atomic owner checks without changing its
caloric reference or moving volume. Primary denormal-passive rejection remains
atomic.

The Linux strong-flow retry still fails before any completed Sod profile. It
now rejects a primary helium parcel, rather than the earlier derived K parcel.
An instrumented first-rejection diagnosis records donor 43, field 7 (helium),
source inventory 3.0517578124999999e-33 kg, parcel volume
6.8694555346832573e-296 m³ and donor volume 0.000244140625 m³. Exact binary64-
operand arithmetic gives **8.5868194183540713e-325 kg**. This is below half the
smallest positive binary64 value, so nearest projection would be zero. The
unchanged primary check rejects it; no helium is clipped.

The full strong-flow reviewer and corruption controls are not run because no
profile completes. The Windows collision failure is carried forward without
another failed-gate invocation. A primary parcel representation must retain
these contributions while maintaining per-constituent donor/target conservation
and positive inventory before this path can complete strong-flow acceptance.
No native full-gate retry followed that diagnosis.

Private first captures are
`local/occupied-staggered-validation/mac-energy-transport-{windows,linux}-r2`.
Completed scoped-review SHA256 values are
`9edc28bda685b9361a1998330f4cc9b69f63e9f29b2654dd323340baae2c1316`
(Windows) and
`0b065890cfb91e587d198892b9e19442daace526638315935e817095093bfcbf`
(Linux). They bind source/compiler/binary identities, EOF-checked captures,
all prerequisite reviews, the retained R1 compile failures and the Linux
primary-transfer diagnosis. The Linux strong retry retains its original
180-second/2-MiB bounds. No independent agent review, Unity launch or production
performance acceptance occurred. The default game DLL is unchanged.
