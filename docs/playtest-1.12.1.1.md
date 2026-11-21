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
