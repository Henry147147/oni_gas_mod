# Completed native publication leases

The bounded native domain and ONI's exported `GameDataUpdate` can describe
different completed frames. The native worker finishes a fluid frame before it
copies simulation data into a game buffer. A managed status read can wait for
that worker and observe a newer frame than the buffer it already received.
Comparing the newer heat journal with the older solid temperature reports a
false energy loss.

An actual Linux Unity reproduction confirmed this ordering. The completed
native old and updated solid arrays both held 370.0952453613281 K, while the
exported buffer still held 370.0953063964844 K. The registered 10 kg solid had
capacity 7900.000214576721 J/K, so the native temperature drop accounted for
the journal's entire 0.4821777474717237 J. The exported buffer accounted for
zero. The unchanged endpoint tolerance was about 1.662e-7 J.

## Source binding

Two separately fingerprinted native procedures identify the actual source:

| Procedure | Linux RVA | Windows RVA | Binding |
| --- | ---: | ---: | --- |
| `SimBase::CopySimDataToGame` | `0x58600` | `0x32820` | After the native copy, retain the completed physical cells and heat observations under the opaque destination identity. |
| `PrepareGameDataUpdate` | `0x32530` | `0x67c0` | Bind the returned packet to the retained source on the calling thread. |

Both signatures come from the licensed build-744825 Linux symbols and the
matching Windows PDB. The copy runs under the existing fluid frame fence.
The mod forwards both native calls and reads no private `GameData` or native
STL layout. No solid temperature, native copy, heat command or numerical
tolerance changes.

These are additive `publication_bindings` in
[the checked profiles](native-hook-profiles.json). Observer ABI 4, its 29 public
hook indices and its 528-byte status remain unchanged. Attachment verifies the
new entry fingerprints and includes both detours in the trampoline capacity
check. Older observer-only profiles can omit the bindings and advertise no
publication capability.

## Reader ABI

[`oni_publication.h`](../native/include/mixed_atmospheres/oni_publication.h)
defines publication capability version 1. A caller acquires immediately after
the native export returns, on the same thread and before its next native
publication query. The packet must match that thread's last binding. A buffer
that predates ownership returns `MA_ONI_BUSY`.

The 64-byte lease information names a stable token, owner session, completed
frame, native epoch, cell count and physical time. Copying a lease returns the
matching physical status, immutable cells, solid heat account, last contact
and native solid observation together. Mixed-frame metadata or stale native
observations reject before publication. A borrowed cell view requires no
allocation or native simulation read; it remains valid until release. Callers
must retain the token while reading and release it exactly once.

The source cache holds at most eight native buffer identities. At most sixteen
leases may be outstanding. Replacing a source, eviction, owner restart and
future simulation steps leave acquired leases intact. Restart invalidates
unacquired bindings, including bindings on other threads. Tokens are never
reused. Released or unknown tokens reject without changing caller outputs.

This reader retains the development domain's 4096-cell and five-constituent
bounds. It is an intermediate reader for existing native integration, not the
catalogue-driven production colony ABI or whole-world publication mechanism.
It copies the bounded physical publication at native export time; it does not
establish the colony-scale memory or publication performance gates.

## Solid heat consumer

The managed solid heat control compares an exported temperature with its
leased source heat journal. Input reservations and new commands still use the
current physical owner. An unowned activation buffer cannot supply acceptance
evidence. Completion waits for a released lease whose frame, native ticket,
clock and every fluid cell match the final paused owner. Zero-time native
exports participate in draining that final buffer.

The portable evidence reader requires this source identity, exact matching
frame and clock, released lease and final cell equality. The endpoint energy
bound remains 128 binary64 epsilons over the actual operands. The original
native arrays remain untouched by this reader.

## Verification

The standalone lease test covers future source replacement, immutable retained
views, cache and lease limits, wrong packets, mixed-frame observations,
duplicate release, two simultaneous readers and a cross-thread owner reset.
The exported C ABI test checks rejection without output mutation. The licensed
native worker control copies a real acquired frame and checks its borrowed
cell view, journal and solid temperature against the exported native buffer.
The same control passes with the Linux library and the Windows DLL under Wine;
Wine evidence is distinct from actual Windows OS and Unity evidence.

Source/save/reload Unity acceptance is tracked with
[the solid heat integration](native-domain-physics.md#native-solid-cell-heat).
The Linux source/save and fresh-process reload pass 121 and 120 contact-bearing
frames respectively, with zero published-temperature energy residual and 120
exact resumed input replays. The paused frame-zero resave also passes. The
Linux Release and Ubuntu 22.04 suites pass 38/38, the two changed C ABI/lease
tests pass fatal ASan/UBSan with leak detection and MinGW/Wine, and the Python
suite passes 297 tests with four skips. Actual Windows OS acceptance of these
new bindings remains open.

WindowsPlayer under official Proton 10.0-4b passes the same source/save and
fresh-process reload with MinGW-built DLLs: 121/120 contact-bearing frames,
zero endpoint residuals, a paused frame-zero resave and 120 exact resumed
input replays. The first attempt failed before launching ONI because the
selected Proton tool was nested under the sandbox's read-only Steam mount;
the successful run uses the separately staged installation. The private
Windows host still refuses SSH, so this does not add Windows OS/MSVC evidence.
