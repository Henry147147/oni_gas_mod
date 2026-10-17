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

The complete prerequisite rerun from `b703031` now passes 207 native groups and
34 saved reviews on Windows, Linux and Linux ASan/UBSan. This includes the
additional durable-actor domain group and public physical-admission control.
All 66 dependency/test compile commands in the sanitizer graph are instrumented.
Windows/Linux ABI and detour controls also pass. All nine new component/owner
streams and eight normal-OS legacy streams remain byte identical to the focused
accepted captures. All processes and pipes are terminal. Private evidence is
`local/mac-compressive-prerequisites-r1`; terminal SHA-256:
`8aa95ec28425bb7608ad95b691713a8eb8347f457f143ddfd8b9d346ec2e923b`.

## First owning strong-flow attempt

With those prerequisites closed, one strict Windows attempt selects material 2,
momentum 3, compression 3 and heat 2. Only those selectors differ in the original
scientific source. Grids, initial states, norms, thresholds, coefficients and the
180-second/2-MiB bounds are unchanged. The process exits 1 after 80.454 seconds;
both raw pipes reach EOF and close normally. Its 808,774-byte stdout SHA-256 is
`db99baa7adf8570c4c71df43163674611cfdc74763eb2819ea9636d4b15f3ef1`.

All three Sod and all three collision profiles complete and pass their native
refinement gates. Collision density/pressure/velocity error ratios are
0.848334/0.819035/0.613088 from 64 to 128 cells, and
0.414988/0.386744/0.481236 from 128 to 256. The original collision density
failure is addressed in this attempt. Independent saved-only review passes
82,824 checks over those six complete profiles, including their actual energy,
momentum, EOS, trace/passive quantities and refinement. This partial review
explicitly does not certify the whole strong-flow gate.

The separation case stops on the unchanged actual stored-U entropy allowance:
`Compressive actual stored-state entropy falls below the original stored-U allowance`.
No separation profile completes. The full gate remains failed, no unchanged
Linux strong attempt is launched, and no default is promoted. The failure must
be diagnosed without weakening the entropy or other scientific bounds.
Private evidence is `local/mac-compressive-strong-r1`; terminal SHA-256:
`5f1ad7eb9acadedaac4e61f9165acbeb2ec8015c73350bfe1eff65a545496e8b`.
Production ownership, exact vacuum interfaces and playable-colony acceptance
remain open.

## Separation entropy measurement repair under validation

A bounded Windows diagnostic runs only the failing 64-cell separation case;
private instrumentation prints the rejected conserved state and identifies a
Newton trial. It then throws the original exception. The actual source and
endpoint U are 0.2881186565689019 and -0.23594879742432628 J, crossing the
arbitrary caloric reference while thermal energy remains about 52.2661 J.
Subtracting rounded absolute entropy readings gives -4.16334e-17 J/K, outside
the original 3.56655e-17 J/K allowance. Independent 100-digit calculation from
the actual masses/U/V gives -2.53940e-18 J/K, inside that same allowance.
The conserved-caloric relative estimate is -2.60209e-18 J/K. This is a false
admission failure caused by subtraction of absolute entropy cache values.

The diagnostic and all owned processes/pipes are terminal. Private evidence is
`local/mac-compressive-entropy-diagnostic-r1`; terminal SHA-256:
`eae274d62964d403790a964e1d2322b76a4ddb0c748f72df5197642209fa9ee7`.
The saved analysis does not relaunch a native process or certify strong flow.

The proposed repair computes constant-composition relative entropy. Pure gas
uses actual conserved caloric increments and actual cell volumes, avoiding
rounded temperature differences as well as absolute entropy subtraction.
Mixed/liquid states use stable log ratios of their actual EOS temperature and
gas headspace, following the existing canonical-material calculation. Full-
exponent leading terms retain their sign when their scalar projection is zero.
The original stored-U/T allowance, canonical U, pressure-work law, source
coefficients and scientific bounds are unchanged. Identical-state and actual
cooling controls challenge the measurement separately from the new admitted
reference-crossing response. Complete prerequisites and another changed-method
strong gate remain required before any promotion.

The repair passes eight constitutive, six coupled and seven owning groups on
Windows, Linux and Linux ASan/UBSan. Each saved-only review cohort passes 12,657
checks and rejects all 11 corruptions. The new reference-crossing response is
admitted, identical-state entropy is exactly zero, and real cooling remains
negative beyond the original allowance. All preceding physical source/endpoint
fields and stored-U allowances are unchanged; only relative entropy diagnostics
and their aggregate change. All eight normal-OS legacy streams remain byte
identical. All 35 composed dependency/test compile commands are instrumented.

The first saved comparator accounted for per-response relative entropy but
omitted its coupled aggregate. Its three retained failures are corrected in a
separate reader, which passes the same native captures without relaunching.
All processes and pipes are terminal. Private native captures are
`local/mac-compressive-entropy-r1`; corrected comparison and terminal evidence
are `local/mac-compressive-entropy-review-r2`, terminal SHA-256:
`8cec7fd567706702e9911e242c1f964a984f632db13fcdd095c782948e730d74`.
The complete prerequisite rerun and changed-method strong-flow acceptance are
still required. No default or production claim changes.

The complete rerun from `f09d0e4` passes 208 native groups and 34 saved reviews
on Windows, Linux and Linux ASan/UBSan, plus normal-OS ABI/detour checks. All 66
dependency/test compile commands in the sanitizer graph are instrumented. Its
new streams match the focused repair captures exactly; all eight selected
normal-OS legacy streams remain byte identical. All processes and pipes are
terminal. Private evidence is `local/mac-compressive-prerequisites-r2`;
terminal SHA-256:
`3fd6cfa66b72d88f2a989508ca93f48f71d98ad07143b643c1cd9d2278114489`.

## Separation accuracy remains failed

After those prerequisites close, the changed entropy-measurement method runs
the original Windows strong-flow benchmark once. It completes all nine profiles
with zero timestep retries. All six prior Sod/collision profiles remain byte
identical, so the repair changes no previously completed physical trajectory.
The separation entropy rejection is resolved. The process exits 1 after
103.594 seconds on the original finest-error bound. Both raw pipes reach EOF
and close; the 1,217,692-byte stdout SHA-256 is
`6459137bbb05dec96437ddb276f19a58c5a720d930c5df145cc8d94baf5f822b`.

At 256 cells, separation density/pressure/velocity L1 errors are
0.017920/0.021640/0.076338. Velocity exceeds the unchanged 0.06 limit. Central
density and pressure relative errors are 81.8118 and 721.5274, also exceeding
the original 0.5 limits. All three errors refine, but refinement alone does not
satisfy these absolute criteria. Saved-only review performs 124,190 data checks
and confirms the failed fine-profile criteria; it does not certify strong flow.

Private evidence is `local/mac-compressive-strong-r2`; terminal SHA-256:
`d2845970a1f950d7692616630d76703c77474652b804a27dc869acd3947be902`.
No unchanged Linux strong repeat is launched and no default is promoted. The
remaining separation error needs analysis of actual momentum transport,
remap heat, shock heat and initial representation before another method change.
Production, exact-vacuum interfaces and playable-colony acceptance remain open.
