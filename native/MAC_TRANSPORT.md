# MAC transport and material reconstruction

This is the remap half of the [`MacClosedWorld`](MAC_SOLVER.md) pressure/transport
stage. After the pressure step has moved material cells, transport returns material
to the fixed grid. It moves cell inventories (constituent masses, stored U, passive
quantities) as positive primal parcels, and moves normal face momentum J and kinetic
energy K with compatible dual fluxes. It computes neither pressure nor the swept
volumes; the [pressure step](MAC_PRESSURE.md) supplies them. Momentum corrections
beyond donor-cell transport are described in [momentum reconstruction](MAC_MOMENTUM.md).

Code: `mac_transport.hpp`/`.cpp`, `mac_parcel.cpp`, `mac_reconstruction.hpp`/`.cpp`.

## Primal and dual transport

For each velocity component, an interior face has inertia `(m_left + m_right)/2`; a
fixed normal face has its one material half. Each cell therefore contributes its whole
mass to each component's dual partition. On the regular grid, the component-normal
dual flux through a cell centre is the average of its two parallel primal mass fluxes.
A transverse dual edge carries half of each adjacent primal flux, with missing or
solid contributions zero. These sums give the same dual mass change as the updated
adjacent primal half masses, so no separate geometric-corner normalization is needed.

A primal donor packet uses one positive mass fraction for every constituent, stored
U and passive quantity. Dual donor packets use their component's derived fluxes to
carry J and K. Outgoing primal and dual mass must each fit the donor; otherwise the
step rejects and the owner retries a smaller dt. The target inertia comes from the
updated primal masses, and its mismatch with transported dual mass is checked at 128
epsilons of the actual operands.

For target velocity `u`, upwind mixing heat is the nonnegative
`Σ packet_mass (packet_velocity − u)² / 2`, including the retained packet. A fixed
normal face removes its provisional resolved K into a separate wall heat and wall
impulse account. Heat goes to adjacent material cells by their new half-mass shares.
Stored-U rounding is explicit in the energy residual and the unresolved-heat count.
This is finite conservation, not an entropy proof for arbitrary caller fluxes.

Inputs and candidates own their arrays; failure changes no input. Grids are limited to
one million cells. Constituent accumulation has no species-count/product cap.
Periodicity is explicit and
needs at least two cells along a periodic axis; solids and the exterior are
impermeable. Exact vacuum has zero constituent, U and passive inventory; a face
with zero inertia must have zero momentum. Gas can enter an empty cell without
seed mass. The cell-average path still lacks resolved occupied-volume geometry
and **fails the vacuum-front gate**; see [empty-cell support](MAC_VACUUM.md).
A zero-flux step preserves every stored value bit for bit.

There are two flux-driven entry points. The original overload requires gas headspace
or a filled constant-density liquid. The EOS-aware overload takes the material source
volumes from a pressure step and the grid's nominal volume as target, and validates
both through `FluidThermodynamics`, including compressed liquid and stored elastic
energy. It applies no legacy caloric-temperature or constant-density test.

Reconstructing dual fluxes from primal mass fluxes is an established staggered method;
see [Brunel, Herbin and Latché](https://arxiv.org/abs/2209.06474) and
[Dakin and Jourdren](https://www.numdam.org/articles/10.1016/j.crma.2015.11.008/).
The regular-MAC formulas above are this implementation's derivation and are verified
by its own controls.

## Parcel transport

`advance_mac_parcel_transport` takes an explicit `MacParcelPlan`: outgoing
constituent/U/passive parcels, retained inventories, source and retained volumes and
one common limiter per donor. It checks each donor's constituent, U, passive and
volume decomposition at 128 epsilons of its operands, plus target volume, EOS and face
geometry. The actual parcel masses define the primal fluxes that drive dual J/K
transport and the nonnegative mixing heat. The API validates a plan; it does not
certify how a caller built it. Parcels whose amounts would lose binary64 range carry a
normalized prototype and scale ([range arithmetic](MAC_RANGE.md#primary-parcels)).

## Prepared admission and reusable candidates

Each transport call prepares the validated topology, source masses and momenta,
actual primal/dual fluxes, transported constituent/passive inventories and internal
energy once. These inputs do not depend on the swept-momentum correction fraction.
Source temperature and entropy come from the same EOS reading that validates the
source. The preparation lives only for that call, so a different source, plan,
topology or EOS profile always receives fresh validation.

The first trial moves the prepared inventory into its working buffer. A limiter
search allocates a second inventory only after the full correction has failed and
the zero-correction candidate has passed. Trials restore the original transported U,
recompute J and compatible work, and swap accepted/working buffers. They reuse the
constituent and passive arrays without repeating material transport. No rejected
candidate is published. Each completed trial still receives a fresh EOS reading;
that same reading supplies its entropy check. The original storage, constituent,
momentum, energy, EOS and entropy bounds and the 53-trial search are unchanged.

This is reuse within one remap, not a persistent block cache. Reconstruction and
diagnostic workspaces still allocate, and primary inventory vectors remain dense.
Continuation and nine saved component captures agree byte for byte with the prior
implementation. The opt-in 2/4/3/2 strong-flow sequence now produces all nine profiles
within its 180-second bound, but still fails separation accuracy; see
[measured progress](../docs/production-timestep-progress.md#prepared-remap-admission).

## Sparse constituent workspace

Transport accumulates mass in contiguous compressed rows containing only exact
nonzero source species and their incoming donor union. Source and target IDs stay
in registry order; a bounded five-way merge covers the retained source and four
possible incoming primal faces. An absent species has no accumulator. There is no
trace threshold, fixed species limit, hash traversal or change in the contribution
order. Full-range parcel amounts retain the original accumulation and storage rules.

The old cell-count-times-registry-size and parcel-count-times-registry-size guards
are removed. Parcel coverage remains bounded by actual grid faces and four outflows
per donor. Primary cell inventories, parcel prototypes and reconstruction fields
still use dense registry vectors; reusable sparse production blocks remain open.
Donor validation creates one conservation account at a time for present species;
it also checks any positive species introduced by a retained or outgoing parcel,
even when that species was absent in the donor. Exact-zero accounts need no work.

`MacTransportReport::constituent_accumulators` reports the actual workspace slots.
The capacity test uses 4,097 registered species and 4,096 cells, with two positive
species per cell: 8,192 accumulators replace 16,781,312 dense slots. In the Linux
build these require 576 KiB of accumulator objects rather than about 1.125 GiB,
following the [compact accumulator](MAC_RANGE.md#additive-expansions-and-accounts)
change (72 bytes per object, previously 128). This is a
workspace measurement, not the total solver or additional in-game memory budget.

`ma_mac_sparse_constituent_tests` also admits actual reconstructed EOS parcels beyond
the old product cap, every registered species simultaneously positive, analytic
two-axis trace transfers with distinct high-index donor identities, and a mapped-EOS
parcel identity. Invented positive traces reject atomically in both retained and
outgoing parcels. Existing primary-parcel controls retain quarter-denormal
recombination. Uniform identity/capacity controls do not satisfy active-cell timing
acceptance.

## Material reconstruction selectors

`MacClosedSettings::reconstruction` chooses how the owner builds the parcel plan:

| Value | Name | Use |
| --- | --- | --- |
| 0 | `donor_cell` | Uniform donor-density flux through the EOS-aware overload; reference only |
| 1 | `limited_linear` | Production default |
| 2 | `swept_first_moment` | Opt-in; repairs a moving-centroid affine error |
| 3 | `interface_plic` | Opt-in sharp gas/liquid interfaces ([below](#sharp-gasliquid-interfaces-selector-3)) |

### Sharp gas/liquid interfaces (selector 3)

A donor that holds both gas and liquid places its liquid behind a straight line, a
piecewise-linear interface (PLIC).
- **Line:** its normal is Youngs' 3×3 gradient of the neighbouring EOS liquid
  fractions (each from the liquid's own volume, never V − V_gas). Its offset
  encloses the donor's liquid fraction, found by bisection.
- **Strip parcels:** each outgoing strip, the band of its face's swept width, carries
  liquid and gas in the proportion the line gives it, at the donor's liquid density,
  headspace density and EOS temperature. A strip's liquid area is integrated
  exactly as a clamped linear length along the coordinate with the smaller normal
  component, so a strip wholly beyond the line holds exactly no liquid. The strips
  are then filled in order (left, right, bottom, top) so that each holds exactly its
  swept width and together they take no more of a phase than the donor holds.
- **Single-phase donors:** these keep limited-linear slopes away from an interface.
  Beside an interface or a change in liquid presence they keep donor order, so no
  slope exports a phase the donor does not hold.
- **Fallback:** a split that fails EOS admission falls back to the donor's own
  composition, which is donor order.

Conservation (retained = source − parcels), EOS admission and the compatible dual
momentum transport are unchanged. Uniform motion therefore stays uniform across the
density jump.

`ma_mac_interface_tests` (CTest `mac_sharp_interface_translation`) translates a 6×6
water square through air at (0.3, 0.2) m/s in a 16×16 periodic box for 3.6 cells:

| Selector | Partly filled cells | Liquid in cells under 1% full | Liquid mass error | Velocity deviation |
| --- | ---: | ---: | ---: | ---: |
| 3 `interface_plic` | 35 | 0.027% | 2e-18 | 8e-11 |
| 3 `interface_plic`, production kernel | 25 | 1.5e-17% | 2e-17 | 2e-10 |
| 1 `limited_linear` | 229 | 0.53% | 8e-18 | 8e-11 |

The normal is scaled by its larger component, so axis-aligned normals are exactly
±1. It does not use `std::hypot`, which is not correctly rounded across C libraries
and imports `GLIBC_2.35`.

**Trace phases.** Clipped polygon areas carry absolute roundoff near 1e-17 of the
cell, and strips near rest are only 1e-14 of a cell wide. Dividing such areas by a
trace phase's fraction once made strips claim more of the phase than the donor held.
The split then failed admission, and its donor-order fallback lifted trace water
through the air, about 1e-13 of it per interval until it reached subnormal amounts
the reference EOS rejects. Exact strip integration and the inventory-limited filling
above remove that path. Still water (the [M1 gate](MAC_GATES.md)) keeps exactly no
liquid above its surface cell, and the translation square above keeps 25 partly filled
cells in production (35 in the reference) instead of 85–91.

An exact representation of the shifted square has 24 partly filled cells. The
remainder comes from diagonal motion: a donor's corner liquid should reach its
diagonal neighbour, but face parcels can only send it to face neighbours.
Directionally split sweeps would remove this.

A diagnostic dam break (48×24 cells of 12.5 mm; water column a = 0.1 m by 2a under
air at 1 atm, 300 K; defaults otherwise) reaches T = t√(2g/a) = 3 with both selectors,
conserving liquid mass to 1e-16. Selector 3 includes its
[sharp-interface gravity weight](MAC_PRESSURE.md#gravity-with-sharp-interfaces):

| At T = 3.08 | Kernel | Front Z = x/a | Partly filled cells | Liquid reach | Mist | Wall time |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| `interface_plic` | reference | 3.625 | 46 | 3.75 a | 0.014% | 285 s |
| `interface_plic` | production | 3.625 | 53 | 3.75 a | 0.027% | 1 s |
| `limited_linear` | reference | 4.0 | 435 | 6 a (whole tank) | 0.18% | 2,334 s |
| `limited_linear` | production | 4.0 | 435 | 6 a (whole tank) | 0.18% | 13 s |

Wall times are single-threaded on a shared machine, so they indicate scale only.
Before the [gravity reconciliation credit](MAC_KERNEL.md#gravity-reconciliation-entropy),
the production kernel failed this run's first interval with "Production interval
entropy audit failed" for both selectors.

The production kernel runs this selector with the same line, strips and fallback.
Its phase fractions and energies come from each cell's post-pressure EOS state.
In the translation test it matches the reference to 0.5% liquid placement (L1),
8e-4 cells of centroid and 2e-10 K. Free-surface gravity intervals pass its entropy
audit through the gravity reconciliation credit
([kernel](MAC_KERNEL.md#gravity-reconciliation-entropy)).

One liquid region is placed per donor, so immiscible liquids are not separated from
each other. Gas/vacuum fronts are not covered. Still water, the dam break and the standing
wave pass their gates ([MAC_GATES.md](MAC_GATES.md)).

### Limited linear reconstruction (selector 1)

For each donor, neighbour inventories are expressed at the donor's material volume.
Centred x/y differences give slopes for every constituent mass, actual stored U and
sparse passive quantity, plus a positive thermal-plus-elastic proxy. One common
coefficient scales all slopes. This keeps linear relations between constituents and U
at constant pressure and temperature, including composition and gas/liquid volume
constraints. No mass fraction is renormalized and there is no trace threshold.

An outgoing face samples the swept slab centre. Its normal coordinate is
`sign(q) (1 − |q|/V) / 2`; its transverse coordinate is minus half the donor's mean
transverse volume sweep divided by V. The four corners, the outgoing sample points
and the retained-volume centroid all constrain the common limiter to the neighbouring
inventory bounds. Positive fields must also keep at least half their cell average at
every evaluated point (a relative slope constraint, never an absolute floor). The
outgoing volume fraction is at most 0.45; the owner uses 0.4.

The reconstructed vector times `|q|/V` is an owned parcel; the retained inventory is
the original minus all outgoing parcels. The prototype, the normalized parcel and the
retained state must be EOS-admissible; if not, all slopes are halved together, down
to exact donor order. An unrepresentable stencil gradient flattens the whole donor and
is counted. An unrepresentable positive parcel constituent or passive rejects the
step. No individual mass, temperature or passive is clipped.

Bounded multidimensional reconstruction is discussed by
[Suresh (2000)](https://epubs.siam.org/doi/abs/10.1137/S1064827599360443); the shared
multicomponent limiter, slab interpretation and EOS backtracking here are this
implementation's choices.

### Swept first-moment reconstruction (selector 2)

The pressure step changes material volumes before transport returns material to the
grid. Selector 1 uses centred differences in cell-index coordinates and a slab
coordinate normalized by material volume; those coordinates do not describe the moved
material centroids. A constant-density nitrogen control with normal face
displacements `[0, 1, 4, 5, 7, 4, 0] / 64` and affine passive concentration `2 + x/8`
shows it: the selected cell has volume 65/64, centre 2.5703125 and outgoing volume
5/64, so the swept slab centre is 3.0390625 and the exact passive amount is
0.1859283447265625. Selector 1 gives 0.18598116361177885, an error of 5.28e-5 against
an operand allowance of 1.06e-14, with limiter one. Selector 2 reproduces such affine
fields exactly.

Coordinates use grid spacings as the length unit, with fixed cell volume V0. A face's
signed normal displacement is `q/V0`; its transverse displacement is the mean of the
adjacent cells' mean transverse sweeps divided by V0. Both face owners use the same
time-centred face position: the original position plus half the displacement. Each
cell must close its volume, `V = V0 + qR − qL + qT − qB`, and its moved centroid
offset is

```
D = sum_faces(sign * q * (original_relative_face_position + displacement/2)) / V
```

This is a discrete first-moment model without an inferred interface polygon. It gives
the actual interval centre for one-dimensional deformation and `D = (qx, qy)/V0` for
uniform translation. Inverse-distance least squares uses the moved neighbour-centre
offsets; the stencil has the four cardinal neighbours, plus diagonals when both
two-step paths pass through fluid, and never crosses walls. The same common limiter,
EOS checks, backtracking, flattening and rejection rules as selector 1 apply, with
normal-face extents, outgoing slab positions and the retained centroid as constraints.

Selector 2 does not fix the strong-flow collision error (collision density ratio
0.8565 against 0.85; see [strong flow](MAC_STRONG_FLOW.md)). It is required by the
most accurate opt-in combination.

## Verification

**Transport (`ma_mac_transport_tests`, seven groups).** Exact kinetic mixing, a rigid
solid corner, variable-density constant velocity, four atomic rejections, 2-D
shear/passive refinement, fixed-N timestep differences and transposed-corner
symmetry. The periodic unit-square shear at T = 0.125 s, `u_x = 0.5 m/s` and
Courant 0.2:

| Grid | Velocity L1 (m/s) | Passive concentration L1 |
| --- | --- | --- |
| 16 × 16 | 0.0102993337394495 | 0.00823946699155952 |
| 32 × 32 | 0.00522505284321382 | 0.00418004227457108 |
| 64 × 64 | 0.00263618086061237 | 0.00210894468848989 |

Both successive ratios (about 0.507 and 0.505) are below the 0.7 joint bound. On the
fixed 16 × 16 grid, y-momentum differences between Courants 0.2/0.1 and 0.1/0.05
have ratio 0.532, below 0.7. These are first-order trends for smooth periodic flow,
not discontinuity or free-surface accuracy. The exact two-stream control resolves
`J = {0.75, −0.75}` and converts 0.4375 J to heat.

**Limited linear reconstruction (`ma_mac_reconstruction_tests`, five groups).**
Gas-composition and gas/liquid contacts keep constant pressure and temperature; a cold
compressed-liquid control (300.001 K against a 300 K profile bound) exercises EOS
slope backtracking; zero flux is an exact identity; eight malformed plans, an invalid
selector and an unrepresentable positive passive reject; oblique scalar advection
refines with each ratio below 0.45:

| Grid | Selector 1 L1 | Ratio | Selector 2 L1 |
| --- | --- | --- | --- |
| 16 × 16 | 0.003785217850 | — | 0.003407536314 |
| 32 × 32 | 0.001177162631 | 0.311 | 0.001021121138 |
| 64 × 64 | 0.000317154091 | 0.269 | 0.0002517850251 |

**Swept reconstruction (`ma_mac_swept_reconstruction_tests`, nine groups).** Eight
affine cases across both axes, signs and length scales agree exactly with their
binary64 expected amounts; four oblique translation/shear cases; zero-flux/wall
identity; invalid geometry and selector rejection; positive passive underflow
rejection. The owner runs selector 2 with every gas/liquid, heat and compression
setting and continues exactly from checkpoints; selector-2 images read on the other
platform without changing a byte.

Passing these controls does not establish shock, vacuum or interface accuracy.

## Reproduce

```sh
cmake --build build --config Release --target ma_mac_transport_tests ma_mac_reconstruction_tests ma_mac_swept_reconstruction_tests
build/ma_mac_transport_tests > transport.stdout
python3 native/tests/review_mac_transport.py transport.stdout
build/ma_mac_reconstruction_tests > reconstruction.stdout
python3 native/tests/review_mac_reconstruction.py reconstruction.stdout
build/ma_mac_swept_reconstruction_tests > swept.stdout
python3 native/tests/review_mac_swept_reconstruction.py swept.stdout
```

`review_mac_transport.py` rebuilds primal packets, dual fluxes, inertia, J, K,
variance heat and wall reaction with exact fractions and checks the shear profiles
against an 80-digit Fourier recurrence. `review_mac_reconstruction.py` checks the
decomposition, common limiter, parcels, retained and target inventories and EOS
contacts with exact fractions and 80-digit EOS arithmetic.
`review_mac_swept_controls.py` reviews the five original reconstruction controls
when the reconstruction function in that test is locally switched to the swept
selector; it requires a uniformly translated periodic mesh. The swept test also
accepts `--write-checkpoint <file>` and `--read-checkpoint <file>` for cross-platform
image checks.
