# Immutable saved-colony layout tests

This standalone tool runs **74** original synthetic groups against seven directly linked product helpers. It needs no game installation, captured save, vendor codec, Unity assembly or licensed binary. Fixtures are generated in memory by the original independent wire writer.

```text
dotnet run --project tools/OwnedReservoirLayoutTests -c Release
dotnet build tools/OwnedReservoirLayoutTests -c Release -p:LayoutTargetFramework=netstandard2.1
```

The default target is a net10.0 executable. The netstandard2.1 contract is a library containing exactly the seven product helpers; it excludes all four fixture/test sources. Both builds require zero warnings and errors. The host accepts no arguments or one fresh receipt path. It stops at the first unexpected failure, prints 74 ordered PASS lines followed by its JSON summary on complete success, and creates an optional receipt without overwriting an existing file. Windows, Linux and game Mono require separate runtime validation.

The inspector starts at byte zero of one immutable complete image. It derives the bounded native header, count-framed saved-template directory, exact remaining zlib fragment, body prefix and KSAV object section. It supports the explicitly pinned build 744825, header version 1, compressed object-save 7.38 profile. Invalid framing and configured limits produce typed rejection; unsupported profiles reject explicitly. Decoder Stream.Position or read-ahead is never used as framing proof, and later magic bytes cannot supply fallback boundaries.

The 39 layout/malformed/immutability/concurrency groups and 35 resource groups retain all original names, assertions, wire bytes, independently computed ranges and Adler controls. The writer manually emits stored DEFLATE blocks and includes fixed RFC and malformed-history controls. It does not invoke a compressor, another inspector, a native reader or an actual captured save to obtain expectations. Resource groups exercise all 33 caller scalar dimensions, their constructor bounds, the fixed header ceilings, exact compressed/output ranges, decoder granularity/read limits and structural budgets. Caller/exported buffers and readonly collections cannot mutate pinned observations; parallel calls must retain private results.

The header facts API remains independently linkable using primitive facts and its existing inspection type. The layout layer owns image ranges and typed failure conversion. Existing header entry points retain their behavior. No default game hook, load permission or restore path is activated.

Native Sim bytes, custom components and terminal tail remain opaque. Recognized Storage traversal is bounded; arbitrary descendants and owner/checkpoint reconciliation are outside this tool. A recognized owned declaration or assigned marker supplies structural evidence without authentic ownership or permission. The asserted report flags keep whole-file preflight, native validity/load, owned restore, writer permission, quarantine activation and first-playable acceptance false. Successful synthetic inspection does not grant any of those authorities or establish physical conservation, gameplay persistence, performance or actual Mono behavior. The managed decoder may use its platform compression implementation; NativeInvoked=false means no ONI native Sim API invocation.
