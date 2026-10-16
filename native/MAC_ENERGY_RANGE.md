# Derived MAC energy range

Seven standalone controls pass on Windows and Linux. Its
[transport/owner integration](MAC_ENERGY_TRANSPORT.md) now passes its scoped controls;
the [strong-flow failures](MAC_RIEMANN.md) remain unresolved.
Its purpose is to retain derived kinetic/variance energy such as the Linux
parcel 9.12e-436 J without changing primary masses, internal energy or momentum.

`MacEnergy` stores a normalized binary64 significand and an independent integer
exponent. Kinetic, variance and shared-energy products use normalized factors
throughout. Their significands still incur ordinary binary64 multiplication/
division rounding; this is not an arbitrary-precision thermodynamic model.
Binary64 projection is explicit and flags subnormal/below-range rounding.
Strict narrowing rejects any such loss; overflow and exponent/storage exhaustion
remain typed numerical-precision failures.

Additive sums retain nonoverlapping components with their own exponents. Two-sum
operates on aligned normal significands when the exponent gap is at most 54;
larger gaps retain both operands directly. Grow-expansion addition preserves
every represented additive operand, including through large cancellation.
The four inline components avoid a heap allocation in small expansions; extra
components use bounded storage, with a 4,096-component limit. No allocation or
production-performance claim is made.

Error-free expansions are described by
[Shewchuk](https://www.cs.cmu.edu/~quake/robust.html). The independent-exponent
adaptation and domain checks here are this implementation's choices and need
their own validation; the referenced work does not certify this code.

An energy account retains the signed sum and sum of absolute operands as exact
additive expansions. Its comparison against 128 binary64 epsilons multiplies
the latter by exactly 2^-45 and compares expansions, without a unit floor or a
projected zero. A collapsed value is only a rounded estimate; exact additive
components remain available for data review.

The read-only batch-entry guard requires x86-64 binary64 arithmetic, nearest
rounding, FTZ/DAZ off and masked SSE exceptions. It changes no host setting.
Private tests temporarily exercise unsupported modes and restore their process
environment; this is not a game/user configuration change.

Seven controls pass: finite binary64 roundtrips; derived K/variance/
parcel products including the original underflow; cancellation and 32 generated
80-operand expansions; exact relative accounts above/below binary64 range;
explicit projections and strict narrowing; ten invalid/overflow cases with a
held-sum identity; and five unsupported arithmetic modes plus restoration.
The independent reviewer uses exact fractions to check products within eight
epsilons, shares within four, exact equality of full additive expansions and
exact account classification. Altered parcel, missing component and hidden
projection-loss records must reject.

Fresh Windows/MSVC and Linux/GCC15 strict builds and all seven native groups
pass. Their separate exact-fraction reviewers each pass 21,019 checks and
reject all three altered parcel/component/projection records. The 4,147 native
output lines are identical after line-ending normalization. The original
9.12e-436-J parcel is retained as a positive represented energy while its
binary64 projection is explicitly marked unresolved.

Source freezes, compiler approvals, executable/archive identities and first
EOF-checked captures are retained in
`local/occupied-staggered-validation/mac-energy-{windows,linux}-r1`.
Completed-review SHA256 values are
`2b98a9364b2ca268c3acb8fb1fbe5f4bad0b741cb89968618c84de493c67bd8b`
(Windows) and
`c0e83433c8ec3aa0762239b30861ad0e4c4f32a41e6fc8abdddbea50acdba5c4`
(Linux). No independent agent review is claimed.

Bounds are aggregate build 180 s, native/review 30 s each and 512 KiB
per raw pipe. No old failed physical gate is replayed for these standalone
controls. The later transport/closed-owner integration and all existing
prerequisite controls also pass; its relevant Linux strong-flow retry now
rejects an unrepresentable primary helium parcel. Constituent/passive/primary
momentum checks retain their existing rules. The standalone source cohort above
did not change transport/owner code; the later integration is separate evidence.
The default game DLL is unchanged. This component alone cannot establish a
strong-flow, production or game pass.
