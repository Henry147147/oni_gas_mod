# Actual stored-state heat entropy

Heat transfer must reject a decrease in actual stored-state entropy beyond the
existing 128-epsilon allowance. Subtracting absolute EOS entropy readings can
falsely reject a transfer when the thermal energy nearly cancels a negative
caloric reference in stored U.

The first rejected pair in the private quadratic N256 separation run has fixed
volume 1/65536 m3 on each side, nitrogen with a positive helium trace, and
temperatures near 416.940 K. Its actual stored U changes are exactly opposite:
`-61 / 576460752303423488` J and `+61 / 576460752303423488` J. At 90 and 120 decimal
digits, the fixed-inventory ideal-gas entropy increment is positive,
approximately `1.38133532328e-26` J/K. The original allowance is approximately
`9.03164930849e-19` J/K. Subtracting the two native absolute entropy readings
instead gives `-1.73472347598e-18` J/K and rejects the pair.

This diagnosis uses the actual stored energies, constituent masses, caloric
offsets and heat capacities, with `Cv * log((U1 - offset)/(U0 - offset))`.
The first operand capture is `local/mac-quadratic-heat-entropy-diag-r1`, terminal
SHA-256 `44eab819ab42d017ee301f70467457610d607ba21bbd0a5da18e43e36193dcb7`.
The second adds the actual full-range conductance and transfer operands without
changing the failing state. Its terminal SHA-256 is
`36613fe3ad744ef547ff828d1d3281e092e39f69a0b375a4550fcc547880e149`.
Both captures retain the original throw, physical states and comparison bound.

`mac_shock_heat.cpp` now uses the existing relative material-entropy helper for
each actual fixed-mass, fixed-volume endpoint. Pure gases use the conserved
caloric energy increment; mixed and liquid states retain the helper's EOS
temperature and gas-headspace calculation. An identical stored U and identical
constituents at fixed volume give exactly zero change. The pair and global
checks use the same calculation. Bound operands, their accumulation order,
the 128-epsilon coefficient and the final rejection sign remain unchanged.
Heat fluxes, final stored U, pressure, composition and timesteps are unchanged.

The Windows focused regression compiles identical test inputs and dependencies
against the original heat source and the repaired heat source. The original
source fails at the original entropy guard; the repaired source returns the
same captured stored endpoints. Three groups cover forward/reflected transfer,
unchanged and below-range stored states, and a genuine cooling state that still
violates the original bound. Independent saved review performs 528 checks and
rejects five mutations, including a removed trace, a zeroed positive entropy
increment, altered U, a doubled allowance and concealed cooling.

Evidence: `local/mac-heat-entropy-r1`, terminal SHA-256
`6ef680f071305fe1d57961f3adf81794517856ef6508030f1b8b13336912e734`.
Reproduce the focused native output with `ma_mac_shock_heat_tests
--entropy-regression`; review it with `native/tests/review_mac_heat_entropy.py`.
This scoped result does not complete fresh native prerequisites, the strong-flow
accuracy gate, the first playable colony or actual Windows/Linux Unity testing.
Private quadratic reconstruction and pooled remap remain unpromoted.
