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
