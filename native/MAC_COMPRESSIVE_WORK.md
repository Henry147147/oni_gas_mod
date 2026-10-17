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
all 21 dependency/test compile commands are instrumented, with fatal
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

## Coupled pressure control under implementation

The private occupied-material solver now accepts a complete constitutive work
response. Its public isentropic entry supplies no override and retains the old
calculation. The new explicit `advance_mac_compressive_pressure` entry freezes
one linear coefficient per material cell. It uses the same total stress in the
nonlinear volume root, nodal force and stored material U. Rejected Newton trials
are not published; completed response records are rebuilt and matched against
the accepted pressure and energy fields. This is a material pressure step,
before Eulerian remap or the owning interval.

The first coupled validation must pass six native groups and 15 accepted
responses per OS and under Linux ASan/UBSan: dry reflection, wet pressure,
filled water, gravity/body work, uniform periodic translation and five early/
late rejections. Actual stored U plus nodal kinetic energy minus body work,
momentum with individual wall/body impulses, midpoint volume closure and actual
entropy retain their original operand bounds. The saved reviewer independently
rebuilds each force and geometry equation, and must reject node, geometry, heat
and trace corruption. The original 16 constitutive responses must remain byte
identical after extracting their common output helper. Four preceding normal-OS
streams (reconstruction, closed pressure/transport and both physical momentum
controls) must remain byte identical to their accepted captures.

Predeclared bounds: configure/build 180 seconds each, new native controls
60 seconds and 1 MiB per pipe, saved reviews 180 seconds. Preceding native
controls retain 180 seconds and their original 2 MiB capture cap. This control
does not select a new owning mode or authorize another unchanged strong-flow
run.

All six coupled native groups now pass on Windows, Linux and Linux ASan/UBSan.
Each independent saved review passes 2,277 checks over the 15 accepted material
responses and rejects all four corruptions. The preceding constitutive control
still passes 1,724 checks and its three corruptions per platform; all three
constitutive native streams remain byte identical after the shared output
helper extraction. All eight preceding normal-OS streams remain byte identical
to their accepted captures. The sanitizer inspection covers all 23 dependency/
test compile commands. The default and installed native libraries are intact.

The first coupled saved review selected zero heat in an expanding cell for its
doubling corruption, so that witness changed nothing. It is retained as a failed
review. The corrected witness selects strictly positive heat and explicitly
checks that each corruption changes its saved operand. It passes the same native
captures without rebuilding or repeating the coupled native controls.

Private native evidence is `local/mac-compressive-pressure-r1`; corrected
review and unchanged-path checks are `local/mac-compressive-pressure-review-r2`.
Terminal SHA-256:
`3694462446879f742ad6033ed0327f7b4ab063fdd949558efbc72b28de1c69b9`.
All owned processes and pipes are terminal. No independent-agent review is
claimed. The owning timestep/selector, complete prerequisite rerun, original
strong-flow gate and game acceptance remain required.

## Windows backend regression

Source `e181a18` also passes a fresh Windows build and the ABI, detour and four
native-domain groups. These exercise the default pressure path after the
private work-law refactor. The installed-assembly suite passes 132 groups;
four Python runner suites pass 120 tests without skips, and 12 actual
PowerShell option checks pass. The adapter retains only its two existing
MSB3277 reference conflicts. Native terminal SHA-256:
`2454dd94e8003a1d3c770fa306afb8a88a9f0f8dfb264452afa7cd7ac6bbc102`.
Managed/runner terminal SHA-256:
`546ad352dfb7196e0ced460389da673905487d9cf9e791565bfb916dcf094ce2`.

Freshly built probe and native libraries additionally pass isolated gas and
water runs against the installed licensed Windows SimDLL. Each advances 61
source frames, restores a paired physical/native checkpoint into a new
session, and reproduces 12 complete continuation frames exactly. Invalid
sources reject before native loading; zero-time restore flags, compatibility,
outside cells and frame-zero pairing also pass. All processes exit normally
with closed pipes. The first saved reader incorrectly required empty native
stderr; its retained failure is corrected to recognize exactly three native
SimThread startup lines. The original captures pass without relaunching.
Private evidence is `local/windows-licensed-native-r5`; terminal SHA-256:
`4089728c3182940eaee2eef8e5247e78df605fb1144ee27ae1d6fec653f936cf`.

These runs use the default owning backend. The new compressive work control
remains unselected. They do not execute Unity, test ordinary colony saves or
establish Windows actor persistence, production authority or the playable
colony. The installed SimDLL and default game library remain unchanged.

## Explicit owning integration under validation

Compression selector 3 (`coupled_pressure`) now calls the coupled material
pressure step before remapping. Modes 0/1/2 and the default value 2 retain their
existing paths. Selector 3 skips both split compression half steps. It freezes
the existing incoming sound speed and pressure sensors once per candidate,
using the larger directional sensor for one isotropic bulk stress. The
quadratic coefficient remains one. The accepted work pressure supplies the
canonical face impulse and midpoint volume sweep; actual heated U supplies the
remap and subsequent EOS readings. Compression diagnostics count its heat once
without adding it again to the energy or wall account.

This first owning path rejects incoming nodal velocities or midpoint sweeps
that need range projection, preserving the original source atomically. It
does not silently replace below-range volume work with zero. The unchanged
full-exponent constitutive response retains positive subrange heat and reports
unresolved canonical-U updates. Existing selectors retain their range support.
Checkpoint version 2 already stores the complete compression selector; the new
value is admitted explicitly, without changing old values or saved bytes.

The first controls require seven native groups per platform: dry two-dimensional
flow, wet gravity/walls, filled water, uniform translation, exact unsplit owner
clock/heat accounting, 24 new checkpoint-setting continuations and early/range/
late atomic rejection. Four completed scenes export 29 accepted constitutive
responses and their actual source, pressure and remapped states. A saved-only
reader independently checks frozen coefficients, every trace/passive, actual
EOS, nodal forces, midpoint sweeps, primary face kinetic energy and material U.
Altered heat, trace, sweep and endpoint U must reject. The preceding constitutive
and coupled controls and eight normal-OS legacy streams must remain unchanged.

Configure/build and owner/review bounds are 180 seconds each; owner raw pipes
have 2 MiB and the preceding local controls retain their original bounds. This
does not run the original strong-flow benchmark. All complete prerequisites
must pass before a distinct changed-method attempt can be considered. Game
dispatch and production defaults remain unchanged.

The first strict builds reject a fixture helper named `clock`, which collides
with the C library function. All three build failures are retained; renaming
only that helper permits a fresh attempt. No native owner control ran in the
failed build cohort, and its numerical implementation is unchanged.
The next Windows cohort passes all native controls and saved reviews, including
8,393 new owner checks. GCC rejects two copied wall-reaction loop operands under
warnings-as-errors. Borrowing those test-only values addresses that build
failure; the owning numerical implementation remains unchanged.

All seven owning groups now pass on Windows, Linux and Linux ASan/UBSan. Each
saved owner review passes 8,393 checks over 29 completed responses and rejects
all four corruptions. Each platform also passes the preceding 13 component
groups and 4,001 saved checks, with seven corruption rejections. All 24 new
checkpoint-setting continuations match exactly. The three constitutive streams,
three coupled pressure streams and eight normal-OS legacy streams remain byte
identical to their accepted captures. Borrowing the two test operands also
preserves the preceding Windows owner stream byte for byte.

All 35 compile commands in the composed sanitizer target graph are instrumented
with fatal address/undefined-behavior findings and leak detection. The first
terminal reader assumed a smaller dependency graph; inspecting the actual
Ninja commands corrects that metadata count without a native relaunch. All
processes and pipes are terminal. The default game library and installed SimDLL
remain unchanged. Private accepted evidence is `local/mac-compressive-owner-r3`;
terminal SHA-256:
`d49529f5c92f40bbe8c1c759ae12d8f7a8664251ba300adc5054c45cc846fc73`.
No independent-agent review is claimed. Complete prerequisites, the original
strong-flow benchmark and game acceptance remain open.
