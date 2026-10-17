# Linux native trampoline storage

The Linux observer installs all 28 verified SimDLL hooks using a bounded buffer
in the loaded module's unused executable-page padding. This is part of native
backend startup. It removes the need for a new mapping within the x64 relative
jump range when Unity has already occupied that address space. Windows keeps
its independent hook allocation and platform profile.

The pinned Linux image has an RX load segment at RVA `0x2a000`, with file and
memory size `0x7a1b9`. Its allocated bytes end at `0xa41b9`. The aligned range
`0xa41c0..0xa5000` contains 3,648 zero bytes outside every load segment's
allocated contents, inside the final privately mapped executable page.
This holds 50 entries in the pinned Funchook x64 layout; startup requires at
least 28 and checks that every prepared hook used this buffer.

`oni_hook_profiles.py` derives this metadata from the licensed ELF program
headers and file padding. The checked-in profile contains only boundaries and
hashes. The observer checks the complete native and managed file hashes,
loaded entry fingerprints, actual loaded program headers, zero-padding hash,
system page size and actual private RX mapping before registering the buffer.
Writable segments, BSS, live instructions, overlapping load segments, nonzero
padding and other page layouts reject activation. Future game builds require
new inspected profiles.

The hash-pinned Funchook extension limits each borrowed page to its actual
buffer size. While that buffer is registered, exhaustion or an unreachable
target rejects preparation; it cannot silently allocate another page. Writes
temporarily give the containing page read/write/execute permissions, retaining
execute access to native code that shares the page. Installed trampolines are
read/execute. Destruction restores every padding byte to zero and restores RX
permissions before releasing registration. It never unmaps a game-image page
or changes the installed game file. A failed restoration retains ownership and
reports an error; the observer requires process restart if cleanup cannot be
verified.

For unregistered synthetic Funchook users, the existing gap scanner still uses
exact `MAP_FIXED_NOREPLACE` claims and rejects ignored claims. It never
overwrites occupied Unity mappings. Both modifications ship as source patches
with the original dependency archive and linking exception.

The deterministic regression first denies every nearby allocation and requires
ordinary preparation to fail without changing native code. With borrowed
storage it executes and restores short-branch trampolines for 96 complete
cycles, with no nearby claims or image unmaps. A one-entry buffer rejects a
second target. Invalid buffers, active rebinding and a failed final `mprotect`
exercise bounds and cleanup errors. All page bytes and actual RX permissions
are checked after successful cleanup. Loaded-segment and ELF-profile controls
also reject malformed boundaries and overlapping segments.

The earlier `liquid-actors-and-native-features-unity-v1/gravity-save` failure
remains recorded. It rejected hook 0 before simulation after observing 2,610
regions and no reachable address gaps; ten earlier Unity save/load cases in
that matrix passed. Synthetic controls alone do not establish a completed
game matrix or whole-colony takeover. Real Unity evidence is recorded
separately for each frozen package.

The initial implementation passes all 13 native CTests on Linux, all 13 with
ASan/UBSan, all 13 Windows x64 CTests under Wine, and 164 Python checks with
four platform-specific skips. All 13 licensed Linux native domain controls
also pass, including attach, worker execution, shutdown, detach and exact
checkpoint continuation. These isolated native runs use the real SimDLL but
do not launch the Unity game player.

The frozen `a5aa2de` runner/package and Ubuntu 22.04 compatibility build v8 now
pass actual Linux Unity gravity save/reload and registered gas-actor
save/reload. Each source reaches frame 122 / 24.400000364 s; each fresh process
performs its paused initial autosave and matches all 120 resumed physical
frames through 48.400000721 s. The gravity source physical hash is
`c6694fe2cee654da3026be1ef1e57c9b69ce5a0a3305c905f5b9b13bfc6b682d`,
with colony-file hash
`87d23a0da3cec6dda264ce6b225ef12244b673ee45350d47fcd466a7d0209ebb`.
The actor source physical hash is
`c1d280df4139cb633827b6ffa7f95eaa2903cfe94794d902a4149513aade19b5`.
These four completed cases belong to
`verified-trampoline-unity-matrix-v1`; the remaining matrix cases are still
running. All 13 licensed Windows SimDLL domain controls also pass under Wine;
those do not establish Windows Unity verification of this package.

The later clean `eae30f9` merge is rebuilt with native MSVC on Windows. All 13
native CTests pass, followed by an actual Windows Unity compressed-water
heat/save run through 121 completed frames. The save's complete header/body
readback and profile restoration pass, and actual hover retains the corrected
liquid pressure/status. This exercises the separate Windows allocation and
cleanup path after integration; it supplies no additional Linux borrowed-buffer
evidence. See [the Windows record](sealed-native-domain.md#current-windows-conservative-liquid-heat-and-reload).
