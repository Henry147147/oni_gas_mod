# Freeze/melt evaluation

## Root cause, 2026-11-02

The refreeze is driven by real heat flow, but the water that freezes is
in the wrong place. The fixed gate (fill/source/check 2/2/42, >380 kg owned
water) is unchanged and still fails on the default profile 9.

**Energy.** The 2026-10-31 audit below closes the fluid region's U+K+G with
the journal heat and the departing parcels to 2e-8 J; the 16 t granite block
at 40 K absorbs 12.1 MJ and warms by about 1 K. No energy is lost or
misbooked. All 111.7 kg of ice comes from the owner's hidden-liquid freeze
(liquid in cells the projection shows as gas), not from native freezing.

**Placement.** The owner's own per-frame compositions
(`artifacts/release-ready-2026-10-30/freeze-custody-base-b1-v1/freeze-audit`) show where the liquid goes. One
frame after the fill, the 20 kg cold-pool cells on the block have already
spread water 4 cells up into the air (14.4, 7.6, 3.3, 0.8, 0.06 kg in the
left column), and the melt has thrown water over the whole arena. At the
check about 52 kg of liquid still hangs in the five rows above the floor at
0.2–3 kg per cell, and 108.7 kg of ice sits on the block's row, against the
80 kg that started there. Liquid never falls out of a gas cell in profile 9:

- Placed liquid holds vacuum (ONI's element replacement and melting give a
  liquid-only cell). Air rushing into that vacuum, and the falling melt,
  move gas through cells that hold liquid.
- The game kernel's remap (`patch.hpp`) moves every constituent of a donor
  in proportion to the swept volume. Gas leaving the top of a partly filled
  cell therefore carries the cell's liquid fraction upward, though that
  liquid lies at the cell's bottom.
- Nothing returns it: layering exchanges only liquid phases, and interface
  sharpening is weak for small fractions and moves liquid toward the fuller
  neighbour, upward above a misty layer.

The cold air over the block then freezes this suspended water and the extra
water carried onto the ledge. An offline 8 x 6 replica of the arena with the
same kernel settings reproduces the first frame closely (left column 14.3,
7.4, 2.9, 0.6 kg).

**Repeats.** Unchanged builds repeat exactly: 368.3 kg in six base-b1 runs
of five packages (2026-10-29 to `722c0a3` on 11-02), 387.3 kg twice,
423.2 kg three times. The
results differ between revisions that change solver arithmetic or the source
timing (337.9–423.2 kg), and slightly between batches (365.2 kg alone,
368.3 kg in base-b1), because the splash is sensitive to its inputs.

**Options (user decision).** See
[the game kernel](../native/MAC_GAME_KERNEL.md#suspended-liquid-and-the-layered-vertical-remap).
A layered vertical remap that draws gas first through a top face and liquid
first through a bottom face keeps placed water out of the air. With it the
unchanged gate passes in game (429 kg, 47.7 kg of ice on the block), and so
does `airflow`; `phase` then fails. It would be a new game profile, so it is
not adopted.

## Original audit, 2026-10-31

The original base-b1 gate still fails. Its fill/source/check frames remain
2/2/42 and its late owned-water requirement remains >380 kg. No input, grid,
clock, phase rate, numerical bound or acceptance change is adopted here.

The original warm400 kg ice reaches native physics at290 K in both buffers.
The journal then admits400 kg water at288.4981689453125 K, alongside the four
original20 kg cold-water cells. At check42,368.293808 kg liquid plus
111.706198 kg visible nonabsorbed ice accounts for480 kg within about5.4 mg,
including binary32 debris effects; no product is pending. That discrepancy is
measured, not a newly accepted tolerance.

## Original replay and decoder

Read-only extraction recovers the original save's exact owner frame3. The
corrected retained-input-prefix replay reaches152 both cold and after native
menu stop/restore. A passive arena observer preserves all298 complete
owner/status hashes. The original43 composition/region/ledger observations
match both branches under the producer runtime's decoder. Fresh session-local
native tickets have explicit constant offsets. Original full future-owner
images were not captured: this is a sampled original-game oracle.

The initial strict Python comparison retains two mismatches for one value.
Actual Mono/Newtonsoft writes binary64 `0x3fd9c803863c4e88` as
`0.402832871522357`, then parses that token back to those bits. Python selects
adjacent `...4e87`. Exact-bit and neighbouring-value controls reproduce it.
A separate actual-Mono export decodes13547 floats, preserves all nonfloat
structure and verifies all43 original input hashes unchanged. The original
strict result remains failed. No mask/tolerance or recovery of unserialized
bits is claimed.

## Departing conversion energy

A second passive observer records already-computed parcels and actual emitted
receipts. All298 owner/status hashes and the arena-observation byte stream
remain exact. Eight controls verify errno, rounding, flags and MXCSR
preservation; altered settings are not solver-admissibility claims.

Each branch has241 identical local freeze conversions, frames116–152:

| Operand | Measured value |
| --- | ---: |
| Departing fluid / planned ice |111.70619222449922 kg|
| Emitted binary32 mass sum |111.70619231462479 kg|
| Parcel temperature range |202.232853–272.416558 K|
| Mass-weighted temperature |265.429546 K|
| Carried internal energy U |−15274552.391244 J|
| Carried resolved kinetic K |67.559044 J|
| Carried potential G |210586.211220 J|

All are ice receipts at the original source frame/cell. These parcels have no
passives, so this cohort does not test passive custody. From post-source113 to
check152 there are no other local transfer/terrain/native-freeze inputs.
Local journal heat is−12146387.888846 J; regional delta(U+K+G) is+2917510.732134 J;
departing U+K+G is−15063898.620980 J. The account
`delta(U+K+G) - journal_heat + departing(U+K+G)` has maximum absolute residual
1.94336e-8 J over the suffix. Exported energy includes the fluid EOS formation
reference; omitting it would misread the regional rise as unexplained heating.

This accounts for the fluid region in this cohort. Native solid creation,
merging, later solid heat and a compatible caloric reference remain open
before joint native/owner energy closure can be claimed. No fusion heat or
new law is introduced to fit the fixture.

## Pending observable proposal

The private proposal keeps the native ice proof, final open/owned source cell,
>380 kg water bound and deadline42, but observes that bound at any coherent
frame from source2 through42. It retains late liquid as a diagnostic and all
other checks. The measured peak is480 kg; subsequent111.706 kg refreezing
explains the failed late liquid-only observation. Eight synthetic controls
reject never-melted ice, exact380 kg, late/pre-source-only water and invalid
native/source witnesses. No new custody tolerance is proposed.

The temporal-observable change requires the user's decision under
[AGENTS.md](../AGENTS.md). It remains pending, unintegrated and uncounted as a
gate pass; joint solid energy is separate. Private evidence is under
`artifacts/release-ready-2026-10-30/{freeze-custody-v1,freeze-custody-base-b1-v1,freeze-energy-v1}/`.
