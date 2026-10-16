# Symmetric compression coupling

All 66 native groups and saved-data reviews pass on Windows/Linux; the full
strong-flow gate remains failed. The preceding
[post-step compression cohort](MAC_COMPRESSION.md) passes its scoped controls
but fails collision pressure refinement. This change replaces its first-order
operator ordering with compression(dt/2), pressure/transport(dt), compression(dt/2).
The two-stage primitive stays unchanged. Both compression candidates, energy
bounds, heat and wall impulse commit only with the complete accepted substep;
an unsuccessful later attempt publishes none of them. The explicit `none`
reference setting is retained.

Symmetric operator composition is described in
[Strang's primary paper](https://epubs.siam.org/doi/10.1137/0705041).
That reference does not certify this nonlinear spatial discretization. Normal
momentum advection remains first-order, and no claim of second-order accuracy
for the complete Euler solver follows from symmetric temporal ordering.

The interval entropy reading now uses actual initial/final EOS states in both
zero and nonzero gravity, at the unchanged endpoint operand bound
`128 epsilon sum(|U_initial|/T_initial + |U_final|/T_final)`.
Individual pressure/gravity/compression reports keep their existing meanings.
This removes the former zero-gravity reading of compression entropy alone.

Before the first build/run, five owner groups are fixed:

1. The existing gas owner, now with separately exported pre/post compression.
2. The existing mixed-liquid solid-corner owner with both stages and wall work.
3. The existing full-range owner, retaining positive unrepresentable heat and
   explicit unresolved primary-update counts from both halves.
4. The existing disabled-reference, invalid-setting and later-failure rollback
   controls.
5. A manufactured forced-compression pair with exact Riccati solution. Inertias
   are 2/4 kg, initial J 6/-8 kg m/s, opposing compression force 4 N, coefficient
   rho*area 0.4, duration 0.5 s and N8/16/32 time intervals. The closing velocity
   satisfies `c' = a F - a b c^2`, `a = 1/I_left + 1/I_right`. Each successive
   normalized symmetric error must be below 0.30 of its predecessor, and N32
   error below 1e-4 and one tenth of the original post-only error. Removed K,
   heat and pressure-kick work retain the 128-epsilon actual-operand account.

The saved reviewer independently replays every pair event in both halves,
validates stage links and endpoint mass/U/J/entropy, and recomputes manufactured
solutions in 80-digit arithmetic plus exact rational subflow composition.
The same altered owner heat, wall impulse and constituent controls must reject.
All 61 other native groups and their saved reviews must pass, giving 66 groups
per platform before a strong-flow retry. Scientific Riemann/gravity source
bytes, problem inputs, grids, dt and original .85/.06/128-epsilon limits remain
unchanged. No parameter tuning or oracle substitution is part of this change.

Bounds fixed before invocation: aggregate strict build 180 s, owner native and
review 60 s, compression/exponent/canonical/momentum/primary/derived native and
review 60 s, range/original transport 30 s, reconstruction 180 s, closed/gravity
60 s, each raw pipe 512 KiB. A failed strong gate can repeat only after these
prerequisites at its original 180 s/2 MiB. First results are retained. These are
correctness bounds, without production performance, independent-agent, game or
shipping acceptance. The path stays outside default builds and C ABI/game dispatch.

## Results and remaining pressure refinement

First strict MSVC and GCC15 builds pass, followed by all 66 native groups and
saved reviews on both platforms. The five owner groups pass 14,221 saved checks
and three corruptions each, including both compression stages, exact rational
subflow composition and actual endpoint entropy in zero/nonzero gravity.
The 61 other native groups and their unchanged physical reviews also pass.
The manufactured symmetric errors are 1.8988874e-4, 4.7494979e-5 and 1.1875170e-5;
successive ratios 0.250120/0.250030 satisfy 0.30. Its post-only N32 error is
0.003367796. This is temporal evidence for the manufactured pair, without a
second-order claim for the full spatial Euler solver.

One Windows strong retry follows all prerequisites. Sod N64/128/256 passes
joint refinement with fine errors 0.00414965765728/0.00335354264817/0.00621473099095.
Collision N64/N128 still fails pressure:

| Grid | Density L1 | Pressure L1 | Velocity L1 |
| --- | --- | --- | --- |
| 64 | 0.0526543121889 | 0.0585699326621 | 0.0738437039896 |
| 128 | 0.0430535697504 | 0.0808639063913 | 0.0467898223816 |

The ratios 0.817665/1.380639/0.633633 pass density and velocity but fail pressure
at the original 0.85 limit. No collision N256 or separation case runs. A partial
review checks 59,205 identities over five completed profiles and reproduces the
failed pressure norm; original constituent/passive, U+K, momentum/wall, EOS and
entropy checks pass individually. No full strong-flow review or partial-review
corruption controls follow. Linux does not repeat the known shared failure.
The timing repair is insufficient for shock accuracy; spatial momentum
reconstruction and its compatible energy work still require investigation.

Private results are
`local/occupied-staggered-validation/mac-compression-symmetric-{windows,linux}-r1`.
Completed scoped-review SHA256 values are
`411a850cd5176f88f00f36e671705ee442a083813c5b08ea59066c4f28ea00cb`
(Windows),
`b91aaf94c4d4b46cafd2fd0873cd59835da695ed5cc288d1f2b43eac9b6bd4f5`
(Linux). They bind frozen sources, binaries/strict compiler flags, first bounded
build/native/data results and the retained strong failure. No new sanitizer,
independent-agent, Unity, production/performance or shipping acceptance follows.
The default game DLL is unchanged.
