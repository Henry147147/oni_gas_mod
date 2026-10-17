# Swept momentum with bounded carried energy

`MacMomentumReconstruction::swept_energy_flux` is explicit selector 4. It uses
the swept first moments, neighbour bounds and completed-state admission of
[selector 3](MAC_SWEPT_MOMENTUM.md). It bounds the carried kinetic-energy flux
separately from the admitted momentum correction. Selectors 0 through 3 retain
their laws; the default remains 1. No production backend is enabled.

## Reproduced restriction

The completed, failed selector-3/coupled-pressure strong-flow capture shows a
real separation accuracy defect. At N256, the two central gas temperatures are
119.42 and 118.74 K, versus the exact star temperature of 13.65 K. Most velocity
L1 error lies in the expansion fans. Saved-only localization accounts for both
rows and all x/y kinetic-energy terms. Its first reader incorrectly assumed
exactly zero transverse momentum; that failure is retained and corrected.

A private read-only diagnostic executes separation N64 once at the unchanged
horizon. All final RSTATE, RFACE and RMODE records reproduce the earlier saved
profile exactly. It reports 46 accepted substeps, no global correction-fraction
backtracks, and 62 zero-work-limited central events. In the first remap, all
four nonzero outward momentum corrections from the two stationary central
dual faces are cancelled: their momentum and transported kinetic energy are
both zero. One requested impulse is 0.0145373 kg m/s, but its admitted impulse
is zero. The central material is isentropically cooled from 336.92 to 313.27 K
by pressure work, then heated to 376.39 K by remap; the adjacent shock-heat
stages leave it unchanged. This identifies an actual restriction rather than
an ungated expansion heat law.

Private saved localization: `local/mac-compressive-separation-analysis-r2`,
SHA-256 `91ad7dbb0fec1736881bd5632f9b01413624559116d89cfa9472c80e9259c07b`.
Diagnostic terminal evidence: `local/mac-compressive-separation-diagnostic-r2`,
SHA-256 `dc80d3de53cd6f73008953b420409a31f5407967cb70e02f205984d4f9b063e2`.
The initial cell/face kinetic-energy quadrature discrepancy is also recorded;
neither the initial state nor any benchmark energy is changed to compensate.

## Compatible correction

For the original geometric request R and completed-state fraction theta, let
dJ = theta R. Let ul and ur be the actual sequential pre-correction velocities,
and Il and Ir the final dual inertias. The unrestricted carried energy is
F* = (ul + ur) dJ / 2. Restrict only F to the available nonnegative transported
kinetic-energy account, using the existing per-edge limiter and rounding
backoffs. Retain the full dJ. With D = F* - F, the signed internal-work changes
are

- Wl = (ul - ur) dJ / 2 - dJ² / (2 Il) + D;
- Wr = (ul - ur) dJ / 2 - dJ² / (2 Ir) - D.

Consequently, each side satisfies its original compatible-work identity:
delta Kl + Wl + F = 0 and delta Kr + Wr - F = 0. Pair momentum is conserved.
The carried energy stays nonnegative. Upwind variance and wall heat remain
separate, nonnegative accounts; signed work is deposited into actual stored U.
The event's `transported_work_limiter` multiplies carried energy in selector 4;
in selector 3 it retains its original impulse-limiting meaning.

Both components and the deposited material energy still pass the original
species/passive, momentum/wall, stored-U-plus-face-K, EOS-domain and entropy
checks before publication. An unsafe candidate still uses the common theta
search, and even its donor fallback must pass. No temperature, energy or
entropy floor, scientific tolerance or model coefficient is introduced.
Checkpoint schema 2 already stores the complete uint32 selector. Older readers
that do not support selector 4 reject it.

Conservative remapping of momentum and carried kinetic energy can produce a
different actual kinetic energy, requiring an internal-energy reconciliation;
this issue is described in the primary literature, for example
[Bailey et al., reduced-dissipation staggered remapping](https://laro.lanl.gov/esploro/outputs/journalArticle/Reduced-dissipation-remapping-of-velocity-in-staggered/9916363418903761).
The particular correction above is this prototype's formulation and must pass
its own controls and physical gates.

## Scoped verification

Strict MSVC, GCC and Linux ASan/UBSan pass seven new groups per platform. The
saved reader performs 200,276 checks per platform over 28 affine cases, 162
selected actual readings and 304 compatible-work events. It verifies 12
nonzero corrections with a zero carried-energy limiter. Four new interior
stagnation controls cover both axes and inward/outward flow; selector 3 fails
eight selected outward readings, while its inward controls remain accurate.
All cases preserve the original balances, actual EOS and entropy conditions,
including below-binary64 momentum and both true one-dimensional axes. Four
corrupted streams are rejected.

All 96 dry/wet, material, heat and compression combinations pass exact local
save/restore continuation per platform, including coupled compressive work.
Both cross-OS checkpoint reads preserve their input bytes and exact local
continuation. Eight existing-mode output streams remain byte identical on the
normal OSes. All 33 composed sanitizer compile commands are instrumented.

The first strict build rejects a shadowed local name. The next fixture wrongly
requires an inward-flow defect and is corrected to require the reproduced
defect only for outward flow. These failed captures remain retained. The first
saved readers require exact source-volume equality for the new nondyadic
geometry; corrected readers compare that geometry with the original 128-epsilon
operand bound, then evaluate the actual saved volume. The original 24 cases
keep exact equality. The same native captures pass without relaunching.

Private evidence: `local/mac-bounded-energy-r3`, terminal SHA-256
`cc721ea17bbb331636b560cc8bae89d9820b49e21a0b0159ffc578c903872878`.
All processes and pipes are terminal. The checked shared reader is promoted
with identical normalized source. Explicit selector-4 intermediate admission,
complete prerequisites, strong flow and game acceptance remain to be checked.
Independent agent review is unavailable for this evidence and is not claimed.

## Complete prerequisites and the first strong attempt

The complete suite passes 216 native groups and 36 saved reviews on Windows,
Linux and Linux ASan/UBSan. Selector 4 also passes the explicit intermediate
EOS-admission control with 7,017 saved checks and three corruption rejections
per platform. The original 128-epsilon accounts and 32-epsilon EOS boundary
remain unchanged. The first suite exposes an old negative test that still
expects selector 4 to be invalid; it now uses unused selector 99. Only that
test is rebuilt, with all 232 other passed source-identical stages retained.
The repaired test's outputs match the previous successful outputs exactly.

A sanitizer domain capture is interrupted by WSL power-off. The journal records
shutdown at the capture's exit time. Its retry on the restarted VM uses the
identical executable, arguments and original 900-second limit, and passes with
byte-identical output to the previous successful domain capture. The interrupted
and stale-test failures remain retained. Both normal-OS ABI/detour controls
pass. All 67 original dependency/test commands and 33 repair commands are
sanitizer instrumented. Complete evidence is
`local/mac-bounded-energy-prerequisites-r2`, terminal SHA-256
`79ac4d6bd5793be239b459dc3238c8c722c65478228a457adb55b20343bfd7fd`.

The first Windows strong-flow attempt then exits 1 in 0.122 seconds on the
original paired compatible-work guard in Sod N64, before any complete profile.
Strict compilation and the selection-only benchmark change are inspected
before execution. Its 363-byte stdout and both pipes close normally. No Linux
strong repeat or partial-profile certification is attempted. Terminal evidence:
`local/mac-bounded-energy-strong-r1`, SHA-256
`3363187a4e802fc6cb42089b2c348a2e74c3b504c8a3e5ce972b8ebc675c137d`.

## Preserve tiny work at stationary faces

A read-only diagnostic reproduces the original throw. At the failing right
face, J and carried K start at zero. The impulse is 3.01395e-38 kg m/s and its
new kinetic energy is 1.48831e-71 J. The old evaluation rounds the quadratic
work away while combining terms of order 7.32777e-53 J; the subsequent opposite
term cancels the stored work to zero. The original guard correctly rejects
that state. Exact saved Fractions confirm the residual is 2^45 times the
original operand-based bound. Diagnostic terminal evidence is
`local/mac-bounded-work-diagnostic-r1`, SHA-256
`ed33b4fb375238fde8e5c5dab9bb191213d77ac6d63f4b25798eff140c669d5e`.
The first saved reader reverses one right-side algebra sign in an assertion;
that failed reader is retained and corrected without native relaunch.

For limited carried energy, evaluate the same law directly per side in one
additive expansion:

- Wl = ul dJ - dJ² / (2 Il) - F;
- Wr = -ur dJ - dJ² / (2 Ir) + F.

This retains the cold face's negative quadratic work without intermediate
cancellation. Unrestricted events keep the original velocity-difference
evaluation. The stress, impulse, energy-flux limit, EOS/entropy admission and
128-epsilon account rule are unchanged.

Eight focused groups pass per platform after the repair, with 32 affine cases,
186 selected readings, 368 work events and 20 nonzero corrections at zero
carried-energy limit. Four new tiny-transfer controls exercise both axes and
flow signs. The same new executable source linked to the original libraries
fails at the outward tiny-transfer case, proving it catches the original bug.
The strengthened saved review checks each actual side's kinetic-energy change,
signed work and carried energy under the original rule. It performs 231,450
checks per platform and rejects five corruptions, including erased tiny cold-face
work at the exact paired-work guard. All 96 local checkpoint combinations and
both cross-OS image reads pass. Eight legacy normal-OS streams stay byte
identical; all 33 composed sanitizer compile commands are instrumented.

Focused evidence is `local/mac-bounded-energy-r4`, terminal SHA-256
`328c01903bbc1e584640c7907d3c1bc75bdb763190a64a4bab2b29a179dbd9a3`.
The stronger readers are promoted with identical normalized source after all
captures close. The fresh 217-group prerequisites and changed-method strong
benchmark remain required. No default, production or playable-colony gate is
promoted.

## Fresh full suite and remaining separation failure

The fresh rerun from `7f192b3` passes all 217 native groups and 36 saved
reviews on Windows, Linux and Linux ASan/UBSan. Both normal backend ABI/detour
checks pass, and all 67 composed sanitizer compile commands are instrumented.
The new native streams match the focused repair captures; selected legacy
streams remain byte identical. All processes and pipes close. Full evidence:
`local/mac-bounded-energy-prerequisites-r3`, terminal SHA-256
`4816e6baad131031c3420b0d8a7fe996682d486ff1de926e884826a00aba16af`.

The next original Windows strong-flow attempt resolves the paired-work
rejection and completes all nine profiles with zero timestep retries. Sod and
collision pass their refinement gates. Separation N256 still fails: its
density/pressure/velocity L1 errors are 0.0178341/0.0193070/0.0908767, with
velocity above the unchanged 0.06 limit. Central density and pressure relative
errors are 25.9256 and 56.9301, above the original 0.5 limits. The process exits
1 in 110.355 seconds without timeout. The 1,217,507-byte stdout SHA-256 is
`6c7261e2d4b36d569967806825e265bec4538565edcec273819a65949e3a4727`.
Saved-only review performs 124,190 data checks and confirms the failed finest
criteria. It does not certify the strong-flow gate. Terminal evidence:
`local/mac-bounded-energy-strong-r2`, SHA-256
`16d2d3def3afb8aab944fef026f623086af765922e7e0c900c6edcbdc1e72158`.

Saved localization places most velocity error in the expansion fans. The two
central N256 cells have density 0.008384/0.008782 kg/m3 and temperature
30.551/29.368 K, versus exact density 0.00032616 kg/m3 and temperature 13.650 K.
Their specific entropy changes relative to the uniform initial source are
-364.88/-407.99 J/(kg K), although total domain entropy increases. This calls
for inspecting actual parcel reconstruction, signed work and heat stages; the
global entropy check alone does not explain those local readings. Saved-only
localization SHA-256 is
`cd6a63615392800b0fdb13006de39be89eaf292af704f2abc5fe7914d203d723`.
No unchanged Linux strong attempt is launched. No initial energy, coefficient,
grid, timestep, norm, scientific limit or default is changed. Strong flow,
exact-vacuum interfaces, production coverage and playable-colony acceptance
remain open.
