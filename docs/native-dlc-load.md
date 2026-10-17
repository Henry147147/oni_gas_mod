# Native Linux Spaced Out load and continuation

The bounded fluid owner now passes an ordinary Spaced Out colony load on the
installed Linux game. This uses the existing licensed Linux installation's
expansion bundles through `oni_headless.py --game-dir`; the normal save-content
check remains mandatory. A depot missing those bundles still rejects this same
save before activation. No assets are transferred between platforms.

The actual `spaced-out-installed-linux-load-v1` uses frozen adapter and runner
`a91217b`, compatibility native build v11, ONI 744825 and Steam build 24423041.
The Linux assemblies and SimDLL match the preceding base-game controls.
Content admission reports `EXPANSION1_ID` available, and all nine native worlds
load. The six owned water cells occupy their saved location in that cluster;
the surrounding colony remains native ONI.

Both loads run in one Unity process. Shutdown and the main menu occur between
native sessions 1 and 2. Each load completes the two native startup batches,
holds the initial pause, writes an ordinary frame-zero autosave, verifies its
complete serialized body/header and advances 120 individually checked physical
frames. Both sessions pass the restored pressure and breathing readers and the
independently calibrated shader-bound GPU check. The original input is
unchanged, and the private desktop is retired.

The restored physical image is
`a447df9cb4f07c859dfeb8a6a3fb8ef7db6868628b5a14925098ce036a5ecf79`.
Both final images equal
`641d1e3402d3af084a320d7c011f79e2ad28e43c9aa49e09235d5aa364836ea6`.
The second complete save body contains 10,601,921 bytes. Its file SHA-256 is
`0cd7e1bdcb418bfbb4c5defcaf65ff9bfba8ff46f454b50b774d36793b95bae7`.
The raw result is
`a081120bc28f1df9969cc60b639ed9cf5cde932b9cd240751b5c1a3888398f52`;
the independent portable review is
`23f1b970fb34ec67cc43369f4c4ad55043ce2fa71d82c416056e168a1a594e74`.

Each resave logs `Missing KPrefabID reference: 2`. The same warning occurs in
some preceding base-game saves. Native `Ref<T>` logs it when a saved object
identity cannot be found, then resets that reference. Exact fluid continuation
does not establish native object-reference integrity, so that remains an open
investigation. This run has no separate manual image review. It does not prove
all gameplay packs, live-actor persistence, arbitrary fluid ownership or a
playable nine-world overhaul.

Related controls: [GPU readback](native-gpu-readback.md),
[reader lifecycle](native-reader-restore.md) and
[the remaining implementation gates](completion-plan.md).
