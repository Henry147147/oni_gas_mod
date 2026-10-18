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
one million cells and sixteen million constituent slots. Periodicity is explicit and
needs at least two cells along a periodic axis; solids and the exterior are
impermeable; every fluid cell must stay occupied. A zero-flux step preserves every
stored value bit for bit.

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

## Material reconstruction selectors

`MacClosedSettings::reconstruction` chooses how the owner builds the parcel plan:

| Value | Name | Use |
| --- | --- | --- |
| 0 | `donor_cell` | Uniform donor-density flux through the EOS-aware overload; reference only |
| 1 | `limited_linear` | Production default |
| 2 | `swept_first_moment` | Opt-in; repairs a moving-centroid affine error |

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
