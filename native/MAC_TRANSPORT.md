# Retained MAC momentum transport

The production solver remains unfinished. This separate 2-D transport component
retains normal face momentum and cell constituent/U/passive inventories. It
accepts integrated primal mass fluxes; it calculates neither pressure nor those
fluxes. No world/C ABI/game dispatch selects it. The older Cartesian/hybrid
remap and original integrated vacuum/performance failures are unchanged.

Reconstructing dual fluxes from primal mass fluxes is an established staggered
method; see [Brunel, Herbin and Latché](https://arxiv.org/abs/2209.06474) and
[Dakin and Jourdren](https://www.numdam.org/articles/10.1016/j.crma.2015.11.008/)
for staggered transport and energy treatments. The regular-MAC formulas below
are this implementation's derivation and require separate verification. Those
papers do not validate this code, its rigid-corner treatment or a production
pressure/free-surface composition.

For each velocity component, an interior face has inertia `(m_left+m_right)/2`;
a fixed normal face has its one material half. Thus each cell contributes its
whole mass to each component's dual partition. On a regular Cartesian grid,
the component-normal dual flux through a cell centre is the average of its two
parallel primal mass fluxes. A transverse dual edge carries half of each
adjacent primal flux, with missing/solid contributions zero. These sums give
the same dual mass change as the updated adjacent primal half masses. There is
no separate geometric-corner mass normalization or half-mass rebalance.

Primal donor packets share one positive mass fraction for every constituent,
stored U and passive quantity. Dual donor packets use their component's derived
fluxes to carry canonical J and kinetic energy. Explicit outgoing primal and
dual mass must each fit their donor; a caller must choose/retry its timestep.
The target inertia derives from the updated primal masses. Its local mismatch
with transported dual mass is checked at 128 binary64 epsilons of actual local
operands, without a unit floor.

For target velocity `u`, upwind mixing heat is the positive sum
`sum(packet_mass*(packet_velocity-u)^2)/2`, including the retained packet. A
fixed normal face also removes its provisional resolved K, with a separate wall
impulse/heat account. Heat is distributed to adjacent material cells by their
new half-mass shares. Scalar U rounding remains explicit through the final
energy residual/bound and unresolved-heat count. This is finite conservation,
not exact arithmetic; the operator does not establish entropy for arbitrary
caller-supplied fluxes.

Inputs/candidates own their arrays; failure changes no input. Width/height and
products are bounded to one million cells and sixteen million constituent
slots. The original overload requires gas headspace or filled constant-density
liquid. The EOS-aware overload validates source material volumes and nominal
target volumes with an explicit `FluidThermodynamics` profile, including
compressed liquid and stored elastic energy. All fluid cells must remain
occupied; solids/exterior faces are impermeable. Periodicity is
explicit, with at least two cells along a periodic axis. Empty cells, cut/free
surfaces, gravity, pressure work, diffusion, viscosity, conduction, phase change,
persistence and game/actor ownership remain separate required work.

The explicit `ma_mac_transport` and `ma_mac_transport_tests` targets are excluded
from default builds and CTest. New translation units use warnings as errors and
strict/no-contraction FP settings; upstream core flags are unchanged. The
seven controls cover exact kinetic mixing, a rigid solid corner, variable-density
constant velocity, four atomic rejections, 2-D shear/passive N16/32/64 refinement
at a predeclared joint ratio below 0.7, fixed-N timestep differences below 0.7,
and transposed-corner symmetry.

## Current-source validation

Fresh Windows MSVC and WSL Linux GCC15 Release builds pass all seven groups.
Actual project/compile-command and executable/archive hashes were audited before
runtime. The separate data-only Python reviewer reconstructs the two exported
mixing/corner cases using exact binary64 fractions: primal constituent/U/passive
packets, half-cell dual fluxes, target inertia, J, K, positive variance heat,
wall reaction and global accounts. An 80-digit Fourier recurrence and continuous
translated sine check the five saved shear/passive profiles. It passes 3,884
saved-operand/profile checks on each platform and rejects three altered records
(dual flux, shear profile and trace mass). This is independent arithmetic in a
separate checker, not an independent agent review.

For the periodic unit-square shear at `T=0.125 s`, `u_x=0.5 m/s` and Courant 0.2:

| Grid | Velocity L1 (m/s) | Passive concentration L1 |
| --- | --- | --- |
| 16 × 16 | 0.0102993337394495 | 0.00823946699155952 |
| 32 × 32 | 0.00522505284321382 | 0.00418004227457108 |
| 64 × 64 | 0.00263618086061237 | 0.00210894468848989 |

Both successive error ratios are approximately 0.50732 and 0.50453, below the
predeclared 0.7 joint bound. On the fixed 16 × 16 grid, the Y-momentum differences
between Courants 0.2/0.1 and 0.1/0.05 are 0.0007087416799651 and
0.0003771627790080 kg m/s, a ratio of 0.53216. Smaller explicit timesteps approach
the spatially diffusive upwind solution; their absolute continuous-sine error
need not decrease on a fixed grid. Windows/Linux reported errors differ by at
most `2.95e-17`. These smooth periodic controls establish first-order transport
trends, not arbitrary discontinuity, contact, pressure or free-surface accuracy.

The exact two-stream control resolves `J={0.75,-0.75}` and converts 0.4375 J to
heat. The solid-corner case has a measured energy residual of
`-8.3138685274e-10 J`, inside its unchanged `9.2708245003e-7 J` operand bound.
The bound includes stored U; it is not an exact-heat or entropy certificate.

Private captures are `local/occupied-staggered-validation/mac-transport-{windows,linux}-r1`,
including source freezes, EOF-checked bounded raw pipes and compiler approvals.
Completed-review SHA256 values are
`186c52e3ecb27839d249fd3cceec2a42309a888d45bc88f0c3d58d205df26cba`
(Windows) and
`6d72d785f6f1a375f572d0bb8e74ee7897953bb9bcbe9a617fc7d423e249f4e5`
(Linux). Build, runtime and review retain the original 180/30/30-second bounds
and 128-KiB pipe limits. No earlier failed physical gate was replayed. The
default game DLL still has SHA256
`5eb98c28d52c9e5408acdd3a310d060331566f135345dc7d707e884001fa485d`.
No game, production solver or performance acceptance follows from this result.

The later [closed pressure/transport owner](MAC_CLOSED_WORLD.md) uses the
EOS-aware overload. Its reconstruction-cohort Windows/Linux builds repeat all seven transport
groups and the 3,884-check saved-data review successfully. A full retained share
now returns the original value directly; the added zero-flux control checks
every stored quantity bit for bit. No other physical bound is changed.

The [reconstructed parcel path](MAC_RECONSTRUCTION.md) adds an explicit input
plan containing outgoing constituent/U/passive parcels, retained inventories,
source/retained volumes and one common limiter per donor. It checks local
decomposition, target volume, EOS and face geometry against actual operands.
Actual parcel masses define primal fluxes and drive the same compatible dual
J/K transport and positive variance heat. Uniform donor-cell transport remains
the reference overload. The seven original groups and current 3,888-check review
pass on Windows/Linux; the new parcel controls and composed-owner checks are
separate evidence. Normal face momentum transport remains first-order.

[Wide derived-energy integration](MAC_ENERGY_TRANSPORT.md) now carries K,
variance heat and assigned cell heat with independent exponents. Legacy report
values have explicit rounding masks; full values drive energy accounts. Four
additional Windows/Linux controls and a 4,530-check reviewer pass, including
nonzero energy whose binary64 projection is zero. Primary transfer rules remain
unchanged. The Linux strong-flow gate now stops at a primary helium parcel below
binary64 range, so this does not establish general flow or game acceptance.

The later [scaled primary-parcel integration](MAC_PRIMARY_PARCELS.md) retains
complete normalized prototypes when an actual constituent/U/passive projection
would lose range. Full contributions reach donor/target accumulators before
bounded primary cell storage; unresolved destinations still reject atomically.
Five new groups and a 7,681-check review pass on both platforms. The Linux
strong-flow retry then stops at an incoming canonical-momentum share below
binary64 range. Its full strong-flow gate remains failed.
