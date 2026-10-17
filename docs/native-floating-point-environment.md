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
successful and rejected input. Linux and Windows-binary controls pass. Full
native rebuilds, mutation checks, sanitizer and actual Unity verification are
still in progress. The strict cross-platform water bound remains open; the
scope repair alone does not establish that the two math libraries agree.
