# Compressive stress in material pressure work

This component prepares an alternative to splitting shock damping from the
material pressure step. The existing strong-flow gate still fails density
refinement. No runtime mode, owning integration or new strong-flow result is
provided by this local response.

Compatible staggered methods derive internal-energy work from the same forces
that change momentum; see [Caramana et al. (1998)](https://www.sciencedirect.com/science/article/abs/pii/S0021999198960296).
The constitutive response here is explicitly specified below. It is not an
implementation or validation of that paper's complete algorithm.

For fixed source mass M, volume V0, grid spacing h and timestep dt, define
`s=max(-dV/V0,0)`, `a=h/dt` and `rho=M/V0`. A supplied nonnegative, frozen
linear speed L gives compressive bulk stress `q=rho*(L*a*s+a*a*s*s)`.
The quadratic coefficient is one. No coefficient is fitted to the failed
Riemann norms. The reference model supplies the original isentropic endpoint
and work pressure. Add `Q=-q*dV` to its actual stored internal energy and add
q to its work pressure. The same candidate thus satisfies
`dU=-(p_isentropic_work+q)*dV`, within the original stored-operand allowance.
Q is nonnegative and vanishes in expansion or at zero volume change. At fixed
V0/h/dt, `dq/ddV=-rho*(L*a+2*a*a*s)/V0` on the compressive branch. The zero-step
tangent is explicitly the compressive one-sided tangent.

The returned equilibrium is evaluated from the actual stored U and endpoint
volume. Every constituent, including traces, is unchanged; chemical reference
energies may be negative. Full-exponent stress, derivative and heat are retained,
including heat below binary64 range. Unresolved canonical U increments are
reported. Actual U/work and entropy checks use the existing 128-epsilon operand
and stored-U/temperature allowances, with no unit floor. Invalid EOS states,
internal liquid voids or invalid inputs return no candidate. Source/model state
remains unchanged.

Before integration, strict Windows, Linux and Linux ASan/UBSan controls must
cover dry gas, wet gas/liquid, compressed water, expansion/zero change, negative
reference U, subrange positive heat, geometric scaling, monotone stress/tangent
and late EOS/input rejection. A saved-only reader must reconstruct stress,
heat, EOS, entropy and actual work independently, and reject altered heat,
constituents and endpoint U. Build and review bounds are 180 seconds each;
native controls have 60 seconds and 1 MiB per raw pipe. First failures remain
retained. The target remains explicit and outside default builds and game
dispatch. This does not authorize another unchanged strong-flow run or promote
any existing experimental selector.

## Component verification

Strict MSVC and GCC executions pass seven native groups and all 16 saved
responses on Windows and Linux. Linux ASan/UBSan passes the same controls;
all 21 linked numerical/test compile commands are instrumented, with fatal
findings and leak detection. Each platform's saved-only review performs 1,724
checks and rejects altered heat, trace mass and actual endpoint U. Ten input
controls reject, including a valid isentrope whose added stress heat exceeds
the original temperature domain. Negative stored U, wet mixtures and compressed
water retain their actual EOS and original energy/entropy bounds.

The first negative-reference test changed only the EOS energy and was correctly
rejected for contradicting its registry. The corrected registry's deliberately
large negative shift then failed the existing per-species sensible-energy
representability rule on all platforms. That case is retained as an explicit
precision rejection. An admitted negative-reference control uses a modest
matching registry/profile shift. Neither EOS guard is relaxed. The first GCC
build also rejects a copied range-loop value under warnings-as-errors; the test
now borrows that value.

After all native groups pass, the first saved reviewer rejects its own reversed
secant-derivative sign. Differentiating `p_work=-dU/dV` gives
`dp_work/ddV=(p_endpoint-p_work)/dV`. Correcting that reader expression passes
the same captured native states without a rebuild or native relaunch. The
native implementation is unchanged since its first attempt; every failure is
retained and all owned processes/pipes are terminal.

Native captures are `local/mac-compressive-work-r3`; corrected reader evidence
is `local/mac-compressive-work-review-r4`. Terminal review SHA-256:
`752d7c36041fc016fd8ba44d924e971ebfae59538be90e7efa30b74d7dd7c8f2`.
The default game library and installed SimDLL remain unchanged.

Next, couple this response to the same nodal pressure forces and material volume
root, with stored U plus face kinetic energy checked together. A local material
response alone does not prove that owning conservation or shock accuracy.
The original strong-flow gate, production takeover and playable-colony
requirements remain open.
