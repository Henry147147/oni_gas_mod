# Mixed Atmospheres 1.12.1.1

This hotfix addresses a crash during manual saves and autosaves. The
November 21 screenshot reported 106 pending pipe-body heat messages after
64 zero-time frames.

ONI sends a body-energy message only when the magnitude of the clamped
contact work exceeds 0.000001 kJ. Rounding can still change the temperature
of a small pipe packet below that cutoff. Version 1.12.1 used that change
to predict a message, then waited for it when saving. The hotfix applies
the game's cutoff before registering the expected message and uses the
rounded carrier temperatures to identify genuine messages. A contact that
sends no body work credits no body energy to the pipe inventory.

Existing colonies can load directly. The package uses the native libraries
of 1.12.1 and the same save format. The 1.12.2 performance work is separate.

## Verification

The crash-session log records successful autosaves for cycles 46 through
49 and the failure on the next day boundary. The November 21 manual save
and the separate copy supplied with the screenshot have the same SHA-256.

On game build 764025, the Windows player under Proton reproduced 20 pipe
contacts that changed the carrier temperature below the native sender's
cutoff. With the hotfix, the save barrier settled at frame 35. The pipe
energy residual was exactly zero and the game wrote its save. The cutoff
was also checked against the Linux and Windows native senders for builds
744825, 763796 and 764025.

Using the player's Cycle 49 autosave on Windows under Proton, the release
package restored frame 143777 with 84,809 owned cells. ONI wrote the natural
Cycle 50 autosave (4,423,246 bytes) and continued to frame 146924, where a
manual save also succeeded. The supplied save stayed intact. A 600-tick
control on the earlier manual save also saved successfully on 1.12.1. The
original crash happened 12,782 ticks after that file's opening frame.

The full standalone build and test run passed, including 94 native tests.
All 90 deliberately incompatible game-contract checks refused the modified
contracts across the six game/platform combinations. All twelve distributed
native libraries match the 1.12.1 release byte for byte. Both ZIP archives
and the unpacked Workshop folder were verified. The private upload folder
is `publish/update-1.12.1.1/MixedAtmospheres` (build `3a7ebfd`).
