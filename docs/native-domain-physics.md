# Native domain physics

The [sealed native domain](sealed-native-domain.md) selects its physics with a
scenario number in `MAOniDomainConfig` (domain ABI 2). This document covers the
physics added after the base gas, water, conduction and gravity scenes:
filled-liquid gravity, occupied gas/liquid cells, nonlinear mixed-cell
conduction, Newtonian viscosity, starting from a native inventory, and heat
exchange with native objects and solid cells. Base single-phase conduction and
gas gravity are described in the sealed-domain document. Scenario 7 is the
[configured domain](native-configured-domain.md), which selects the same
options with explicit flags.

| Scenario | Physics | Physical checkpoint |
| --- | --- | --- |
| 0 | Mixed air (nitrogen, oxygen, helium trace) | Version 2 |
| 1 | Filled compressed water | Version 2 |
| 2 | Mixed air with gravity | Version 2 |
| 3 | Filled water with gravity, shifted energy coordinates | Version 2, EOS identity `oni-sealed-domain-liquid-gravity-v1` |
| 4 | Occupied gas/water mixture, conduction disabled | Version 2 |
| 5 | Scenario 4 with complete-EOS conduction | Version 2 |
| 6 | Scenario 5 with Newtonian viscosity | Version 3 |
| 7 | Configured physics flags and solid mask | `MADCFG01` wrapper |

Older adapters reject an unknown scenario; the 40-byte configuration is
unchanged. All accounts below use the 128-epsilon bound over actual operation
operands, with no absolute mass or heat floor, and report unrepresentable
updates as unresolved instead of forcing a minimum flux.

## Filled-liquid gravity

Scenario 3 is a completely filled compressed-water room with downward gravity
of 9.81 m/s². It uses the same worker barrier, conserved mass and canonical
momentum, elastic EOS, heat operators and native fluid exclusions as the other
scenarios.

### Energy coordinates

Small mechanical and thermal updates must not repeatedly round against water's
large material reference energy. The owner therefore stores water's energy
anomaly about 300 K, including its elastic compression energy, and keeps the
material reference as an explicit function of the authoritative mass:

`physical U = stored U + mass * (original e0 - owner e0)`

This is an affine change of energy coordinates. The owner registry and EOS use
the shifted caloric reference, mass transport carries it, and the reference
contribution follows the same constituent mass. Heat and pressure work change
stored U without adding or removing reference energy. Every published cell U,
boundary receipt and domain energy account adds the original reference back
using additive energy expansions. Managed material definitions keep their
original caloric reference, and neither formation energy nor carried
gravitational potential is discarded.

The full-U representation lost 0.128 J over 1,200 frames against a 0.00595 J
endpoint bound; with separated reference energy the same control ends with a
0.00025 J U+K+G residual under the unchanged bound.

The checkpoint holds the shifted registry and the distinct EOS identity
`oni-sealed-domain-liquid-gravity-v1`. Restore must match scenario 3, its
phase, gravity and complete identity. Stateless continuation recognizes only
the two supported native profiles and rejects unknown identities.
Format-2 checkpoints of scenarios 0–2 keep their model.

### Controls and status

```sh
python3 tools/oni_headless.py run --package /path/to/probe \
  --mode native-liquid-gravity-domain --frames 120 --save-colony \
  --timeout 1800 --output artifacts/liquid-gravity-001
```

The Unity control sends real water injection and consumption messages through
the owner, publishes their once-only callbacks and checks body impulse,
cell-centre potential and carried boundary potential. Windows uses
`run_colony_validation.ps1 -NativeDomain water-gravity`. The isolated licensed
controls `sealed-water-gravity` and `sealed-restore-water-gravity` do not launch
Unity. The native 1,200-frame (240 s) test also checks the pressure head, body
force, injection and extraction at different elevations, exact checkpoint
continuation, and initial U, pressure and temperature against the unshifted
model.

- **Native suites:** pass on Linux, ASan/UBSan, the Ubuntu 22.04 build, Windows
  binaries under Wine and MSVC on Windows (residual -0.00022 J, pressure head
  6223.113 Pa).
- **Linux Unity:** source/save, fresh-process reload and a same-process
  two-session reload pass with 120 exact continued frames each.
- **Actual Windows Unity (MSVC):** the source runs 120 frames with both water
  callbacks (relative mass/energy drift 1.5e-15/1.2e-15), and a same-process
  two-session reload passes. The [rendering repair](native-fluid-rendering.md)
  verifies the blue full-water appearance on Windows.
- **WindowsPlayer under Proton:** the source/save passes. Its reload keeps the
  exact source image at frame zero but stalls before the deferred autosave is
  admitted: the save indicator waits on its `working_pre` animation while Unity
  reports about 0.00001 s of unscaled time per frame. This is unresolved and has
  not been rerun since other Proton reloads began passing.

Linux liquid appearance, scientific hydrostatic convergence, free surfaces,
phase change and other liquids remain open.

## Occupied gas/liquid cells

Scenario 4 keeps gas and liquid constituents in the same cell, with their
actual EOS partial volumes, one mechanical pressure and one stored internal
energy. The liquid fraction comes from the completed EOS state, never from a
scenario flag or nominal density. The published liquid volume is the EOS's
compressed volume; gas partial volumes use each constituent's ideal-gas volume
at the same completed temperature and pressure. Managed readings keep these
exact phase facts, including trace headspace smaller than the rounding unit of
the liquid-volume field. Gas hover percentages use gas moles, with liquid
reported separately.

The occupied-mixture transfer policy admits gas and liquid additions and
selective removals only while the resulting EOS state stays occupied and
valid. Liquid parcels keep their pre-edit pressure and elastic energy; gas
parcels keep their actual temperature. Removal carries local face momentum,
kinetic energy and gravity potential. Each batch builds a separate candidate.
Late invalid EOS states, void creation, invalid policies and passive metadata
reject without changing the source. The single-phase entry still rejects phase
mixing, and scenarios 0–3 keep their original phase admission. Scenario 4
checkpoints and durable actor records admit both gas and water.

**Controls.** `native-mixed-domain` (`mixed`; Windows `-NativeDomain mixed`).
The reviewer reconstructs every constituent partial volume, compressed-water
volume, gas moles and mass and the complete occupied volume. The component
`ma_mac_mixed_transfer_tests` (built explicitly; reviewed by
`native/tests/review_mac_mixed_transfers.py`) covers injection into a wet cell,
selective removal of both phases, phase appearance from a filled state, traces
and carried mechanics. The native worker group `native_oni_mixed_domain` covers
pressure transport, water injection, oxygen removal, late-batch rejection and
exact restored continuation.

**Status.** Native groups pass on Linux, Windows binaries under Wine and
ASan/UBSan. A WindowsPlayer (Proton) source and two local WindowsPlayer plus two
native Linux load sessions pass with exact local continuation. The independent
complete-state comparison of all 119 stored quantities between players has a
maximum relative difference of 1.91e-9, within the 1e-8 bound. Liquid
interface rendering, free surfaces, immiscible interface geometry, phase
conversion and dissolved gases remain open.

## Mixed-cell conduction

Scenario 5 enables a conservative conduction operator for cells containing both
gas and liquid. Single-phase cells keep the existing constant-capacity graph
solve. A mixed cell uses the complete EOS at fixed constituent masses and
occupied volume: heat changes temperature, pressure, gas headspace, compressed
liquid volume and elastic energy together, so an initial intrinsic or
equilibrium heat capacity cannot stand in for the response.

Each face uses the existing series thermal resistance. Gas/liquid conductivity
is weighted by completed partial volumes (gas partial volumes proportional to
gas mole fractions). Conductance is frozen for one conductive stage. A closed
pair solves

`q = dt * G * (T_hot(U_hot - q) - T_cold(U_cold + q))`

with a bracketed Newton solve on both full-EOS endpoints. It respects each
constituent's temperature and compression ranges. If one cell's temperature is
beyond the other's compression limit, it first locates the admissible
endpoint, so a small valid exchange still proceeds. An exchange outside the
model rejects the candidate. Stiff roots use a temperature-scaled residual so
`dt*G*deltaT` cannot overflow into a false success.

One contact uses its full backward-Euler interval. Several contacts use
forward/reverse half sweeps in deterministic grid order: a first-order
contact-split scheme, not a simultaneous nonlinear graph solve. Refining time
approaches the independently integrated variable-conductivity heat ODE. Walls
and disconnected components exchange no heat. The pair's two energies are
rounded together by the closed component rule. Every constituent, trace,
passive quantity and face momentum is kept. Entropy uses `dS=dU/T` at fixed
volume and integrates both represented heat paths jointly through the full
EOS; subtracting two rounded EOS entropies erases the weak-heat signal. The
operator supplies no phase conversion, latent heat or interface dynamics.

The checkpoint stores the conduction flag and scenario separately: scenario 4
restores only with conduction disabled and scenario 5 only with it enabled.

**Controls.** The component runs as CTest `mac_mixture_conduction` and has a
saved-only reviewer using 90-digit Decimal arithmetic:

```sh
cmake --build build-oni-observer --target ma_mac_mixture_thermal_tests
build-oni-observer/ma_mac_mixture_thermal_tests > /tmp/mixture-heat.txt
python3 native/tests/review_mac_mixture_thermal.py /tmp/mixture-heat.txt
```

`native-mixed-heat-domain` (`mixed-heat`) submits two real game energy messages,
+2 MJ and -0.5 MJ. It requires exactly two committed commands, no native fluid
dispatch, the 1.5 MJ net account at the operand bound, a warmer source and
neighbour, and positive completed conduction and entropy progress. Restored
checks keep the operator without inventing new heat commands.

**Status.** Component and native groups pass on Linux, Windows binaries under
Wine, ASan/UBSan and the Ubuntu 22.04 build. LinuxPlayer and WindowsPlayer
(Proton) sources each pass two local and two cross-player load sessions with
120 exact frames per session. The strict cross-platform comparison **fails**: a
momentum difference reaches 3.22e-8 against the 1e-8 bound. That comparison
stays open; the bound is not relaxed.

## Physical viscosity

Scenario 6 adds an optional Newtonian viscosity operator for occupied gas,
liquid and gas/liquid cells. Constant dynamic shear and bulk coefficients (Pa·s)
are explicit settings, zero by default, and are development surrogates rather
than material laws. Physical viscosity is separate from compression and shock
regularization. Scenario 6 uses 0.5 Pa·s shear, 0.25 Pa·s bulk and initial
opposing horizontal velocities of 0.5 m/s to exercise the stress.

The stress is the compressible Newtonian form with a separate bulk coefficient
([NASA formulation, slide 30](https://www.grc.nasa.gov/WWW/wind/TFAWS2007/Formulation.pdf)).
Its plane-strain dissipation is

`mu*(du/dx-dv/dy)^2 + (mu/3+zeta)*(du/dx+dv/dy)^2 + mu*(du/dy+dv/dx)^2`.

Normal strains sit at cell centres and symmetric cross derivatives at interior
corners, so rigid rotation is not damped. Supported boundaries are periodic or
impermeable free-slip walls; corners touching solids supply no tangential
stress. Each strain is a rank-one relaxation with fixed face inertia,
`dJ/dt = -W*a*(a.u)`, integrated exactly over its subinterval. Symmetric
forward/reverse half sweeps compose the stresses; a periodic Fourier shear
control shows second-order time refinement of this operator, which does not
make the complete timestep second order.

Face momentum uses range arithmetic. Lost modal kinetic energy heats the
supporting cell, or the four cells sharing a shear corner. Species, traces,
passives and occupied volume are unchanged; wall reactions are explicit. The
momentum account near zero includes the cancelling stress tractions that
produced it. Stored heat has a positive entropy lower bound, `Q_stored/T_after`,
labelled as a bound. A late failure publishes neither state nor clock.

Enabled viscosity writes physical checkpoint version 3, which adds both
coefficients after the existing settings. Version 2 stays readable and
byte-identical when viscosity is off. Negative or nonfinite coefficients,
unsupported versions and a noncanonical all-zero version 3 image reject.
Restoring scenario 6 needs the exact coefficients and version 3; scenarios 0–5
keep zero coefficients and version 2.

The additive viscosity ABI 1 (`ma_oni_domain_viscosity_status`, 120 bytes)
reports stages, modes, dissipated kinetic energy separately from stored cell
heat, unresolved heat and momentum updates, wall impulse and the entropy lower
bound. It resets on a new session and publishes with its physical frame.

**Controls.** CTest `mac_physical_viscosity` and the saved-only reviewer:

```sh
cmake --build build-oni-observer --target ma_mac_viscosity_tests
build-oni-observer/ma_mac_viscosity_tests > /tmp/viscosity.txt
python3 native/tests/review_mac_viscosity.py /tmp/viscosity.txt
```

`native-viscous-domain` (`viscous`) requires completed viscous stages and
positive represented heating; no external heat command substitutes for it.

**Status.** Native and component tests pass on Linux, Windows binaries under
Wine, ASan/UBSan and the Ubuntu 22.04 build. LinuxPlayer and WindowsPlayer
(Proton) sources and all local and cross-player reloads pass: eight sessions of
120 exact frames, about 2.26 J of stored viscous heat per source. Material laws,
no-slip and moving-solid coupling, interface geometry and gameplay acceptance
remain open.

## Initial inventory admission

The additive initial-inventory ABI 1 starts a domain from a completed native
`GameDataUpdate` instead of a synthetic mixture. Only the explicit
`native-import-domain` (`import`) and `native-liquid-import-domain`
(`water-import`) controls use it. General world and geometry import is
unfinished.

`ma_oni_domain_start_from_cells` takes exactly width × height row-major
`MAOniDomainInitialCell` records of 56 bytes: size, version, five binary64
masses (nitrogen, oxygen, helium, water, carbon dioxide) and temperature.
Native binary32 masses and temperatures are promoted exactly; each native cell
contributes its actual mass to its actual constituent and absent constituents
stay zero. The legacy 256-cell limit applies. Unsupported materials or phases,
empty cells, invalid numbers or temperatures and invalid EOS states reject
before activation. Scenarios 0–3 admit single-phase cells; scenarios 4–6 also
admit occupied gas/liquid cells. All allocation, EOS initialization and
projection checks precede publication. If a later managed check fails, the
owner stops and keeps its exclusions until shutdown.

Ordering matters: the adapter captures every cell from one completed native
packet **before** installing the room's native flow flags, because flag
installation can displace fluid. It then activates the custom exclusions,
installs the flags and restores the canonical compatibility cells used by the
paired save. Those scalar writes cannot change the captured inventory.

Initial momentum is zero. Internal energy is initialized at the native
temperature in the declared reference EOS, including compressed water's
elastic energy. This does not recover native momentum or conserve energy across
a change from ONI's caloric model; production migration needs its own model and
conversion accounts. Disease, radiation, passives, vacuum and free surfaces are
outside this path.

The report records the completed-frame count, the source cells, native element
indices, the original binary32 tuples, the admitted values and a SHA-256 over
the little-endian 56-byte rows. Before the first frame the copied cells must
match every admitted constituent and temperature with zero kinetic energy and
momentum. Normal conservation, unchanged native cells, rendering, a real change
from the source, outside native heat and the save guards still apply. Paired
saves use the ordinary physical checkpoint; the source packet does not replace
it. Run both controls with the [matrix](native-validation-matrix.md#running-the-matrix)
and `--scenes import water-import`.

**Status.** Native suites pass on Linux, ASan/UBSan, the Ubuntu 22.04 build and
under Wine. Linux Unity gas (three CO2 and three oxygen cells, 4.2 kg each
constituent) and water (six cells totalling 6600 kg at 288.6–312.7 K) sources
and fresh-process reloads pass. WindowsPlayer under Proton passes both in the
[feature matrix](native-validation-matrix.md#windowsplayer-feature-matrix). An
actual Windows OS import run has not happened.

## Native object heat

Native `SimTemperatureTransfer` objects (duplicants, loose items) exchange heat
through ONI's element-chunk system, a native writer separate from cell-pair
conduction. Inside an owned region it would otherwise change compatibility
temperatures behind the physical owner.

Observer ABI 4 adds the private element-chunk contact hook. Attachment checks
the entire licensed native image, the entry fingerprint and the pinned layout.
Windows PDB inspection gives a six-argument procedure; the separately inspected
GCC specialization has four. The 16-byte cell accessor carries the padded
native cell index at offset 8. The 44-byte chunk record carries temperature,
heat capacity, conductivity and area/thickness scale at offsets 4, 8, 12 and 16.
No native STL object crosses the public ABI and no pointer is retained.

An outside or inactive contact calls the original procedure unchanged. An owned
contact reads the completed fluid state plus earlier accepted inputs, prepares a
conservative exchange, reserves an ordinary heat command and journal entry
(source `0x4d414348`), then changes the native object's temperature. It never
writes the native fluid temperature. ONI's outer chunk loop keeps object
registration, adjusters, outside ground contacts and temperature/`deltaKJ`
publication. The custom fluid advances after the original `UpdateData` physics
and before `Step` publishes game data, inside the frame fence that covers both
owners.

**Contact model.** The body's binary32 heat capacity is converted from kJ/K to
J/K. Fluid conductivity uses actual EOS constituent volumes; body and fluid
conductivities combine as a harmonic mean, times the native area/thickness and
per-contact scales. This is a declared contact law, not vanilla's heat-rate
formula. For conductance G, interval dt, body capacity C and heat q entering the
fluid, the implicit passive contact solves

`q/(dt*G) = T_body_before - q/C - T_fluid(U_before+q)`.

Single-phase constant-capacity models solve analytically; mixtures use the
complete EOS and a bracketed solve. Both directions keep temperature ordering
and a nonnegative entropy lower bound. The body's candidate temperature is
rounded toward its old binary32 value, and only the represented body energy
loss enters the fluid. A contact too small for either endpoint changes neither
and counts as unresolved. Each exchange checks the fluid/body energy account at
the operand bound.

Capability `ma_oni_domain_chunk_heat_*`, version 1, reports a 112-byte status:
session/frame, contacts, transfers, unresolved contacts, iterations,
represented body loss, stored fluid heat, balance residual and bound, entropy
lower bound and requested absolute heat. Pending contacts appear only when
their frame commits. The journal entries give exact fluid replay without
predicting object AI or physiology.

**Status.** CTest `mac_native_body_heat` and the native groups pass on Linux,
Windows binaries under Wine, the Ubuntu 22.04 build and ASan/UBSan. Licensed
SimDLL worker controls (`MA_VERIFY_CHUNK_HEAT=1` in the
[worker harness](windows-validation.md#isolated-native-worker-harness)) pass gas
and water on Linux, under Wine and on an actual Windows host (61 exact replays,
56 contacts each). Native body registration leaves its output accumulator
uninitialized until a zero-time output-list update, so the harness runs that
update before the first positive contact. A Linux control with the owned room
in the second of two active native regions also passes, showing the contact
adds no second fluid clock. In Unity, both players' live-breathing sources and
every local and cross-player two-session reload pass with 120 contact-bearing
exact frames per session. ONI's game thread does not wait for the native
worker, so the worker can publish frame N+1 between two status reads in one
managed capture. The per-frame check verifies that next-frame read in full and
leaves it for the next capture (`AdvancedWorkerObservations`); any other
mismatch still fails.
Actual Windows OS, building heat, a complete conductivity model,
phase-changing objects and conserved metabolism remain open.

## Native solid-cell heat

ONI owns a solid cell and its save data; the custom engine owns the adjacent
fluid. Their contact must exchange one represented heat quantity per native
frame instead of letting a compatibility cell receive it.

Before positive time the adapter atomically registers supported solid faces
with capability `ma_oni_domain_solid_heat_*`, version 1. Each 48-byte record
binds a drained completed source session/frame, public cell, native material
index, mass, specific heat, conductivity and native insulation byte. Each cell
must be outside the fluid rectangle and share a face with it; zero or
insulating solids need no registered contact. Registration rejects duplicates,
nonadjacent cells, invalid thermal facts and pending fluid work. Every contact
rechecks material, exact mass bits and insulation; a change faults the owner
before either endpoint changes.

The exact-build `UpdateTemperature` hook handles a pair of one owned fluid cell
and one registered solid. Layouts are inspected per platform: `SimData.cells`
and `updatedCells` pointers are at offsets 32 and 40; CellSOA element,
temperature, mass and insulation pointers are at 0/24/48/96 on Linux and
8/40/72/136 on Windows. The complete library hash and call-site signatures are
required.

Native pair updates accumulate in `updatedCells`. The bridge reads the current
solid endpoint there and writes only its represented result, preserving earlier
native contributions and the original temperature grid. The frame interval is
shared with native job threads (a thread-local interval was invisible to them
and recorded no contacts). A face is handled at most once per completed fluid
frame, across multiple active regions. Outside pairs keep native dispatch.

The [object contact solver](#native-object-heat) serves this boundary too.
Solid capacity is the product of the native binary32 mass and specific heat,
kept as binary64 without binary32 rounding. Conductivity is the harmonic mean of
the full-EOS fluid value and the solid's value after applying each endpoint's
squared native insulation fraction. The 112-byte solid status is separate from
the object status; accepted commands use source `0x4d415348` (`MASH`) in the
journal.

Two read-only 64-byte observations support diagnosis without changing either
endpoint or the checkpoint. `ma_oni_domain_solid_heat_endpoint` reports the last
completed contact's native old-grid temperature, accumulated before/after
temperatures, interval, cell identities and contact/owner frames, published
atomically with the fluid heat account. `ma_oni_domain_solid_cell_state` reads
the native old and updated arrays after each worker sub-update, tagged with the
completed fluid frame and native ticket; stale observations reject. The
licensed worker control is `MA_VERIFY_SOLID_HEAT=1` (solid at 400 K);
`MA_NATIVE_SOLID_TEMPERATURE_BITS` selects an exact hexadecimal binary32
initial temperature instead. Neither changes gameplay settings.

**Control.** `native-solid-heat-domain` (`solid-heat`; Windows
`-NativeDomain solid-heat`) places one 10 kg, 400 K granite face beside mixed
air, with an unobtanium backer. Paired-save admission accepts that contact and
checks its mass, phase, temperature range and backer before native loading; the
adapter reconstructs and checks the binding before restored time. Arena
placement reserves a three-cell object-free margin around every changed fixture
cell (building the arena once uprooted flora and dropped a seed into the
chamber).

**Status.** Native groups pass on Linux, Windows binaries under Wine, the Ubuntu
22.04 build and ASan/UBSan. Licensed worker controls pass gas and water on
Linux, under Wine and on an actual Windows host (61 exact replays and 56
contacts each; gas ends at 399.9931640625 K, water at 399.8564453125 K), and at
the failing colony's 369.62255859375 K initial temperature on Linux and under
Wine, with both native arrays matching the published temperature. See
[the Windows worker harness](windows-validation.md#isolated-native-worker-harness).

**Unity publication repair.** A fresh Linux reproduction confirmed that the
completed native old and updated arrays already accounted for the full heat
exchange. The exported `GameDataUpdate` described an older frame than the
separately read fluid journal. Two inspected native procedures now associate
each export with an immutable [completed publication lease](native-domain-publication.md).
The heat control reads that source's cells, journal and native observations
together; current-owner command reservations are unchanged. Completion drains
zero-time exports and requires the final source frame, clock, native ticket
and every fluid cell to match the paused owner. No endpoint or tolerance is
changed.

Linux Unity source/save and fresh-process reload now pass: 121 source contacts
transfer 87.51526116611785 J, followed by 120 restored contacts transferring
86.79199454491027 J. Both published-temperature residuals are exactly zero at
the unchanged approximately 1.691e-7 J endpoint bounds. The reload verifies the
initial saved image, paused frame-zero resave and 120 exact accepted-input
continuations. The package uses a frozen Ubuntu 22.04 native build with all
38 registered tests passing. WindowsPlayer and actual Windows OS acceptance
for this repair remain separate. Private reports are under
`local/solid-heat-native-debug/unity-publication-matrix-v1/`.

Whole-colony heat, moving or changing solids, building thermal components and
phase-changing boundaries remain open.
