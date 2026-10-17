# Physical viscosity in the occupied native owner

The MAC owner has an optional Newtonian viscosity operator for occupied gas,
liquid and gas/liquid cells. Constant dynamic shear and bulk coefficients are
explicit settings in Pa.s. They are development surrogates, with no claim of
complete material-dependent transport properties. The default coefficients are
zero. Physical viscosity is separate from compression/shock regularization.

The constitutive model uses the compressible Newtonian stress with a separate
bulk coefficient, following the [NASA formulation, slide 30](https://www.grc.nasa.gov/WWW/wind/TFAWS2007/Formulation.pdf).
For plane strain, its nonnegative dissipation splits into

`mu*(du/dx-dv/dy)^2 + (mu/3+zeta)*(du/dx+dv/dy)^2 + mu*(du/dy+dv/dx)^2`.

Normal strains sit at cell centres; symmetric cross derivatives share an
interior corner. This retains rigid rotation rather than damping its two
velocity gradients independently. The supported boundaries are periodic or
impermeable free-slip walls. Corners touching solids supply no tangential
stress. Moving solids, no-slip coupling, vacuum/interface geometry and
temperature/composition-dependent viscosity remain separate work.

Each strain is a rank-one relaxation with fixed face inertia:
`dJ/dt = -W*a*(a.u)`. It integrates exactly over its subinterval using the
exponential decay of `a.u`. Symmetric forward/reverse half sweeps compose the
stresses; a periodic Fourier shear control shows second-order time refinement
to the independent discrete viscous solution. The complete owner places these
stages around its existing heat, pressure and transport operations. This does
not establish second-order accuracy for the complete production timestep.

Canonical face momentum uses range arithmetic. Lost modal kinetic energy heats
the supporting fluid cell, or the four cells sharing a shear corner. Every
species, positive trace, passive quantity and occupied volume stays fixed
during this operator. Wall reactions remain explicit. Unrepresentable heat or
momentum updates are counted. Closed energy and momentum accounts retain the
128-epsilon rule over actual operation operands, without an absolute floor.
For transverse momentum near zero, this includes the cancelling stress
tractions that produced it. An earlier refinement run rejected because its
account omitted those operands; that failed capture remains retained.

Stored heat has a positive entropy lower bound from `Q_stored/T_after` for
each fixed-mass/volume heating update. This avoids subtracting rounded EOS
entropies for weak heat. It is labelled as a lower bound, not an exact entropy
integral. Every candidate and EOS validation is immutable; a late failure
publishes neither its state nor the owner clock.

Enabled viscosity writes physical checkpoint version 3, adding both dynamic
coefficients after the existing settings. Version 2 remains readable and
byte-for-byte unchanged when viscosity is disabled. Negative/nonfinite
coefficients, unsupported versions and a noncanonical all-zero version 3
image reject. An independent old-v16/new-disabled checkpoint generation gives
the identical SHA-256
`77fec804f04865f777dd52857bd435a7aaa4113fa6be7a0176dd0aa2946fb77a`.

## Verification

GNU Linux, GNU Windows binaries under Wine and Linux ASan/UBSan pass exact
modal relaxation, stiff wall reactions, rigid rotation, below-range momentum
and heat, gas/wet mixtures, both axes, periodic/free-slip boundaries, stationary
and zero-coefficient states, invalid-input rejection, Fourier refinement and
complete owner/checkpoint continuation. Native stderr is empty for all three
accepted captures. The existing physical checkpoint suite also passes.

The saved-only reviewer imports no native code. With 90-digit Decimal
arithmetic it reconstructs stress stencils/weights, modal inertia and strain,
exponential impulses, wall reactions, kinetic-to-heat conversion, supported
cell heating, complete U+K/momentum accounts and gas/elastic-liquid EOS
readings. It verifies exact species/traces/passives and the final canonical
face state. Each platform capture passes 1,068 checks and rejects eight altered
captures, including heat, impulse, asymmetric stress, trace inventory and
entropy. The component is a backend CTest named `mac_physical_viscosity`.

```sh
cmake --build build-oni-observer --target ma_mac_viscosity_tests
build-oni-observer/ma_mac_viscosity_tests > /tmp/viscosity.txt
python3 native/tests/review_mac_viscosity.py /tmp/viscosity.txt
```

An actual ONI source/save and repeated continuation still require a dedicated
native scene and ABI diagnostics. This component does not mark viscosity
gameplay, whole-colony ownership or the [complete overhaul](completion-plan.md)
as accepted.
