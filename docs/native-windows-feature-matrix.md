# WindowsPlayer native import and reader verification

Frozen `2df8e7b` with native v15 passes all twelve real Unity source/load
controls in `windows-current-native-import-and-reader-matrix-v3`. The installed
ONI build is 744825, Unity 6000.3.5f2. WindowsPlayer runs through official
Proton 10 with OpenGL on Linux; this is not evidence of execution on Windows OS.
Each source starts a fresh nine-world Spaced Out colony. Each load uses a fresh
game process and its source's complete saved file.

| Scene | Source behavior | Fresh load |
| --- | --- | --- |
| Gas import | Imports completed native gas masses and temperatures before fluid exclusion | Paused frame-zero autosave, 120 exact complete frames |
| Water import | Imports completed native water masses and temperatures | Paused frame-zero autosave, 120 exact complete frames |
| CO2 particle | Actual `CO2Manager.SpawnBreath`, one 0.03125 kg receipt, original fluid dispatch avoided; particle retires and is reused | Paused frame-zero autosave, 120 exact complete frames |
| Breathing reader | 24 actual owned reads through the native breathing helper, with unchanged outside behavior | Paused frame-zero autosave, 120 exact complete frames |
| Gas pressure | Six physical pressure getter samples, five inspected caller patches, 824 game call-site reads and unchanged outside behavior | Paused frame-zero autosave, 120 exact complete frames |
| Water pressure | Six compressed-water pressure samples and unchanged outside behavior; caller patches verified, no game call-site reads observed in this scene | Paused frame-zero autosave, 120 exact complete frames |

All twelve cold starts pass the final nitrogen `Tag.Name` setter identity
checks. Each complete native save passes body/header readback and the strict
orbital save-root registry gate. Each reload retains its source physical image
exactly before advancing, checks complete state on every continued frame, and
passes native readers, calibrated shader-bound GPU readback and pause-depth
restoration. These results supersede the earlier interrupted matrix's Windows
startup gate; failed earlier runs remain retained.

Matrix SHA-256:
`3db5ff7e08489db0912ef7ca42cde4c08b5f742ea86a051da6922b73368c466f`.
The independent saved-receipt review checks all twelve raw-result hashes,
clean package provenance, the portable physical/reader/GPU gate, actual saved
file hashes, native references and six source/load physical-image links.
All 72 checks pass. Review SHA-256:
`6c45d59de29ad245f970b66bc3f22a72df03e9dc4534c0c79eef1fcceb750ce6`.
Private saves, licensed content and full receipts remain outside the repository.

This accepts the bounded incoming reader/import/particle paths. It does not
establish complete metabolism, production machinery, every native reader,
whole-colony fluid ownership or all-DLC progression. The complete overhaul
continues under the [completion plan](completion-plan.md).

Related contracts: [native import](native-initial-inventory.md),
[breathing](native-breathing-reader.md), [pressure](native-pressure-reader.md),
[nitrogen startup](nitrogen.md) and [save references](native-orbital-references.md).
