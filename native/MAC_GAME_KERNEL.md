# Diagnostic game kernel

Selector3 and game-profile family2 are explicitly opt-in for private in-game
validation. New colonies request flag4 (normally flags5 with conduction), and
the managed selector requires capability 1. Old saves retain their own profile;
there is no implicit conversion. Physics gates and the reference keep their
existing implementations and settings.

An additional explicit creation selector, `MA_COLONY_KERNEL3_POSITIVE=1`,
selects profile3/core21 while kernel3 is enabled. The original selector alone
continues to create profile2/core20. `MA_COLONY_KERNEL3_SURFACE=1` creates
profile8/core27 ([below](#carried-interface-momentum-and-speed-profile-8)). Saves
restore their recorded law and work caps independently of any creation selector.
These profiles remain opt-in; floor and colony acceptance are open.

The backend runs the accepted semi-implicit design's sparse pressure/transport,
fluid heat, latent phase relaxation, density layering and bounded liquid
interface correction. Every derived reading and edit uses its own EOS: ideal
gas plus a saved liquid law and stiffness (linear in profile2, positive-volume
in profile3). It never
uses the reference evolution solver as a fallback. Once-per-tick connected
region audits check species, passives, U+K+G and canonical momentum with wall
and body impulses, at the accepted relative1e-12 operand bound. Traces enter
the named sink. Entropy is reported as a diagnostic, without admission retries.

The owner bridge retains dense compatibility state around the sparse tick.
Its per-edit copies and audit passes are now mostly removed; see
[parallel owner thread](#parallel-owner-thread-and-in-place-owner-edits). Native shadow/body heat remains an external booked exchange; duplicate
internal fluid conduction, phase relaxation and settling are skipped. Opened
cells start empty and expand through physical face flow. Core20 stores sparse
species/passives, energy, face momentum, geometry/masks, clock/revision,
numerical settings and calibrated phase pairs; no cache affects continuation.

The colony owner defaults to the number of physical CPU cores. The mod's
[Performance settings](../docs/settings.md#preferences) can select an explicit
count in the supported 1..256 worker range, including SMT threads. This
replaces the earlier logical-CPU-minus-two choice. Worker preferences apply
between frames and after restore, without entering the colony save. The game
kernel honours that request without a twelve-worker cap; small grids still
limit the pool to their fixed row blocks. `MacClosedAdvanceReport::game_threads_used`
reports the pool size, including the caller, without entering the checkpoint.
The focused control requests 1/12/13/22/30/256 workers on 31 blocks and checks
actual pool sizes, exact physical images and cold continuation against one
worker. This changes scheduling only; it does not establish a throughput gain.

On the Ryzen 9 5900X (24 logical CPUs), eight interleaved 20-frame Terra
profile5/core23 frame-50 snapshot runs per count under the whole CPU lock give
warm means of **84.44 ± 2.38 ms at 12 workers** and **90.73 ± 1.51 ms at 22**
(mean ± sample SD across runs, first frame excluded). The extra workers are
**7.45% slower**, with all eight pairs slower. Final checkpoint bytes match
between counts and parent `8c1285e`. This measures offline solver advance;
30-worker throughput on the user's 9950X remains unmeasured. Linux Release
passes 86 CTests; all 86 applicable MAC executable outputs and exit statuses
match the parent except timing fields and the new control's PASS line.
The changed C++ units also pass MinGW syntax checks; no Windows runtime check
is claimed.

Focused controls cover own-EOS imports/readings, core20 round trips,
worker-independent and cold continuation, heat/parcel/wall energy, full passive
observations and malformed images. A separate native-owner control exercises
flag5, door opening, heat, publication and exact saved continuation.
The first corrected whole-lock Terra60 at requested fast speed completed with flags5,120flow halves, no
rejections/faults/skipped phase, M/E1.67e-16/7.10e-16 and trace 1.183e-7kg /
0.12784J. Owner mean/p95/max210.27/226.40/275.22ms; observed spacing
266.07/404.87/766.01ms. The first diagnostic had omitted trace receipt counts;
core and owner regressions now require those counts and actual sink booking.
This is basic execution/conservation evidence, not a floor or performance pass.


## Exact integration speed step

Caching material-profile indexes, sharing an unchanged settled face-mask state
(with the same revision increment), and moving tick arrays change no solver
arithmetic or checkpoint bytes. Clean Release85CTest and all 100 applicable MAC
outputs (99 legacy invocations plus the new kernel control) match the parent,
apart from timing fields. Actual core20 twelve-tick images match at 1/4/8/12
workers, and native-owner continuation controls pass at each count.

Clean whole-lock Terra60, requested fast 2, identical managed assemblies and
12workers; mean/p95/max in milliseconds:

| Metric | Before | After |
| --- | --- | --- |
| Owner advance |214.421/237.971/259.746|194.112/210.494/247.371|
| Observed interval |256.714/342.880/747.360|239.111/344.533/726.670|
| Relative mass/energy error |2.20e-16/7.72e-16|1.67e-16/7.09e-16|
| Trace sink kg/J |1.17480e-7/0.127785|1.18098e-7/0.127624|

Both complete60frames,120flow halves, zero rejections/faults/skipped phase.
Measured owner mean falls 9.47%, observed mean 6.86%; this does not meet2x.
The user's vLLM workload shares the GPU outside CPU locks, so these runs do
not establish isolated causal speed gains or final GPU performance acceptance.
The owner timer ends before projection/publication. Whole-lock offline AB/BA
repeats pinned to physical cores give 104.130/103.042ms per tick (~1%); the
larger gain is in owner edits/readings. Fixed floor errors are unchanged by
this exact step; their acceptance remains open.

Energy report fields expose the accepted audit residual and bound in joules,
with matching wide fields. This corrects a diagnostic units error; the native
owner ledger audit and core20 physical bytes are unchanged.

## Whole-asteroid reliability

The unchanged `f329bff` diagnostic bundle fails both new LOW-lock checks.
Oceania completes 2/60 frames in two runs, then its post-transport EOS rejects
a liquid strain. Completed-frame mass/energy errors are 1.52e-16/3.16e-17 and
1.95e-16/2.17e-17; trace sinks are 8.85e-10kg/0.001013J and
9.02e-10kg/0.001019J. Volcanea completes 0/60: a pressure component exhausts
the200-iteration cap. All runs exit and the game process is verified gone.
These failures block whole-asteroid acceptance for profile2. The profile3
checks below clear these particular failures; private-release acceptance stays open.

Exact captured inputs reproduce both failures offline. A private400-iteration
cap clears 60 Volcanea ticks with global/region audits and sink1.06714e-7kg /
0.139128J; this is not an in-game or saved-profile acceptance. Oceania still
fails after experimental post-transport trace booking through the1mg ceiling.
Neither experiment is adopted. Raising stiffness alone also fails the captured
Oceania case; those probes initialize fresh worlds at declared temperatures,
and do not continue an old save under different settings.

A separate passive-free isolated-cell control exposed an export crash: an
absent sparse passive column has no offsets. Export now skips that column.
The focused control advances a quiet cell, repeats after a refused parcel,
and compares requested 1/12-worker cold continuation. Existing successful
solver arithmetic and checkpoint bytes are unchanged.

An initial exact liquid-stage prototype reused existing EOS values and an
ordered phase buffer. Six whole-lock offline AB/BA runs on the captured Terra
world measured 105.587 to 103.057ms per tick (2.40%); all six30-tick terminal
images matched. Refreshed profile3 and in-game evidence is recorded below.

## Suspended liquid and the layered vertical remap

**Defect (profiles 2-9).** The donor-cell remap moves every constituent of a
donor in proportion to the swept volume. A cell holding gas and liquid keeps
its liquid at the bottom in reality, but gas leaving through its top face
carries the cell's liquid fraction with it. Once lifted, liquid stays: one
velocity per face cannot let it fall through the gas, layering exchanges only
liquid phases, and interface sharpening is weak for small fractions and moves
liquid toward the fuller neighbour, which above a misty layer is upward.
Placed liquid makes this common: ONI's element replacement and melting leave
liquid-only cells with vacuum headspace, and the gas that rushes in and back
out lifts the liquid. In the `airflow` arena one frame after the fill, the
990 kg water cells have spread 167, 19 and 0.45 kg into the three air rows;
mist stays in them for the whole run. In `freeze-melt` about 52 kg of water
hangs in the air at the check and freezes over the 40 K block
([evaluation](../docs/freeze-melt-evaluation.md)). Mass and energy stay closed.

**Profile 10 / checkpoint 30 (adopted for new colonies, delegated decision
2026-11-02).** New colonies create it; saves keep their profile, and
`MA_COLONY_KERNEL3_PROFILE9=1` still creates profile 9 for its controls. A cell holding gas and
liquid keeps its phases in gravity order during the remap. Horizontal faces
sweep every phase in proportion; a top face draws whole phases from the top
(gas, then lighter liquids) until its volume is met; a bottom face draws from
the bottom (heaviest liquid first). Phase volumes and energies come from the
donor's own EOS reading and the retained energy is the remainder, so species,
U+K+G and passives stay exact (passives move with the mass share). The dual
momentum remap and the potential work use the mass that actually crosses.
Where draining the heaviest phase first would export more than the
interval's 0.39 outgoing mass bound, the donor blends toward a proportional
bottom sweep just enough to meet it (its top face stays layered). A donor
whose retained part would lack positive thermal energy keeps the proportional
sweep. Profile 10 keeps profile 9's activity tiers and their saved state.

Measured before adoption (private evidence under
`artifacts/physics/` in the physics worktree; in-game runs are the fixed
acceptance batches, profile 10 selected with `MA_COLONY_KERNEL3_LAYERED=1`;
its clean build passes all 91 CTests):

| Check | Profile 9 | Profile 10 |
| --- | --- | --- |
| Offline 8 x 6 `airflow` fill, liquid in the air rows after 1 / 20 frames | 132 / 6.5 kg per column | exactly 0 / 0 |
| `freeze-melt` owned water at the check (>380 kg) | 368.3 kg, fails | 429 kg, passes |
| `airflow` right-side gas (increase >0.05 kg) | 4.241 -> 4.052 kg, fails | 9.249 -> 9.836 kg, passes; no water crosses |
| `phase` (boil-only steam condenses as it mixes into the air) | passes | fails: 0.0497 of 0.1304 kg steam left |
| The other 27 scenarios of base batches 1, 2 and 6-9 | pass | pass |
| All 15 fixed base batches on the adopted default (`a95c134` package, 2026-11-02/03) | — | 64 of 65 scenarios pass; only `phase` fails |
| Flow-only replays of three captured 2026-10-22 worlds, restarted under each profile, 100 frames, 12 workers | 38.8 / 51.6 / 50.7 ms | 34.5 / 48.9 / 47.8 ms |
| Their liquid in gas cells holding under 50 kg of it, after 100 frames | 10.2 / 7.1 / 7.1 t (from 2.5-2.8 t) | 2.8 / 1.9 / 1.7 t |
| Offline 128 x 64 tank of collapsing 990 kg cells, flow time per frame | 1 | about 1.7-1.8 times |

`phase` places a 3 kPa row of pure steam between 1 atm air rows. The inrush
heats that row to about 356 K, and the latent heat of the first condensate
then holds it at its dew point (about 363 K at the arena's 70 kPa): gas
conducts slowly and the symmetric hot layer does not overturn. Profile 9
passes because water lifted from the pool into the steam row (0.27 kg per cell
in an offline replica) absorbs that heat; the check therefore depends on the
defect ([below](#the-phase-scenario-under-profile-10)). The violent synthetic
tank is slower because its sharper free surface keeps sloshing (about 1.8
times the pressure iterations); the captured worlds are not slower.
`mac_game_layered_control.hpp` (in `ma_mac_game_kernel_tests`) keeps freshly
placed water out of the air on profile 10 where profile 9 lifts 25.8 kg,
closes mass, U+K+G and passives, and continues checkpoint 30 exactly cold and
on 12 workers.

### The `phase` scenario under profile 10

The check and inputs are unchanged. An offline replica of the arena (same
kernel settings and water/steam/oxygen catalogue pair) reproduces the hold
and separates its causes:

- *Pressure equalizes at once.* Within the first frame oxygen fills the 3 kPa
  steam row (0.72 kg per cell); the whole arena settles at about 70 kPa. The
  row does not stay a vacuum layer.
- *Latent heat is booked to the condensing cell.* Boil-only condensation
  converts at the cell's fixed U, so each cell heats by its own condensate.
  With only 0.72 kg of oxygen and no liquid there (about 480 J/K), the first
  6 g of condensate raise the row to its dew point at 70 kPa (about 363 K) and
  condensation stops. Mass and energy stay closed.
- *Conduction is physical and slow.* Gas conducts with its own conductivity
  (oxygen 0.024 W/(m K)): about 1 W across the 37 K difference to each
  neighbouring row, so cooling the row takes hours. Eddy mixing applies only to
  moving gas. The arena's unobtanium walls do not conduct in ONI, and the
  granite floor lies under the pool at the pool's temperature. ONI's own exchange is far faster: the
  vanilla F4 rooms lose half their 70 K difference in 1.6 s against a
  physical lower bound of about 26 s ([floor](../docs/base-game-floor.md)).
- *The hot layer is buoyantly unstable but symmetric.* Hot, light gas lies
  under the colder top row, and every column is identical, so it cannot
  overturn. Raising one top-row cell's start temperature by 1 K breaks the
  symmetry: convection mixes the steam into cooler air and only 0.0017 kg of
  0.16 kg is left after 160 frames (0.11 kg unperturbed; profile 9 condenses
  all of it by frame 64 because lifted pool water absorbs the heat). A 0.01 K
  perturbation leaves 0.0061 kg.

No booking or transport defect was found. The hold is the discrete
solver's: real gas with a hot layer under cold gas overturns within seconds
at metre scale (an enormous Rayleigh number), but identical columns cannot
break the symmetry. Profile 11 below adds the missing sub-grid convection.

## Convective adjustment, profile 11 / checkpoint 31

**Adopted for new colonies (delegated decision 2026-11-03).** Profile 11 is
profile 10 plus a sub-grid convective adjustment in the layering stage
(`layering.hpp`). Across each open vertical face, where the gas of the lower
cell is less dense than the gas of the cell above (gas mass over gas volume),
the stratification is buoyantly unstable: the two cells swap equal volumes of
their gas, a share `min(0.25, 1 - exp(-dt w / h))` of the smaller gas volume,
with the free-convection speed `w = sqrt(g h (rho_up - rho_low) / rho_mean)`.
Each share carries its species at its donor's temperature (its own EOS
energy) and its mass share of passives; liquid stays. The released potential
energy is heat, as in liquid layering, and momentum follows the parcels as on
profile 8, so species, U+K+G, passives and momentum are exact. A stable or
neutral face exchanges nothing, so a still, hydrostatic atmosphere advances as
on profile 10. No random perturbation or new tunable is involved: the cap is
layering's. `MA_COLONY_KERNEL3_PROFILE10=1` (or `PROFILE9=1`) still creates
the earlier profiles; saves keep theirs.

Measured:

| Check | Profile 10 | Profile 11 |
| --- | --- | --- |
| In-game `phase` (air steam at the check, at most 1e-3 of its start) | 0.0470 of 0.1304 kg, fails | 6.6e-7 of 0.1082 kg, passes |
| In-game `freeze-melt` (>380 kg) | 425.7 kg | 428.7 kg |
| All 15 fixed base batches as the new-colony default | 64 of 65 scenarios pass (`phase` fails) | all 65 pass, `airflow` included, and again with the quiescence gate |
| Offline symmetric arena, steam left at frame 64 | 0.110 kg | 0.0244 kg (the warm top layer is then stable at its dew point) |
| `mac_game_convective_control.hpp`: hot row under cold row after 25 ticks | 60.0 K difference kept | 4.8 K; a stable box advances bit-identically to profile 10 |
| Flow-only replays of three captured worlds, interleaved A/B/A/B, 100 frames, 12 workers (ms per frame) | 40.5, 34.4 / 47.8, 48.5 / 48.7, 47.9 | 36.5, 34.9 / 50.3, 47.7 / 48.7, 47.5 |

The flow cost is unchanged within run-to-run noise. ONI-rate gas-to-gas heat
exchange (1000 times the geometric-mean conductivity, the wiki's vanilla
rule) was also prototyped: alone it left 0.053 kg in the offline arena at
frame 64, and with the adjustment 0.0023 kg, so it was not needed and is not
adopted.

The adjustment stands in for overturning the resolved flow does not produce,
so it applies only to quiescent cells: a face exchanges nothing when the
resolved speed `|J| / I` on any face of either cell exceeds its `w`. Without
this gate a Spaced Out! launch faulted: a parcel carried at the plume's speed
(hundreds of m/s) into a near-vacuum cell (4e-11 kg) booked a face kinetic
energy debt larger than the receiving cells' whole thermal energy. With it
the debt is bounded by about `w^2` per kilogram moved, which thermal energy
covers. `mac_game_plume_control.hpp` reproduces the launch cell (4e-11 kg at
95 K below 4.3e-9 kg at 119 K): beside a 1500 m/s face it is left alone
(before the gate it faulted with the in-game message), and the same column at
rest still overturns. Still atmospheres, the `phase` and `airflow` scenarios
and profile 11's controls are unaffected; the gate changes profile-11
arithmetic only where a face moves faster than `w`. Profiles 9 and 10 and the
checkpoint format are unchanged.

## Thin-gas drag, profile 12 / checkpoint 32

**Adopted for new colonies (user decision 2026-11-05).** Profile 12 is
profile 11 plus an implicit drag on thin gas in the pressure step
(`pressure_be.hpp`). A face whose inertia I (half the mass of its two cells)
is below `m_thin = 1 g` feels a drag on its momentum J at the rate
`(1/tau)(m_thin/I - 1)`, `tau = 10 ms`: zero at the threshold and growing as
the gas thins. In the backward-Euler step the face enters the pressure system
with inertia `I(1 + gamma)`, `gamma = (dt/tau)(m_thin/I - 1)`, and its new
momentum is `(J_old + pressure impulse + body impulse)/(1 + gamma)`, so the
swept volumes stay those of the solved pressures. The kinetic work the drag
removes, `gamma J^2/I`, is added to the step's existing damping heat in the
face's cells, and the momentum it removes is booked as an external load like
a wall's (the region audits book it on the face's cell). Species, U+K+G and
passives are exact.

Why: a cell's acceleration into vacuum does not depend on its density, so
milligram traces raced across vacuum caves at hundreds of m/s, and the
0.4-cell swept-volume bound and the transport subcycles followed them. On
the Spaced Out! full colony one such region cost 535-855 ms a frame. With the
drag, thin gas seeps (its velocity about proportional to the pressure
difference). Faces at or above 1 g are untouched, so air of ordinary density
advances exactly as on profile 11. Saves keep their profile;
`MA_COLONY_KERNEL3_PROFILE11=1` (or 10, 9) still creates the earlier
profiles. Measurements and the other settings tried:
[performance acceptance](../docs/performance-acceptance.md#thin-gas-drag-a-proposed-game-profile-option-2026-11-05).
`mac_game_thin_drag_control.hpp`: thin oxygen (0.2 g a cell) beside vacuum
puts 0.24 instead of 0.54 g past column 12 after 1 s in 28 instead of 79
flow intervals (0.31 g in 30 intervals since the thin-gas heating below,
which keeps that front near 300 K instead of 500 K; the check moved from
half to 0.6 of profile 11's), with closed mass and U+K+G; air of ordinary density is
bit-identical to profile 11; checkpoint 32 continues exactly cold and on 12
workers.

### Thin-gas heating, profile 12 (2026-11-07)

**User decision 2026-11-07 (option C), in place.** Thin gas holds almost no
heat. A thin cell that fills faster than it drains takes pressure work and
drag heat on almost no mass, and the next cell fills from it, so a front can
heat itself step by step. The drag makes that more likely by holding fronts
back: in the Spaced Out! acceptance scenario a 386 kPa hydrogen pocket
(heat-capacity ratio near 4.4) leaking into an empty cave took a trace to
1.6e8 K within about 30 flow intervals and the next step put 9.7e-13 kg at
1.08e9 K, past the EOS domain. Profile 12 now keeps thin gas near the
temperature of the gas it touches with two conservative steps in every flow
interval (`flow_local.hpp`):

- **Before the transport** (`limit_thin_work`): the interval's pressure
  work, body work and damping and drag heat may raise a gas-only cell lighter
  than `m_thin` at most to the hottest reading among it and its occupied
  neighbours across open faces at the start of the interval. The excess heats
  its heaviest such neighbour, which must be heavier than it.
- **After the transport** (`share_thin_heat`): each gas-only cell lighter
  than `m_thin` exchanges heat with each gas-only neighbour across an open
  face at the drag's own rate, `gamma = (dt/tau)(m_thin/m - 1)` from the
  cell's mass m. Backward Euler: a pair's temperature difference relaxes by
  `1/(1 + gamma)`, as the drag relaxes momentum; the pair conductance
  `gamma C_i C_j/(C_i + C_j)` is at most the drag's rate times the heat
  capacity of 1 g of gas. A cell and its neighbours are solved together
  (each neighbour coupled only to it), so every new temperature lies between
  the old ones.

Both visit thin cells lightest first (ties by cell index), so heat a thin
front carries passes up the mass gradient to the gas pushing it within the
same pass; the result does not depend on the worker count. Energy moves only
across open faces, so within one region; species, momenta, passives and
U+K+G are exact. Cells at or above 1 g are untouched except as neighbours,
so air of ordinary density still advances exactly as on profile 11. The
saved format and version 32 are unchanged; the arithmetic of version 32
changed in place (old saves are not a consideration before release, user
decision 2026-11-05).

Measured (Linux, 12 workers): the saved fault state (`b4a8e70`,
`spaced-out-worlds`) now runs 10 frames with its hottest reading at the
start of any flow interval 2.65e4 K (profile 11: 5.4e5 K; the exchange alone
1.03e6 K, the limit alone 1.05e5 K; without either the run stops at 1.08e9
K). The Spaced Out! full colony's flow takes 59.3 instead of 57.1 ms a frame
(three alternating runs each). `mac_game_thin_heat_control.hpp`: a 386 kPa
light gas with the game's hydrogen data leaking into an empty 30x3 box reads
at most 6.3e3 K at the end of any tick (1.06e6 K before this change, 5.1e4 K
on profile 11), with closed mass and U+K+G, and continues exactly cold and on
12 workers.

### Flooded cells and boiling, profile 12 (2026-11-09)

The 2026-11-08 Windows playtest flooded air pockets from a pool through dug
cells. Pool cells then read megapascals beside near-vacuum cells, carbon
dioxide condensed at 22 °C, and liquid carbon dioxide stayed in water for
minutes. Offline replicas of the `flood-pocket` and `flood-co2-corner`
scenarios (the game's catalogue path, profile 12 defaults) separate three
causes. All three fixes change profile 12 in place: the saved format and
version 32 are unchanged (old saves are not a consideration before release,
user decision 2026-11-05) and no setting is added.

- **Room in liquid cells** (`flow_local.hpp`, `pressure_be.hpp`). The
  pressure solve sees a partly filled, gas-free cell (void where water
  drained or cavitated) as a zero-pressure boundary and a gas bubble by its
  tangent compliance at the start of the interval. One interval could send a
  liquid cell more volume than its void or gas could take, and the transport
  also draws a cell's gas out of its top face first. In the replica a pool
  cell with 0.024 m³ of void received 0.23 m³ in 46 ms (1,208 kg, 15 MPa
  after the transport), and a cell with a 0.0095 m³ steam bubble at 0.6 kPa
  received 0.23 m³ (14 MPa). A cell's room is now the void and gas its own
  sweeps leave. The tangent model holds up to it; beyond it the cell is full
  and only its liquid's compliance applies, from the pressure the tangent
  model reaches there (zero for a void or a near-vacuum bubble), so the two
  branches meet. A liquid cell whose solve overfills its room by more than
  0.3 % of the cell re-enters the solve on the second branch, like the
  nonnegative-pressure correction (at most three passes), and the solve then
  stops its inflow within the interval. After the transport, a cell that had
  room and holds liquid is refined (the transport's momentum receipts are
  held back, so a refused interval books nothing) when it holds more than
  0.3 % of the cell beyond what it holds at twice the largest of its start
  pressure, the solved pressures of it and its open neighbours and its own
  weight, plus one atmosphere (the excess pressure times its compliance). The
  tolerance is a volume, so that one small mismatch costs no extra solve in
  any liquid: 0.26 MPa in water, 0.12 MPa in liquid carbon dioxide (below its
  0.73 MPa saturation pressure at room temperature) and a few MPa in magma,
  far above every gas's critical temperature. A first version refined every
  interval whose inflow exceeded the room; on a captured Spaced Out! world
  with magma it exhausted the attempt budget. A second settled and refined
  any overfill above a millionth of the cell and a pressure bound; that
  world then flowed 1.6 times slower than before these changes, nearly all
  of it in magma overfilled by a fraction of a percent (the tolerance swept
  in the cost notes below).
- **Volume-consistent remap** (`patch.hpp`). Faces run on local dyadic
  clocks, so a cell can receive a coarse face's whole sweep before its finer
  faces send theirs; a donor swept as if its material filled the cell then
  ships the excess density onward. A liquid-full cell with balanced volume
  fluxes gained 59 kg (5 MPa) in one interval. Each donor's material now
  occupies its swept volume (the cell volume plus its sweeps so far, floored
  at 0.4 cell for cells outside the net-volume bound), so material keeps its
  density while it moves and ends at the net volume change the solve
  predicted. A one-event advance sweeps exactly as before.
- **Boiling with displacement** (`phase.hpp`, `tick.hpp`). The phase
  relaxation runs at fixed cell volume, so a liquid boiling in a liquid-full
  cell formed only the vapour its tiny gas volume and the liquid's compression
  held at saturation: about 40 g of carbon dioxide a tick at 400 kPa, 7 g after
  the 1 s relaxation, and 37 of 70 kg were still liquid after 60 s. A pair
  liquid boiling at its cell's pressure (with the nucleation superheat where
  the cell has no gas) in a cell whose gas and void cannot hold its vapour now
  boils during the tick's flow intervals. The tick plans the heat above the
  boiling point at the cell's pressure over the latent heat (a flash),
  relaxed over the tick like the rest of the phase change and limited to 0.4
  cell of vapour per flow part. In each interval its vapour volume enters the
  pressure solve as a source, pushing the liquid out, and after the transport
  its share converts at fixed U while the heat above the boiling point at the
  interval's solved pressure lasts and the cell's gas pressure stays at or
  below saturation, so boiling into a sealed space stops at the saturation
  pressure. The relaxation at the tick's end does not evaporate those pairs
  again; the region audits book the conversions. Only a liquid body boils
  this way: a cell its liquid fills at least half (as the game shows a
  cell's liquid) whose pressure is at least the weight of its contents, so
  that a column or atmosphere holds it. Liquid in a mostly gas cell, and
  liquid whose pressure its own thin vapour or void sets, boils in place up
  to the saturation pressure at the tick's end and the flow carries the
  vapour away. Without that limit a captured Spaced Out! world planned
  7,532 m³ of vapour in 40 frames (a water body with 1 % void per cell at
  zero pressure, magma droplets in near-vacuum cells at 0.2 to 0.9 kPa): the
  vapour volume at those pressures pushed liquid into the neighbouring cells
  far faster than anything boiled, and the flow refined 44.6 times a frame
  (7.4 before these changes).

Replicas, Always simulate / Activity tiers (the regions go remote two seconds
after the dig and pause 60 s after it):

| Check | Before | After |
| --- | --- | --- |
| `flood-pocket` peak pressure, any frame | 19.9 / 26.9 MPa | 222 / 331 kPa |
| `flood-pocket` liquid carbon dioxide, peak | 11.7 / 178 g | 0 / 0 |
| `flood-pocket` steam in the arena, frame 20 | 3.0 / 4.4 g | 0.2 / 0.3 mg |
| `flood-co2-corner` liquid carbon dioxide after 60 s (of 70 kg) | 37.4 / 31.8 kg | 0 / 0 (8.4 kg after 4 s, 2.8 g after 12 s) |
| `flood-co2-corner` peak pressure, any frame | 17.3 / 25.0 MPa | 435 / 640 kPa |
| `flood-pocket` flow intervals (400 frames) | 866 / 2,133 | 811 / 165 |

Under Activity tiers the old kernel kept re-overfilling the pool on its 1 s
remote steps and paused 60 s after the dig with one cell frozen at 18.4 MPa
(1,264 kg). That is how a pool cell could read 5.51 MPa beside a 0.6 kPa cell
for many seconds: a paused region holds whatever transient it had. Activity
regions are solid-separated, so no region boundary runs inside a liquid body;
pausing a moving region is the accepted policy (motion is not activity). With
the fixes one pocket cell reaches 331 kPa for one 1 s remote step and the
pool then settles at 129 kPa. On those steps the corner cell, full of water
with its last carbon dioxide liquid, swings up to 640 kPa (below the 728 kPa
saturation pressure at 295 K) until that liquid is gone after about 20 s,
then settles at 365 kPa.

`mac_game_flood_control.hpp` (in `ma_mac_game_kernel_tests`) moves a uniform
liquid round two loops at clock levels 0 and 2 (the old remap drifts a cell
by 21.5 kg, the new one by 0) and drops a water column into a cavitated cell
and a thin-gas bubble (profile 11 peaks at 19.75 MPa, profile 12 at 175 kPa),
with closed mass and U+K+G and exact checkpoint 32 continuation cold and on
12 workers. `ma_mac_phase_change_tests --boil-only` boils liquid-full 390 K
water in a sealed box: 0.051 kg (profile 11) against 6.30 kg (profile 12) in
1 s, peak 205 kPa (the headspace approaches the 180 kPa saturation pressure;
the bound is 500 kPa), closed ledgers and cold continuation.

In game (Linux, base game, defaults and the 2026-11-08 playtest preset),
`flood-pocket` peaks at 218 kPa from the dig on with no liquid carbon
dioxide, and `flood-co2-corner` boils its 70 kg within 20 s and returns to
its column's pressure (364.6 kPa). The terrain, heat, phase, freeze-melt,
door, sublimation, falling, airflow, debris-heat, building-melt, sensor,
vent-overpressure, liquid-pump, meteor, geyser, mixed-pipe, pitcher-pump and
storage-ledger scenarios pass. `storage-ledger` found its mopped chunk by a
20 ± 0.01 kg mass; its puddle holds only water until its air arrives at
frame 6 and now boils into that vacuum towards saturation (11.6 g of steam
by the mop, 8.4 g before), so the chunk is 19.988 kg. The check now finds
the chunk by the mass that moved; its 0.5 K bound is unchanged (299.72 K).

Cost, Linux, 3,000 frames at speed 3 on the whole machine, against the
0.1.1 package (the same old source built with this revision's pipeline runs
alike, 1.680 on the industrial save):

| Run | 0.1.1 | This revision |
| --- | --- | --- |
| Large industrial save, game seconds per real second | 1.696 | 1.654 |
| Same, mean flow stage per 150-frame window | 71.3 ms | 73.9 ms |
| Same, refined flow attempts | 4,855 | 3,352 |
| New Volcanic colony (story traits V7, mixing 0), game seconds per real second | 1.523 | 2.553 |
| Same, mean flow stage per window | 97.9 ms | 41.8 ms |
| Captured Spaced Out! fault world, flow per frame (world-dump replay, 12 workers) | 148 ms | 165 ms |

The 0.1.1 Volcanic run is from an earlier run the same day on this machine;
this revision also carries main's natural sublimation scan.

Earlier versions cost more. Reading every liquid cell that had room after
the transport, changed or not, and the changed ones again for the result,
ran the industrial save at 1.550; the check now skips a cell the transport
left bitwise unchanged (it keeps its start pressure, which the bound
exceeds) and hands its readings on. Settling and refining any overfill
above a millionth of the cell under a pressure bound ran the Spaced Out!
world at 231 ms a frame (1.6 times), nearly all of it in magma overfilled
by a fraction of a percent. Overfill tolerances swept on that world and
the two flood replicas (post-transport / in-solve, share of the cell):

| Tolerance | Spaced Out! flow | `flood-pocket` peak | `flood-co2-corner` peak |
| --- | --- | --- | --- |
| a pressure bound / 10⁻⁶ | 231 ms | 137 / 142 kPa | 376 / 734 kPa |
| 1 % / 10⁻⁶ | 170 ms | 760 / 911 kPa | 376 / 913 kPa |
| 1 % / 0.1 % | 163 ms | 759 / 181 kPa | 390 / 890 kPa |
| 0.5 % / 0.1 % | 167 ms | 137 / 181 kPa | 390 / 931 kPa |
| 0.3 % / 0.1 % | 171 ms | 137 / 168 kPa | 390 / 657 kPa |
| 0.3 % / 0.3 % (adopted) | 160-168 ms | 222 / 331 kPa | 435 / 640 kPa |

A 1 % tolerance lets one water cell keep 0.76 MPa; the adopted 0.3 % keeps
every replica and in-game peak below the 728 kPa carbon dioxide saturation
pressure at room temperature. The trajectories are chaotic (a different
refinement changes everything after it), so neighbouring rows differ by a
few ms either way.

## Source energy

Gas that machines, vents, pumps, duplicants and sublimating tiles add or take
crosses a boundary the grid does not model. `MacFluidTransfer::flow_work`
(`MacFlowWork`, game kernel only; the reference kernel moves internal energy)
chooses what such a gas edit carries; the colony owner sets it on every actor
transfer and reservoir release from the colony's **Gas source energy**
(user decision 2026-11-09, compensated by default; `docs/settings.md`).
Internal moves, terrain displacement, freezing, the space sink and pipe
contents are unchanged.

- **none** (0.1.2 and earlier): an edit moves offset + cv T per kg. Gas pushed
  in then expands and does work on its room, so a strong source's cell runs
  cold (the 0.1.1 playtest's diffuser read 0.5 °C in a 22 °C room). A pump's or
  breather's cell takes in flow enthalpy but gives up only internal energy, so
  it runs warm.
- **enthalpy**: an injection adds offset + cv T + (R/M) T per kg. An extraction
  also takes the work of the cell's remaining contents expanding
  isentropically into the drawn volume: their gas moles n and heat capacity C
  (liquids included) cool by T (1 − f)^(nR/C), f the drawn share of the gas
  moles. That is about (R/M) T per kg for a small draw and never their whole
  thermal energy (the linear form took a hydrogen cell below zero thermal
  energy, since ONI's hydrogen has R/(M cv) = 1.73). The parcel keeps its
  internal energy in `internal_energy_j`, so pipes and receipts see gas at the
  cell's temperature, and books the rest as `flow_work_j`.
- **compensated**: the cell does as with enthalpy; after the batch the same
  work goes the other way across the source's room. A room is the cells
  joined to the source cell by gas through open faces, nearest first, at most
  1,024, not already in an earlier source's room of the batch (a source inside
  one joins it). Each gas-only cell takes a share in proportion to its thermal
  energy above twice its species' lowest temperature, so the room's gas warms
  or cools by one relative amount; cells holding liquid take none. A room
  gives up at most three quarters of that energy in one batch (a room of only
  freshly injected gas gives up 1 − 1/γ, 0.63 for the catalogue's hydrogen,
  so a source into an empty sealed cell ends at its own temperature). What a
  room cannot take (a wet room, or more than that) stays with the parcels as
  `flow_work_j`, as for enthalpy. Otherwise the world's energy changes exactly
  as with none.

The colony owner does not compensate per call: its machine and actor edits
use enthalpy, and once per frame, after every transfer batch, retry and
reservoir release, `MacClosedWorld::with_flow_return` sends their booked work
back across their rooms by the same rule, booked in the ledger's heat. An
enthalpy edit followed by its return gives exactly the compensated edit's
state. (A large colony's transfer batch is refused every frame by a few
sub-ulp injections and retried edit by edit, which made per-call
compensation run hundreds of times a frame.)

Every parcel's `internal_energy_j + flow_work_j` enters the transfer audit and
the owner's ledger. A 12-cell sealed tube of 300 K air fed 0.1 kg/s of 300 K
oxygen for 3 s (`mac_game_kernel_tests`) ends with source cell / mean gas
temperature 284.4 / 300.0 K with none, 302.3 / 302.3 K with enthalpy and
300.2 / 300.0 K with compensated. In the game's `playtest-diffusers` arena
(two oxygen diffusers in a sealed 296 K room) the gas spans 284-302 K with
none and 380-451 K with enthalpy (2026-11-09 A/B); compensated is measured in
[playtest 0.1.2](../docs/playtest-0.1.2.md). With none, solver arithmetic is
unchanged.

Cost: compensation reads each room cell once and records only the energy of
a cell no edit changed, booking its exact change in the audit. On a 256 × 128
grid of air, a batch of 600 edits (300 injections, 300 draws, spread over the
map) takes about 3.3-3.6 ms with none or enthalpy; compensated adds about
0.8 ms when walls split the grid into 16 × 8 rooms and about 3 ms when it is
one open area (every gas cell then belongs to some source's room). In game,
on the large industrial save (32 duplicants, 17,832 gas cells), Compensated
runs at 1.682 against 1.700 game seconds per real second for Internal energy;
the frame's return takes about 1.3 ms.

## Open acceptance work

No private-playtest-ready or performance pass is claimed. The coupled material
transport remains donor-cell plus an interface correction; the limited-linear
FCT component is not integrated. Saved vacuum-front limiting, region sleep,
registered native-body contacts, cached physical publication and the fixed
floor/reference comparisons remain open. The measured gravity/flow
entropy loss is still unresolved. These are tracked against the accepted
[design](../docs/game-kernel-spec.md) and
[private readiness checklist](../docs/private-playtest-release.md).

## Positive-volume profile

Profile3 preserves the linear liquid volume and elastic energy below half of
the saved stiffness. Above that pressure it uses continuous dyadic affine
volume segments, always positive, with elastic U integrated from `-p dV`.
Each mixed closure stays quadratic within a constitutive interval; selection
walks finite intervals, without Newton/bisection or a reference fallback.
Own readings, injection/extraction, conductivity, layering, interface energy
and pressure-dependent phase equilibrium all use that same law.

| Saved change | Effect and cost | Floor/reference acceptance |
| --- | --- | --- |
| Positive-segment liquid law | Progressively stiffens large compressions; pressure/elastic U and high-pressure boiling differ from the original linear model. No mass clipping or feature removal. Low-pressure branch is unchanged. | Resolved-reference error and affected floor metrics remain open. |
| Pressure/heat caps400/800 | Permits more work on difficult components; original tolerances and converged early exits remain. | Conservation/worker controls and three new-colony60-frame smokes pass; floor and long-run acceptance remain open. |

Synthetic controls cover 2000 mixture closures, volume/U
continuity and elastic pressure work. Fresh-world offline60-tick replays at
requested 1/12 workers give identical physical images on Oceania, Volcanea and
Terra, with global/region audits. Sinks are 4.04869e-7kg/0.149010J,
1.05911e-7kg/0.135045J and 1.04212e-7kg/0.117171J respectively. The original
1e-11kg threshold is retained; no experimental post-transport sink is used.
These initialize each own-EOS world at the original masses/J/temperatures,
recomputing its initial U. They are neither old-save continuation nor in-game
acceptance.

An immutable integrated bundle completes actual Oceania60 and Volcanea60,
requested fast 2 under the LOW lock, with120flow halves and zero faults,
rejections or skipped relaxations. Frame1 images verify core21, profile3,
positive law and saved pressure/heat caps400/800.

| World | Owner mean/p95/max ms | Observed mean/p95/max ms | Relative mass/energy error | Trace sink kg/J |
| --- | --- | --- | --- | --- |
| Oceania |220.239/275.398/290.227|278.288/485.876/826.944|5.53e-16/6.75e-16|1.69768e-7/0.113583|
| Volcanea |269.067/319.556/335.748|318.367/528.619/914.984|2.36e-16/3.90e-16|1.10156e-7/0.141078|

Clean WHOLE-lock Terra60, same native/managed bundle, changes only the new
colony profile selector. At requested fast 2:

| Metric | Profile2/core20 | Profile3/core21 |
| --- | --- | --- |
| Owner mean/p95/max ms |199.543/226.389/258.600|199.880/247.134/278.272|
| Observed mean/p95/max ms |250.684/323.738/746.792|252.286/343.988/806.033|
| Relative mass/energy error |3.75e-16/6.95e-16|3.17e-16/6.15e-16|
| Trace sink kg/J |1.19185e-7/0.128080|1.23840e-7/0.128921|

Both complete60frames,120flow halves, zero faults/rejections/skipped phase.
Owner/observed means change+0.17%/+0.64%: no clear runtime gain or cost.
The shared GPU workload remains active, so timings are diagnostic. Actual
spacing does not hold2x. All99 legacy diagnostic outputs/exits stay unchanged
apart from timings; the game control adds one PASS line. Clean Release86CTest
passes. Existing parcel control output and actual core20 twelve-tick bytes
stay identical. New core21 offline60-tick images match across 1/12 workers and
cold restore halfway through each of the three asteroid inputs.

The same clean comparison at requested normal1x gives:

| Metric | Profile2/core20 | Profile3/core21 |
| --- | --- | --- |
| Owner mean/p95/max ms |199.855/224.859/263.114|201.837/231.071/264.022|
| Observed mean/p95/max ms |249.029/323.714/806.375|244.796/344.003/787.773|
| Relative mass/energy error |5.67e-16/7.05e-16|3.66e-16/7.01e-16|
| Trace sink kg/J |1.19134e-7/0.128261|1.22207e-7/0.128524|

Both again complete60frames,120flow halves and zero faults/rejections/skipped
phase. These contended normal/fast pairs establish execution and conservation;
they do not establish isolated timing differences or sustained2x readiness.


## Exact liquid stage reuse

Interface geometry reuses its already evaluated EOS for the same inventory/U.
Density layering reuses an ordered small phase vector instead of allocating a
map per cell, preserving key and accumulation order. Solver arithmetic, saved
settings and checkpoint bytes are unchanged. Clean parent/candidate Release
builds retain all99legacy diagnostic outputs/exits apart from timings, plus
unchanged game/parcel control outputs;86CTest passes. Requested1/12-worker controls and
three captured core21 worlds at60ticks with cold restore at 30 match the parent
images exactly. Six alternating WHOLE/pin0..11 offline30-tick runs measure
102.438748 to 99.745524ms per tick (2.63%); their six terminal images are equal.

Clean WHOLE-lock Terra60 at requested fast 2, identical managed assemblies:

| Metric | Parent | Exact reuse |
| --- | --- | --- |
| Owner mean/p95/max ms |199.255/241.673/248.785|199.674/225.409/270.772|
| Observed mean/p95/max ms |250.165/344.152/745.163|245.186/343.360/787.634|
| Relative mass/energy error |2.79e-16/5.46e-16|2.44e-16/6.90e-16|
| Trace sink kg/J |1.22260e-7/0.127232|1.17472e-7/0.127668|

Both complete60frames/120halves with zero faults/rejections/skipped phase and
verified process exit. Owner mean changes+0.21%, observed mean-1.99%; no clear
whole-owner gain is established under shared GPU load. The user will arrange
a quiet GPU window later. No2x or floor acceptance follows.

A separate allocator-only diagnostic increases glibc tcache_count7 to 65535:
101.330 to 99.916ms offline, but peak RSS rises 142275 to176149KiB. Its six
terminal images match; this host setting remains unselected and has no game
performance evidence.

## Further profile3 reliability controls

The immutable original core21 bundle passes all 10 existing evaporation, door
and freeze/melt checks over139frames, with zero faults,
rejections or skipped phase. Maximum mass/energy errors 3.600e-16/1.292e-15,
trace sink2.48880e-7kg/0.263928J. LOW-lock diagnostic owner mean/p95/max is
222.231/270.107/306.018ms, observed 269.025/349.752/839.975ms. All139samples
and138intervals are present, package hashes are unchanged and cleanup verified.
This establishes phase/terrain smoke coverage, not the frozen accuracy floor.

Fresh own-EOS initializations from three older failure shapes each complete
60offline ticks, warm1worker and12workers with cold restore at 30; each pair's
full checkpoint bytes match and global/region audits close:

| Captured shape | Trace sink kg/J | Maximum absolute energy residual J |
| --- | --- | --- |
| Frame4387 energy-share fault |8.20727e-8/0.110901|0.000241|
| Frame3319 preconditioner fault |7.61204e-8/0.107834|0.000216|
| Frame1071 fault |7.53416e-8/0.102525|0.000250|

These retain original masses/T/J and explicitly recompute initial U using the
new law. They do not migrate or continue an old save, establish actual late-game
reliability, or replace the required fresh-profile colony run. Core21's frozen
F1–F5 comparison remains unmeasured; the recorded F1 failure belongs to profile18.

## Free-surface profile (profile4/core22)

A gas-free liquid cell that is only partly full sits at vacuum pressure. The
face above it, to an empty cell, received the gravity impulse of half the
cell's mass every tick with nothing to oppose it, so its momentum fell freely
while no material could cross; the body work drained the liquid's U into this
phantom kinetic energy. When the dropped oil of floor F5 landed on such
momentum, the liquids churned for about 70 s with cells at up to 3.4 times
their nominal mass. Captured Oceania showed 349 such faces at up to 281 m/s.

Profile family4 closes, for one interval, every face whose momentum predicted
from the current pressures would carry material out of an empty cell: no body
or pressure impulse acts on material that is not there. Momentum stranded on
such a face (its donor emptied during the previous transport) goes to the
boundary impulse and its kinetic energy becomes heat in the occupied
neighbour, so U+K+G, momentum and region audits close as before. New colonies
originally selected it with `MA_COLONY_KERNEL3=1 MA_COLONY_KERNEL3_SURFACE=1`;
it also saves
the adopted gas heat mixing8 (user decision 2026-10-24, floor F1 split).
Profiles2 and 3 are unchanged.

A focused control keeps a resting gas-free surface at rest (profile3 control
still shows the free fall), turns stranded momentum into heat with closed
U+K+G, and checks checkpoint22 cold round trip and 1/12-worker continuation;
the owner control creates core22 with saved mixing8 and restores it without the
selector. All99 legacy outputs/exits, the parcel output and three captured
worlds' core21 sixty-tick images (cold restore at 30) match the clean parent;
core22 images match at 1 and 12 workers. Floor results are in
[base-game-floor.md](../docs/base-game-floor.md).

## Trace removal before conduction (profile5/core23)

An offline Oceania continuation exposed tiny cells produced by transport whose
heat capacity was below the conduction solve's residual scale. Their shared
heat flux could exhaust sensible U before the end-of-tick trace sink ran.
Family5 books the existing sub-threshold trace sink before conduction as well,
refreshes the remaining cells' EOS and then solves heat. The saved threshold
stays1e-11 kg per constituent per cell; all removed mass, U+K+G, J and passives
remain in the sink. The surface selectors now create 23 with saved mixing8.
Old profiles20–22 retain their policy and checkpoint bytes.

Clean parent/candidate Linux Release builds pass 86/86CTest; all 99 legacy
diagnostic outputs/exits match (timings excluded), and the parcel output is
unchanged. A generated hot-gas front exercises the new ordering with closed
energy and both passive identities, exact cold continuation, and an unchanged
no-trace control. The old Oceania failure still reproduces under 22; a scratch
replay explicitly selecting 23 completes 60 ticks with byte-identical1/12-worker
and cold 30 endpoints. This is diagnostic replay evidence. Fresh selected-profile
game checks, the floor and long-colony acceptance remain open.

The subsequent fluid-only Oceania stress probe reaches 923 ticks, then SaltWater
falls below its supported temperature range. It first crosses its low identity
transition at tick 193, but that standalone driver omits the owner's
SaltWater-to-Brine conversion and solid byproduct. This result establishes a
limit of that diagnostic, not an integrated-owner failure; an actual game run
with conversion and freezing remains required.

## Parallel owner thread and in-place owner edits

The 12-thread owner thread was about 88% serial (2026-10-26, Terra frame-50
dump): the pressure stage's setup and publication, component layout, region
and whole-state audits, trace sinks, sharpening and transport setup, layering
and the dense/sparse conversions each ran one serial pass over the grid. Most
of these now run in the grid's fixed row blocks
([determinism](MAC_KERNEL.md#determinism)); the rules below keep every
physical value bit-identical to the serial passes, for any worker count.

- A cell that sums terms from several faces gathers them in the order of the
  original face loops (left and right x faces, then bottom and top y faces).
  Pressure assembly and the post-pressure energy pass work this way.
- Work whose order spans cells or faces (trace-sink, layering and sharpening
  bookings, interface-cluster registration, transport face scheduling) finds
  its cells and faces in row blocks, then books them serially in the original
  order.
- Sparse results are packed from per-block counts.
- Pressure components are slices of flat arrays, numbered by lowest cell.

Audit sums are the one place association changes. The tick's opening and
closing captures sum each block's cells, then its x and y faces, into the
global totals and into per-block region partials, merged in block order;
phase-conversion booking does the same, and the pressure stage's wall and
body impulse sums are block partials. Only the last bits of audit residuals
and reported impulses change, identically for every worker count.

Owner edits no longer copy the state for each batch. A game world keeps the
audit totals its producing operation summed (the tick's closing totals,
carried through heat and transfer edits). A transfer audit books only the
touched cells and the edited cells' faces against those whole-world operands.
An expiring owner (`std::move(world).with_cell_heat(...)`, likewise
`with_fluid_transfers`) whose state no other world shares is edited in place;
an edit journal records each cell, face momentum and occupied volume before
its first change, supplies the audit's original values in both forms and
undoes a refused batch. The colony owner passes its frame world this way, so
only the frame's first edit copies the published state, and its ledger closes
on the kept totals.

Earlier, before region activity tiers, measurements rejected persistent
transport scratch (1.3% slower at one thread), building dense cells on the pool
(3% slower at twelve), and frozen
shared inventories (a state copy is dominated by its cell array, not the
inventories). Checkpoint formats and bytes, saved settings and physics are
unchanged; each commit message gives its interleaved A/B measurement.

With activity tiers, the measured DLC bridge copies 40.54 MB of held mass
vectors per frame. Dense construction now takes explicit immutable snapshots
for held cells, preserving every mixture/passive/remainder field and occupied
volume. Rebuilt cells retain their original path. The mass container's tagged
representation preserves owned deep-copy semantics and uses independent const
vectors for snapshots; it does not retain old worlds. Two earlier layouts
worsened native tails and were rejected. The smaller representation improves
DLC replay mean/p95 and actual game progress while leaving the base-game
clock result essentially unchanged. See [measurements and limits](../docs/performance-acceptance.md#idle-frame-byproducts-and-combined-pacing-2026-10-29).


## Large pressure and heat components

Each connected pressure or heat component runs its own IC(0)-PCG. Small
components run side by side on the pool, but one large connected
atmosphere ran its whole solve on one thread: in the
[colony benchmark](../docs/performance-acceptance.md) a 30,826-cell room
network spent 374 ms of pressure per 0.2 s frame at twelve workers while the
pool waited. The iteration count depends on the acoustic CFL number (about
250 per half-tick for oxygen at 0.1 s, more with hydrogen), not on the
component's size, so the cost grows linearly with connected cells.

Components of at least 4,096 cells now solve one at a time across the pool
(`PressureParallelPlan`, `component_sweep_plan`). The operator gathers each
row's faces in face order. The IC(0) forward and backward sweeps run in bands
of whole grid rows, cut into column chunks and pipelined so that every row
still reads its finished lower neighbours; the backward sweep gathers the
serial scatter's updates in descending row order. Every row's arithmetic is
therefore the serial solve's. Only the dot products differ: they sum fixed
1,024-row blocks in long double and add the block sums in order. Results are
deterministic and independent of the worker count, and a component under
4,096 cells keeps the serial solve bit for bit (`ma_pressure_tests` checks
one-block plans against the serial solve under several schedules and
concurrent threads, blocked plans against each other, and refuses a plan that
breaks an IC dependency). The heat solve keeps its serial component solves:
the same plan measured only about 10% faster there, since its few
iterations are not where the heat stage spends its time.

The physical model, tolerances, iteration caps, saved settings and checkpoint
format are unchanged. Large components' trajectories differ from the serial
solve's only by the reduction order of their dot products.

## Pressure intervals after digging

The 2026-10-28 Windows playtest exposed a transport/pressure splitting defect.
The pressure solve freezes compliance and face velocities while local transport
microsteps finish. At a newly opened vacuum cell, those microsteps could keep
bringing in gas before pressure was recomputed. In the user's colony, twelve
actual native digs drove the observed neighbour from 1.67 kg to 15.36 kg,
pressure above 2 MPa, and temperature below 195 K. The same colony without the
added digs stayed near 304–308 K. The original save remains private and unchanged.

Game profile6, saved as physical checkpoint24, checks the net swept volume
before any pressure work, transport or ledger mutation:
`abs(q_right - q_left + q_top - q_bottom) <= 0.4 * cell_volume`.
The fraction is the existing numerical 0.4-cell scale. A rejected pressure
trial returns its unchanged EOS storage; its shorter replacement solves
pressure again, including work, damping and canonical momentum. Accepted
intervals cover the requested time, with proposals growing by at most two.
Both accepted and rejected pressure trials count against the attempt budget;
non-progress refuses the advance. The net-volume criterion permits rapid
through-flow with zero discrete divergence, which has a separate circulation
control using multiple transport microsteps.

Pressure time refinement applies independently to components separated by
solid walls. Each component runs the complete tick in its bounding rectangle;
explicit face barriers stay within that component. The merged image retains
global cell/face coordinates, translates membrane conflicts, and corrects
trace-sink potential energy for the rectangle's vertical origin. Every child's
physical audits and a global closed account must pass before the candidate is
returned. The attempt budget bounds each independent temporal sequence;
`report.attempts` records the deepest sequence while stage counters sum all
component work. Component stage durations can exceed enclosing elapsed wall
time when components run concurrently.

The focused Linux and Windows/Wine controls pass: a new and a nearly empty
cell agree with a fine temporal reference that does not use the new criterion;
a one-dimensional pre-reflection rarefaction retains its initial pressure
bound; mass/energy/momentum audits, budget refusal, cache retry, worker
equivalence and cold continuation pass. Controls also cover 1,024 isolated rooms
with a two-attempt temporal budget, moving regions at several worker counts,
shifted gravitational sinks, and global membrane coordinates. Both full native
suites pass 89 tests. Clean builds compare all 88 MAC targets: 87 standard
programs retain the same legacy physical outputs and exit codes, with only
three new test messages and timing differences; the long Riemann gate remains
unrun as required. Existing diagnostic failures stay recorded.

The WindowsPlayer/Proton regression uses the uploaded colony's unchanged
physical state and an identical private helper for twelve actual native digs.
Across frames 504–584, the watched cell stays at 32.338–34.561°C,
1.6462–1.6818 kg and 249.852–254.394 kPa; the earlier policy reaches
−78.636°C, 15.364 kg and 2.173 MPa. All twelve inputs match exactly.
Maximum relative mass/energy errors are 8.48e-16/3.53e-15.
The full colony retains expensive remote flows inherited from the earlier
policy: the corrected regional run averages 2.67 s per 0.2 s frame. The
global-refinement prototype took about 18–22 s; this motivated independent
components.

A fresh Spaced Out! Fast-preset run also passes 80 frames and twelve digs,
with mass/energy errors 3.41e-16/6.02e-14 and no first-flow fault. Its observed
cell stays at 19.266–22.697°C. On the development host's assigned cores it
averages 1.37 s per 0.2 s frame (p95 2.01 s); the other CPU slot was active.
These measurements establish no real-time or Windows OS performance pass.
Completed-frame32 writing and ordinary cold/menu loading pass: both loads
restore Fast and advance twelve frames to 44, with mass/energy errors
2.63e-16/3.52e-14 and verified menu return. Focused AddressSanitizer/UBSan
pressure, regional and worker/continuation controls pass. The complete
instrumented game-kernel executable stops earlier at the unchanged account
zero-add non-finite bit-pattern fixture; an isolated copy using the baseline
header also reproduces that fixture failure. Its acceptance checks are not
changed. The compact private comparison is
`artifacts/windows-temperature-flux-comparison.json`.

Checkpoint24 retains version23's field layout and identifies the new arithmetic.
The core reader preserves versions 20–23 and their original policies. Ordinary
mod loading refuses those R&D saves with instructions to start a new colony;
there is no supported save migration. A private same-state version23-to-24
copy is used only to compare the affected colony under the two policies.

### Performance follow-up

A shared executor for regions of at least 4,096 cells was measured and rejected.
On the same saved frame 32 physical state, eight advances repeated three times
were byte-identical to the accepted code, but mean advance time increased
from 1.4423 to 1.9311 s (+33.9%). The accepted implementation and private ZIP
retain the previous executor ownership. The paired evidence is
`artifacts/temperature-pooled-physical-benchmark/summary.json`.

An observational profile of the accepted code also retains all eight checkpoint
images exactly. A 2,846-cell remote magma region accounts for most work:
6,066 of 7,953 retries, all caused by the net-volume limit. Two cells contain
magma/rock-gas mixtures with substantial canonical momentum. Over eight
advances that region spends 4.66 s in pressure,3.60 s in transport,1.03 s in
EOS work and 1.29 s in audits; heat costs only 3.5 ms. The report is
`artifacts/temperature-accepted-region-profile/summary.json`.
This is a same-state physical-kernel benchmark without owner-level inputs or
conversions; it is not a whole-game performance acceptance run. No timestep,
phase-conversion rate, trace threshold or acceptance bound was changed to
obtain these measurements. Further optimization must address that local flow
without restoring the overfill defect.

## Boil-only phase rule (profile6/core25)

New colonies save the [boil-only rule](MAC_PHASE_CHANGE.md#boil-only-rule)
(user decision 2026-10-28) as physical checkpoint25: the tick's phase stage
compares saturation with the cell's total gas pressure, the sum of the Dalton
partials, instead of the vapour's partial pressure. Liquids vaporise only by
boiling, vapour below its boiling point condenses and the air holds no
humidity; a cell whose only gas is the vapour gives identical bytes. Profile6
with **Evaporation below boiling** on keeps core24 and its arithmetic. The
phase stage's entropy remains a diagnostic: it reports the mixture entropy,
which falls when unsaturated vapour condenses, while the rule's own entropy
(each gas at the total gas pressure) rises.

## Retry storms and speed profile 7

The bounded intervals of profile6 multiplied the work of a few regions. In
the fresh Spaced Out! frame-32 state (`writer32`) one 2,846-cell magma region
made 2,062 pressure attempts for 1,024 accepted intervals per 0.2 s advance.
The cause is a face, not the region: between an 8.3 kg magma cell (308,219)
and a rock-gas cell above it holding 2.7e-7 kg, the face momentum was
−4,998 kg·m/s on 4.1 kg of face inertia, −1,207 m/s. The 0.4-cell net-volume
bound then admits at most 3.3e-4 s, and fixed halving and doubling settled
on alternating 1.95e-4 s accepted and 3.9e-4 s rejected trials.

An owner replay of the same fresh colony from frame 3 with its recorded
inputs shows how that velocity forms. Interface sharpening moves about 11%
of the cell's magma per tick into its fuller neighbour, and its equal-volume
parcels leave face momentum unchanged (layering has the same rule), so the
face kept J while its inertia fell from 345 to 1.7 kg: −4.8 m/s at frame 3,
−24 at frame 10, −1,690 at frame 32. The kinetic energy came from internal
energy; the cell cooled from 1,722 K to 1,345 K. The rock-gas cell above is
not exactly empty, so the free-surface closure does not close the face, and
its trace mass carries almost nothing, so pressure cannot build to stop it.
This is a modelling defect of the stages that move material without its
momentum, not a physical flow; correcting it changes the equations and is
reported as an option rather than adopted here.

The other expensive regions of fresh colonies are physical: gas and vapour
of nanograms to grams per cell expanding into vacuum pockets at 100–700 m/s.
The per-cell bound applies to them regardless of mass. In the replayed
frame-60 state the four slowest regions accepted 150–330 intervals per
advance, limited by cells whose own or donor mass was below 1 µg in about
70% of rejections.

Three changes reduce the cost of these intervals:

- *Changed-cell EOS readings* (all profiles, no arithmetic change). After an
  accepted flow interval, a trace prune and layering, cells whose inventory
  and energy are bitwise unchanged keep their reading.
- *Cached component structure* (all profiles, no arithmetic change).
  `solve_components_cached` keeps the component layout, face order, IC(0)
  sparsity and reduction plan while the active cells and coupled faces are
  unchanged. On a one-worker pool (each region of a multi-region world) it
  runs each component's PCG with the general solve's exact arithmetic:
  single reductions below 4,096 cells, 1,024-row reductions above. A control
  compares both paths bit for bit. Setup had been about 0.8 ms of a 1.3 ms
  one-iteration solve on a 9,000-cell component.
- *Game profile7 / physical checkpoint26* (the new-colony default since
  the commit after it; existing profile6 colonies keep their profile). Profile6's equations, criterion, tolerances and caps; each
  interval is proposed from the swept volume the previous trial measured: a
  net-volume rejection retries at `0.9 / ratio` of its interval (at most
  half), other refinements halve, an accepted interval proposes 90% of the
  bound (growth two, shrink half), a remainder shorter than two proposals is
  split in halves, and the proposal carries into the second flow part.
  Accepted intervals still satisfy the same bound, and the expansion control
  checks profile7 against the resolved temporal reference.

Same-state physical benchmarks (Ryzen 9 5900X, twelve workers, the high CPU
lock, three interleaved repeats of eight 0.2 s advances, no owner inputs):

| State | Parent profile6 mean / p95 s | This series, profile6 | This series, profile7 |
| --- | --- | --- | --- |
| Fresh frame 32 (`writer32`) | 1.905 / 2.978 | 1.532 / 2.297 | 1.171 / 1.724 |
| Fresh replay frame 30 | 1.735 / 2.791 | 1.663 / 2.834 | 1.022 / 1.710 |
| Fresh replay frame 60 | 0.591 / 1.498 | 0.541 / 1.331 | 0.337 / 0.750 |

The fresh replay states come from the owner replay above (Spaced Out!
colony, recorded inputs from frame 3, profile6). Concurrent builds on the
other CPU half made these runs slower than unloaded ones; the comparison
within each row is interleaved.

Profile6 checkpoint images of the series equal the parent's in every
compared advance. Clean Release builds give the same exit codes and outputs
for all 87 run MAC programs (the long Riemann gate is not run), apart from
timing fields and the game-kernel test's new PASS lines: before rebasing onto
the boil-only rule (`94c42f6` against this series' cached structure and
profile7) and after it (`1058d0d` against the series' last commit). All 89
CTests pass on both. Profile7 also runs the boil-only rule (version26,
selector1) on the fresh frame-60 state.

The overfill regression was replayed offline through the colony owner with
the recorded inputs of the twelve-dig Proton run (the uploaded colony's
unchanged physical state, frames 505–584, twelve workers). Profile6
reproduces the recorded in-game bounds of the watched cell: 32.340–34.560°C,
1.6462–1.6818 kg and 249.85–254.39 kPa. Profile7 stays within
32.350–34.520°C, 1.6492–1.6818 kg and 249.75–254.39 kPa, while the mean
owner frame falls from 2.94 to 1.49 s. An owner replay of the fresh
Spaced Out! colony (frames 4–43) averages 1.31 s per frame with profile6 and
1.16 s with profile7; the magma face still forms there (up to −2,081 m/s).
Without interface sharpening it never forms (at most −70 m/s) and the same
replay averages 1.05 s; that is a measurement of the cause, not a setting
change. New colonies create profile7 with the boil-only rule. The owner's legacy
liquid-boundary law control (an image relabelled as owner wrapper11) was
written for profile6 and expects its water front to cross an airflow-tile
column within one frame (253.6 kg on profile6); it now names profile6 with
`MA_COLONY_KERNEL3_PROFILE6=1`, a creation selector for such controls. On
profile7 the same front stays behind the column under both owner laws (each
frame's expulsion returns the column's water); a separate control asserts
that for seven frames under the current law and six under the old law, as
ONI's airflow tiles block liquid.

Profile7 roughly halves the attempts of the storm regions;
it does not reach real time on these states. The remaining cost is the
interval count of the fast flows described above, and the per-interval cost
of transport (about 1–2 ms per interval on 5,000–9,000 cells), pressure and
readings. Options that would change the model (momentum-carrying sharpening
and layering; a net-volume bound that ignores cells and donors below a mass
floor) were measured in the orchestration report and subsequently adopted
as profile8 below.

## Carried interface momentum and speed profile 8

Game profile8 / physical checkpoint27 implements the two changes adopted on
2026-10-28. Liquid sharpening and density layering carry momentum with their
material parcels. A parcel's half mass on each donor face carries that face's
velocity to the same side of its receiving cell. Initial face velocities are
frozen across each layering pass and each sharpening correction. Receiving
faces closed by a wall or an impermeable mask book the carried momentum as a
wall impulse. Actual face kinetic-energy changes become local internal-energy
work; gravitational work and mass-carried passives retain their existing rules.
The species, passive, energy and both momentum accounts retain their original
relative bounds, including each connected region's independent audit.

The sized net swept-volume intervals also ignore a cell only when its own
mass and every neighbor flowing into it are each below 1e-6 kg (1 mg). A
massive inflow into a vacuum or trace cell still applies the bound, and a cell
or inflow exactly at 1 mg is included. This floor affects interval selection,
and removes no material. Liquid stiffness, interface sharpening, trace-sink
thresholds and all frozen gate inputs and bounds stay as adopted.

Profile8 uses the boil-only rule; integrated new colonies select profile9 below.
Earlier saved profiles keep
their arithmetic and checkpoint bytes. Development controls can request
profile6 or profile7 explicitly through `MA_COLONY_KERNEL3_PROFILE6=1` or
`MA_COLONY_KERNEL3_PROFILE7=1`.

The fresh Spaced Out! owner replay, from the unchanged frame 3 state with its
recorded inputs through frame 43, keeps the watched magma face at (308,219)
between -24.84 and +5.04 m/s instead of profile7's -2,080.92 m/s. The cell
stays at 1,720.91–1,722.52 K; the paired profile7 replay reaches
1,153.34–1,764.94 K. The same observer, original physical fields and recorded
inputs give 0.764/1.105 s mean/p95 per owner frame for profile7 and
0.583/0.707 s for profile8 (23.7% lower mean), twelve workers under the high
CPU lock while the clean test suite runs on the low half. These are single
paired development-machine observations, not real-time acceptance on the
user's PC.

The uploaded colony's twelve-dig owner replay also completes all 80 frames
(505–584), with the watched cell at 32.346–34.370°C, 1.64923–1.68182 kg
and 249.755–254.394 kPa. It averages 0.860 s per owner frame (p95 0.932 s)
under the same CPU lock. Both replay sources retain their original
equilibrium phase-rule selector to isolate the profile change; new colonies
still create the independently tested boil-only selector.

Focused controls cover both momentum components, closed receiving-face
impulses, vacuum neighbors, both gravity signs, independent sharpening
clusters, overlapping layering footprints, worker equality and connected
audits. Checkpoint27 saves and continues both phase rules exactly. Expansion
into a newly opened vacuum/trace cell retains profile7's resolved temporal
trajectory when its inflow is massive; the floor excludes all-sub-mg flows
and includes an inflow exactly at 1 mg.

## Region activity tiers, profile9

Profile9 / checkpoint28 implements the saved activity policy. Integrated
managed new-colony creation requests the explicit native `GAME_ACTIVITY` flag;
existing native callers and saved profiles keep their previous selection.
[Owner wrapper14](../docs/colony-owner.md#region-activity-tiers) pairs census,
external-input provenance and all owner stages. No sustained 3× performance
acceptance is claimed.

`with_game_activity(touched, actors)` supplies a one-frame binary input mask
and the complete current actor census. Empty masks mean all false. A wall-cell
mark reaches its fluid face neighbours. Updates retain physical fields and
time; touched regions and regions containing actors advance at the saved active
maximum step (at most 0.2 s). After the saved hold time they use remote steps
on a 1 s game-time cadence, staggered into five slots by the lowest global cell
modulo five. After the idle deadline they pause. Pristine regions pause from
creation. Neither physical motion nor passive solid heat counts as activity.

Last-activity time, pending seconds, remote deadline and actor presence are
saved per cell, not under transient region IDs. Splits inherit the held phase;
touched merges wake the joined region. Different merged clocks also wake
conservatively rather than adding histories. Waking or disabling tiers discards
pending time without catch-up. Metadata and physics publish together after a
successful advance; failed work budgets and liquid-boundary retries cannot
commit partial clocks. Worker count, camera and elapsed wall time do not enter
the cadence. Scheduling has a separate finite interval budget even when every
region is paused.

`game_activity_steps(seconds)` runs the same policy through every saved maximum
step and returns total physical seconds due per cell, including multiple remote
events in a longer request. This aggregate does not specify event order. The
owner must consume an ordered schedule or constrain and verify its frame/stage
contract before using it for nonlinear germ evolution, identity conversions,
trace removal or the space sink. An explicit booked external heat edit can
change a paused cell's internal energy without waking it; internal fluid heat,
phase change, layering and sharpening run only when its region is due.

The owner constrains adaptive frames to exactly `double(0.2f)`, containing at
most one 1 s remote event, and tests its full pipeline under that contract.
Held regions reuse their physical state/EOS; solid topology is cached until
walls change. Global reconstruction, publication and full conservation audits
remain costs. Skipping evolution alone is not a measured 3× gain.

## Bounded regional worker scheduling

Regional pressure kernels retain the original serial-per-region path and its
outer worker pool. A nonsaved execution history now allows one expensive,
dominant due region to use inner workers while the remaining regions keep
serial Ticks on a residual pool. Actual inner plus residual workers never exceed
the configured budget; an outer region job never invokes an inner parallel Tick.
Budgets of 1–2 retain original dispatch. Larger budgets allow at most four inner
workers and at most half the total budget, capped by the unchanged fixed blocks.

The policy requires two serial measurements, predicts at least 20 ms of regional
work for the requested interval, and requires a 2× lead over the next due region.
Retention uses 1.5× hysteresis. Unknown systems use the original serial fallback.
After 16 selected intervals a serial probe refreshes that system's cost; paused
systems neither execute nor age the probe. Costs scale with actual due duration,
so a held region cannot borrow a cadence or force physical work. Successful
regional audits alone commit samples; errors and invalid measurements do not
poison timing history. Coordinator infrastructure failures join and rethrow on
the caller, retaining ordered region-error handling.

Keys contain each region's global representative, cell count and membership
fingerprint. Temporary face barriers retain history; split/merged membership
starts with serial sampling. Each regional cache keeps its original serial Ticks
and at most one additional parallel Tick. History is never serialized, and cold
execution can use a different worker route while retaining identical physical
bytes. Equations, pressure caps/tolerances, reductions, timesteps, activity clocks
and all checkpoint layouts are unchanged.

The native control supplies measured costs explicitly and verifies the gates,
hysteresis/probes, budgets, coordinator failures, worker 1/4/8/12 physical bytes,
failed-work isolation, due-mask sleep/wake, topology remapping and cold equality.
The private WHOLE-lock owner replay uses the original saved Spaced Out! and
digging states with their recorded heat/terrain/actor inputs. Two matched 12-frame
repetitions compare every physical image to the original profile8 bytes. The
automatic policy selects no fresh-scene frames and six digging-scene frames per
repetition; the others conservatively sample new/changed regions. Excluding the
first two frames gives 20 samples/configuration: fresh owner mean 586.8→592.1 ms
and kernel 481.3→486.3 ms (no meaningful gain); digging owner 842.3→690.6 ms and
kernel 767.4→617.2 ms (18.0%/19.6% less). Digging process CPU increases 16.6%.
These short windows precede most of the twelve digs and omit the actual game
loop. They establish an exact execution opportunity, not sustained 3× acceptance.

The competing fresh regions explain its flat result. A fixed inner 4 experiment
reduced one region 416→322 ms, but another remained 444–450 ms; even an infinitely
fast selected region could improve that owner's mean by only about 1.6%. Parallel
pressure currently leaves the cached IC path (`solve_components_cached` routes
pools larger than one to the general assembly). Caching the parallel component
layout/sweep plan and reusing remaining `PatchTransport` scratch capacity are
follow-ups; neither is part of this scheduler.

## Exact transfer reading reuse

Transfer batches already build a species table. Their point reads and occupied
volume updates now use the same EOS directly, without constructing unused
partial-volume/specific-energy vectors or entropy diagnostics. Temperature,
pressure and gas/liquid occupied-volume arithmetic retain their original
expressions. Entering any catalogue positive-liquid extended branch falls back
to the full reading, preserving even the rejection of an absent, extremely soft
liquid whose segment cannot be represented. A focused gas-only control checks
that rejection and unchanged source checkpoint bytes.

A private whole-machine-lock comparison uses two actual 78-material saved
states, withdraws 25% or 100% of each nonzero constituent in cell/species order,
and repeats each case twice. Each batch has at most 4,096 requests. Checkpoint
bytes and every serialized parcel field (including passives, energy, both
momenta and rounding diagnostics) match after every batch: 771,324 withdrawals
per implementation, in 192 batches. These comparisons use the same Linux
compiler/platform; they are not cross-platform save equality.

| Private state / withdrawal | Requests per repeat | Parent → current mean transfer work | Reduction |
| --- | --- | --- | --- |
| Fresh Spaced Out!, 25% | 82,584 | 527 → 339 ms | 35.6% |
| Fresh Spaced Out!, 100% | 82,584 | 418 → 312 ms | 25.3% |
| Digging colony, 25% | 110,247 | 684 → 445 ms | 34.9% |
| Digging colony, 100% | 110,247 | 567 → 405 ms | 28.6% |

Timing includes actual transfer operations and their state edits, excluding the
comparison serialization/hash work. This is an isolated transfer workload,
not a whole-owner or full-game speedup. The clean parent/current 87-program
short MAC comparison retains all exit codes and numeric output, apart from
the two new PASS lines and the existing measured timing fields. Private compact
evidence is `artifacts/region-tiers-2026-10-28/transfer-summary.json` and
`mac-comparison.json`. Long gates were not run.

## Exact heat reading reuse

Heat batches of at least 32 edits now build one species table and use the same
validated EOS helper to obtain temperature. Smaller batches retain the original
full-reading cache: the table setup cost loses on one- and eight-edit calls.
The table is built only after the first edit passes the original cell, heat,
limit-argument and representable-energy checks. Edit order, the 4096-edit
cap, positive-heat ceiling, source revisions and copied/in-place rollback stay
the same. The full-reading fallback still rejects an unrepresentable extended
branch of an absent, unusually soft liquid.

Private original/candidate controls compare complete checkpoint bytes,
revisions, rounding diagnostics and exception types/messages across 6057
comparisons over 2019 cases, plus the actual fresh and digging input journals.
Cases include maximum/oversized batches, zero/subnormal heat, temperature-limit
adjacent doubles, invalid cells, walls, vacuum, partially applied refusal and
the absent-liquid fallback. Permanent controls cover the 31/32 cutoff,
single-edit equivalence with explicit revision alignment, copied/in-place
results, refusal rollback and temperature limits. This optimization reuses
existing arithmetic and does not assign a new checkpoint version. See the
[timings and scope](../docs/performance-acceptance.md#further-hot-path-evaluation).

The final heat source passes all 89 Linux Release and 89 Windows x64/Wine
CTests, 439 Python tests and the managed/game-contract suites. All 87 short
MAC programs retain their prior exit codes and existing output; only the new
heat-control PASS line is added, with the usual two timing fields masked.
The full 80-frame twelve-dig replay retains every physical and full-owner
checkpoint hash. Long gates and Windows OS acceptance are separate.

## Expiring passive edits

Germ evolution can consume a uniquely owned game state with
`std::move(world).with_passive_edits(edits)`. The copying overload keeps its
original operations. Shared states and other kernels retain that path.
The expiring path stages only touched passive vectors, in edit order, then
constructs the result and decides volume canonicalization before any state
write. Nonthrowing swaps commit the staged vectors. Allocation or validation
failure preserves the source bytes, revision, cache and state address.

Empty batches retain the shared physical state and audit cache while advancing
the revision. Every nonempty batch still invalidates cached totals, including
no-op batches. Overflow, deletion and absent-identity behavior remain unchanged.
The owner releases its previous activity snapshot only after the last history
comparison, before scheduling and biology; this makes the expiring path usable
without invalidating the history span. No clock, physics arithmetic, checkpoint
format or checkpoint bytes change for the same operations.

Two reversed 16-frame repetitions per saved scene retain complete physical and
owner bytes and canonical receipt/job records. After excluding the first two
frames of each run, full native owner means fall 31.84→27.06 ms in the base-game
capture and 68.52→54.74 ms in Spaced Out!, with 28 samples per mode. These omit
ONI, managed work and rendering; they do not establish actual 3×.
The fully instrumented ASan/UBSan dependency graph passes 977 focused controls,
42 allocation-failure points and owner lifecycle controls. The permanent
boundary suite covers public bytes/errors, aliases, true partial volumes,
revisions, cache presence/values, cold accounting and allocation rollback.
Linux Release and Windows x64/Wine each pass all 89 native tests; all 87 short
MAC programs retain existing output and exits apart from the new passive-test
PASS line. Python and managed/game-contract suites pass too. Evidence:
`artifacts/tier-integration-2026-10-28/passive-expiring/`.

## First activity-interval EOS allocation, 2026-10-30

The first flow interval moves its EOS table into the accumulated result instead
of allocating a second full-grid table and copying due rows. Later activity
intervals keep the existing due-row merge. Held rows are read through the
existing evolved-cell guard. This changes allocation and ownership only;
solver arithmetic, checkpoint bytes and versions are unchanged.

Fresh old/new builds retain every output and exit code across all 88
`ma_mac_*` test/diagnostic commands, masking only existing `ref_ms`/`prod_ms`
fields. Known failing diagnostic exits remain failures. The actual DLC and
synthetic 100k replays retain complete final checkpoints at 1/2/4/8/12 workers;
existing game-kernel controls cover multiple intervals, held/dormant regions,
wake and cold continuation.

Four interleaved WHOLE runs per implementation reduce mean captured fresh-DLC
flow from 32.5875 to 24.002 ms, with all eight final checkpoints matching bytes.
Run-to-run variation is substantial. This flow-only measurement omits future
owner inputs, projection/publication and UI; it does not establish a whole-game
3× gain. The unchanged fixed cost/recovery gates still fail. See the
[release-candidate report](../docs/release-candidate-2026-10-30.md) for the
integrated runtime checks and the remaining pressure/transport, small accepted
interval, conversion and publication work.
