# Native solid-cell heat

The direct backend now includes a bounded fluid/solid temperature-pair bridge.
ONI owns the solid cell and its ordinary save data. The custom engine owns the
fluid. Their contact must exchange one represented heat quantity on the native
frame, rather than allowing a scalar compatibility cell to receive that heat.

Before positive time, the adapter atomically registers supported solid faces
using additive capability `ma_oni_domain_solid_heat_*`, version 1. Each 48-byte
record binds a completed source session/frame, public cell, native material
index, mass, specific heat, conductivity and native insulation byte. Registration
rejects duplicates, nonadjacent cells, invalid thermal facts and pending fluid
work. Every actual contact rechecks material, exact mass bits and insulation.
A change faults the bounded owner before that contact changes either endpoint.

The existing exact-build `UpdateTemperature` hook handles a pair with one owned
fluid cell and one registered solid. Linux and Windows layouts are independently
inspected: `SimData.cells` / `updatedCells` pointers are at offsets 32 / 40;
CellSOA element, temperature, mass and insulation data pointers are respectively
0 / 24 / 48 / 96 on Linux and 8 / 40 / 72 / 136 on Windows. The complete native
library hash and existing call-site signatures remain required.

Native pair updates accumulate in `updatedCells`. The bridge reads that current
solid endpoint and writes only its represented result there, preserving both
earlier native contributions and the original temperature grid. The shared frame
interval is visible to native job threads; a thread-local interval missed all
licensed contacts. A face is handled at most once per completed fluid frame,
including multiple active regions. External native pairs retain their original
dispatch. A later ambiguous frame failure requires normal fault/reload recovery.

The passive implicit [body/contact solver](native-body-heat.md) also serves this
boundary. Solid capacity is the product of its separate native binary32 mass
and specific heat, retained as binary64. There is no extra binary32 capacity
rounding. Conductivity uses the harmonic mean of the full-EOS fluid value and
the native solid value, after applying each endpoint's squared native insulation
fraction. This is the declared custom contact model. It does not claim vanilla
heat-rate equivalence. Unrepresentable heat leaves both endpoints unchanged;
there is no minimum heat flux.

The 112-byte completed solid status is separate from native body/item status.
Source `0x4d415348` (`MASH`) identifies each accepted heat command in the ordered
frame-input journal. Native solid temperature loss, full fluid U+K, represented
heat, balance bounds and entropy are checked independently. Saved continuation
replays those actual inputs without predicting native solid processing.

The development scene is `native-solid-heat-domain`, `solid-heat` in the matrix,
or `-NativeDomain solid-heat` on Windows. It places one 10 kg, 400 K granite face
beside mixed air, with an unobtanium backer. Paired-save admission accepts that
one supported contact and verifies its mass, solid phase, temperature range and
backer before native loading. The ordinary native save retains its temperature;
the adapter reconstructs and checks the thermal binding before restored time.

All 20 native groups pass on GNU Linux and Windows binaries under Wine. Both
managed adapters compile; 172 declaration scenarios pass on each platform, and
the Python suite passes 261 checks. Four licensed worker controls pass: gas and
water on Linux and the Windows native library under Wine. Each replays 61 fluid
frames exactly and observes 56 actual native solid contacts. Gas cools the solid
to 399.9931640625 K with 54.003907716833055 J loss; water reaches
399.8564453125 K with 1134.0820620534942 J loss. All four close their existing
128-epsilon energy bounds. The independent saved-only review SHA-256 is
`ec94ff2c520690684f15d1741bd2c2616d46add3cf33d192a6430c0303e35e23`.

The first licensed control is retained as failed: native job threads could not
see its thread-local duration, so it recorded no accepted contact inputs. The
shared-frame interval fixes that failure. All 20 groups also pass on the clean
Ubuntu 22.04 build and under ASan/UBSan (563.40 s). A Linux control with the
owned room in the second of two active native regions passes its 61 exact
worker frames and actual contacts as well. Actual Unity source/save/reload
controls are queued. The first Unity attempt stopped before ownership because
arena construction uprooted nearby flora and spawned an unbound seed into the
chamber. Placement now reserves a three-cell object-free margin around every
changed fixture cell, including its backer and outside conduction pair. The
actor guard remains active. The next actual Unity source reaches two positive
contacts, then rejects a published solid endpoint that accounts for one
represented temperature step while the completed fluid journal accounts for
two. This failure remains retained; Unity acceptance is still open.

A separate 64-byte endpoint observation records the last completed contact's
native old-grid temperature, accumulated before/after temperatures, actual
interval, cell identities and contact/owner frames. It publishes atomically
with the fluid heat account and changes neither physical endpoint nor the
checkpoint format. The managed report also retains the native publication's
processed-frame count. These observations distinguish publication timing from
lost native heat without changing the conservation bound.

A second 64-byte observation reads the native old and updated cell arrays
after each worker sub-update, tagged with its completed fluid frame and native
ticket. It retains material, mass and insulation facts separately from the
prepared game buffer. Stale observations reject before changing completed
evidence. This is diagnostic evidence; it does not repair or accept the
currently failing Unity boundary.

The licensed worker control now records those completed arrays as well. Linux
passes both 400 K and the failing colony's exact 369.62255859375 K initial
temperature; the Windows native library under Wine also passes the latter.
Each control replays 61 complete fluid frames and checks 56 actual contacts.
The weaker controls end at 369.619140625 K with 27.001953858416528 J body loss.
Both native arrays match the published temperature throughout these controls,
and the unchanged energy bounds pass. A saved-only review of all 168 contact
samples has SHA-256
`18cb763476763df39a5cc33c7480fb9f308052df0c46459bb48a1dfad3a4d8e7`.
`MA_NATIVE_SOLID_TEMPERATURE_BITS` selects an exact hexadecimal binary32 initial
temperature in this standalone control. It does not change the mod's gameplay
settings. Actual Unity publication remains the failing gate.

These Linux and Wine controls do not establish actual Windows OS support.
The separate Windows-host controls below provide that scoped evidence;
whole-colony and actual Unity heat acceptance remain open.

Fresh isolated worker controls now also pass on the actual Windows host with
its installed ONI 744825 SimDLL. Gas and water each complete 61 exact input
replays and 56 positive solid contacts, reaching the same solid temperatures
and represented losses listed above. Saved review reconstructs solid loss from
the original binary32 temperatures and the separate licensed mass/specific-heat
values. The report retains summary fluid-energy bounds and journal digests;
it does not export raw journals or every fluid/contact operand. These passing
solid controls are retained inside the checkpoint whose separate cold-JIT
startup self-check initially rejected; see the [wire correction](native-float-wire.md).

The full Windows/Linux/ASan/UBSan regression passes 239 native groups and 40
saved-data reviews per environment. The sanitizer build's 95 composed compile
commands are all instrumented: 83 C++ and 12 C. Review SHA-256:
`c84be8ffe7e0e3e2b215c2facad4e79aed51544308e10af8506fdb4115af7f78`.
Actual Unity source/save/reload controls remain open, together with whole-colony
heat, arbitrary moving/changing solids, building thermal components and
phase-changing boundaries.
