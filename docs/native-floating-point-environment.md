# Native solver arithmetic and caller restoration

The native solver runs inside ONI's process, so it must establish its arithmetic
environment on every executing thread and restore the native caller afterward.
`ArithmeticScope` preserves the complete floating-point environment and SSE
controls, selects nearest rounding and gradual underflow, and masks exceptions
for the solver's checked operations. Exit restores the original rounding,
exception flags and SSE controls, including failure paths.

GNU x86 builds additionally select the extended x87 precision advertised by
their `long double` type. This matters to the EOS root, which uses that type
internally. Setting nearest rounding alone leaves x87 precision inherited from
the game. The precision setting is local to the scope; original nonfluid ONI
code receives its original control word. MSVC's `long double` representation
does not use this GNU x87 precision selector.

## Reproduced water continuation difference

The `9a1a894`/v13 Windows water save loads twice in each installed player, with
120 individually exact local actor-and-fluid frames per session. Its complete
initial state, actor records, model, geometry and clock transfer unchanged.
However, comparing the platforms' evolved water states fails the strict 1e-8
stored-quantity bound, including different face momenta. This is retained as
a failed cross-platform continuation comparison.

Controlled replays of that exact checkpoint reproduce each platform's Unity
result byte for byte when the caller selects 53-bit x87 precision. Default
extended-precision replays produce different final states. This establishes
an inherited-precision dependency; it is not a direct register capture from
Unity. Earlier scope tests covered rounding and SSE controls but missed x87
precision.

The corrected scope has a dedicated `native_oni_arithmetic_environment` test.
A generated nonuniform water checkpoint continues through 16 identical frames
under both GNU caller precision settings, downward caller rounding and enabled
FTZ/DAZ. It preserves original exception flags and every caller control on
successful and rejected input. The original v13 library fails this synthetic
precision-independence comparison at its first frame. Both fresh Ubuntu
compatibility and Windows-binary suites pass all 14 native groups.

The actual saved water image also replays through all 120 intervals identically
under both caller precision settings with the corrected libraries. On each
platform the results match the original library's extended-precision replay.
Linux ASan/UBSan also passes all 14 native groups. Frozen `15db23a` packages
with the v14 libraries then pass actual WindowsPlayer/Proton OpenGL and native
Linux reloads of the same Windows water save. Each player completes two native
sessions with paused frame-zero autosaves, three live actors, matching consumer
callbacks, native orbital admission, readers and calibrated GPU readback. Every
one of the 120 complete actor-and-fluid frames per session matches its local
replay. The Windows final image is
`fac036d847d61a0e380c599e505b5eee0641bfe9ab8e7f95648889b62bd7f111`;
the Linux image is
`284c01cf758d605d59f79aa445cb3615b7046013f86e3d6c179b2e9b38c429f3`.
Both exactly match the independently predicted extended-precision images.

The same corrected Windows library also reloads the actual live-actor natural
day autosave twice, with 120 exact complete frames and paused autosaves in each
session. Its final image is
`4bad864624ca59dedb018f0b5412779fb6963a59103c87a82a3bb372e8c963ff`.
The seven-case saved-receipt review, covering these controls and their original
water/day sources, passes 88 checks; SHA-256
`9b378dc21153abfa82994b79c0d94995e0860c2cfcdc9e882cff86597d5fba95`.

The strict cross-platform water bound remains failed: the two extended-precision
replays still differ, with maximum stored-momentum relative difference about
2.1e-7. The scope repair removes dependence on the caller's precision; it does
not establish agreement between the math libraries, actual Windows OS execution
or whole-colony ownership.
