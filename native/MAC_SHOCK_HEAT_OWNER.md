# Owning compression-activated heat stages

The default closed occupied timestep selects the verified
[compression-activated heat component](MAC_SHOCK_HEAT.md). Its explicit
`none` reference remains available. The staged owner passes its component,
regression and sanitizer controls. The original strong-flow gate still fails;
real-game acceptance remains pending. The original Riemann and gravity sources,
inputs and physical error limits remain unchanged.

## Completed candidate

Each attempted substep uses this order:

1. Physical material conduction half-step, when enabled.
2. Compression-activated heat half-step, enabled by default.
3. Quadratic compression half-step, when enabled.
4. Existing implicit pressure work and material/momentum transport.
5. Quadratic compression half-step.
6. Compression-activated heat half-step.
7. Physical material conduction half-step.

The primitive pressure/transport API remains a separate component. Existing
primitive, momentum-only and compression-only comparison tests explicitly
disable shock heat; they retain their original operands and physical limits.
The unchanged weak-acoustic, gravity and strong-flow controls exercise the
new default. New owning controls exercise its entire staged candidate.

Material conductivity and compression-activated regularization have separate
settings, reports and energy accounts. The material operator retains its
existing single-phase constant-capacity scope. The heat regularization uses
the full mixed EOS and does not replace physical conductivity or gas diffusion.
No new mixed-phase physical-conduction coverage is implied.

The owning loop holds all candidate state locally. Only an accepted attempt
adds its stage bounds, full heat totals, event/root/rejection/unresolved counts
and completed state/time. A later attempt-budget failure returns no owner.
Each requested interval increments the revision once. Original global U+K,
momentum/wall, positive-trace and stored-U entropy limits are retained. New
heat stage accounts use the original 128-epsilon operand rule; no field is
changed to force closure. Internal heat changes neither mass nor potential
energy. Actual source/final entropy still covers the complete interval.

The native backend links the new static dependency with position-independent
code on Linux. The sanitizer option instruments that dependency and the
relevant owning libraries/tests; private validation additionally audits every
compiled source's flags. No game DLL is deployed by these standalone builds.

## Required validation

Six new owning groups are required on Windows/MSVC and Linux/GCC:

- Default periodic wet mixtures: actual heat/compression/pressure component
  replay must match the complete owner state and all committed diagnostics.
- Wet solid corner with full wall and state accounts.
- Both physical conduction and default shock heat, including actual nonzero
  redistribution by both operators and independent conductive equations.
- Two accepted substeps, one interval revision and correct complete stage chain.
- An initial heat transfer below binary64 range, retained in full with explicit
  unresolved primary-U flags, followed by the actual pressure/transport step.
- Explicit disabled reference, invalid enum rejection, later failure after a
  completed candidate, and deterministic recovery without cached partial state.

Save every actual component operand and the states between stages, plus the
actual returned owner and unchanged source. The saved-only review reuses the
independent heat, compression, pressure/material and geometric momentum
reviews. It must match all stage boundaries, check the complete actual U+K,
wall momentum, constituent/passive and entropy accounts, reconstruct physical
conductance, and reject altered clock, chain, trace and heat records. No
independent agent review is claimed.

Before one changed-source strong-flow retry, both platforms must pass the
existing 85 native groups and reviews, eight heat-component groups, these six
owning groups, and the existing physical-conduction/domain/gas-transfer native
controls: 102 groups per OS. Linux sanitizer coverage must include the new
owning path and its weak-acoustic regression. Scientific sources must retain
their original hashes. A failed strong-flow result remains failed and is not
replayed unchanged.

Bounds fixed before the first run: 180 seconds per aggregate build, review or
weak-acoustic/gravity control; 120 seconds per new owning capture; 4 MiB per new
owning raw pipe, which includes all component operands and complete states
between stages. Existing ordinary pipe caps and physical limits remain as
before. The original strong-flow capture retains 180 seconds and 2 MiB. These
are bounded correctness runs; functional integration precedes performance
tuning under the accepted completion plan.

Passing these controls is not full production, colony ownership, save/reload,
phase/free-surface, material-catalogue, gameplay, rendering or release
acceptance. Actual Windows/Linux game verification remains required.

## First results

All 102 groups and their saved reviews pass on Windows/MSVC and Linux/GCC15.
The new owner review checks 122,035 / 122,029 identities and rejects altered
clock, stage chain, trace and heat records. Linux ASan/UBSan passes 35 groups
and their reviews, including all six owner groups and the unchanged acoustic
and gravity controls, with empty native stderr. Fatal findings and leak
detection are enabled. Every compiled dependency is checked for sanitizer flags.

The first Windows saved review fails because its wall-account reconstruction
omits the already exported material-transport wall impulse. The native six
groups pass. Adding that operand to the reviewer closes the complete balance;
the same retained native data and binaries are reused. No native rerun,
physical bound or arithmetic change is made for that review correction.
The failed terminal SHA256 is
`69198e6d9a8e84c13f30fcfe052a30ad680e08de5c6678de34034cf4f379e487`.

Accepted private cohorts under `local/occupied-staggered-validation`:

- `mac-shock-heat-owner-windows-r2`, source-freeze SHA256
  `3a43edd519d59a4cb3e07db6e9b948d7b1781578fe867b36b2be469f7ce44812`,
  terminal SHA256
  `362215bdc1463bd268f426f6ed2951a89fff24ac3f923b43219a0f936fcf5a54`.
- `mac-shock-heat-owner-linux-r1`, source-freeze SHA256
  `c48095b4df7089db2da4d5acac0239c4badc1cb17361e359ee09776df86ad70f`,
  terminal SHA256
  `155b2de83c6c2fd7a14c4ed5e74e69d7c08126daae94ca4cc74a17b1a2de7b90`.
- `mac-shock-heat-owner-linux-asan-r1`, source-freeze SHA256
  `bf05d1ca658e3d45aaca375f31f5ceb032aba026808676033e32bf936dd6274a`,
  terminal SHA256
  `dd9b2862f0c1058542f2b3b2e6e0e4c66a530eae326ce1a5b6d09fc104ee4ac6`.

The one changed-source Windows strong-flow retry completes Sod at all three
grids, then fails collision density/pressure refinement at N128. Central
collision density errors fall below 0.7%, but the full profile errors fail the
unchanged gate. See [the saved profiles and diagnosis](MAC_RIEMANN.md).
Linux does not repeat this shared failure. The default game DLL is unchanged;
there is no new independent-agent, Unity, production or performance acceptance.
