# Steam Offline Mode for parallel playtesting

Native Linux ONI still needs the licensed Steam installation. Klei lists
[Linux support through Steam](https://support.klei.com/hc/en-us/articles/360029880271-Oxygen-Not-Included-Minimum-System-Requirements),
including [the DLCs](https://support.klei.com/hc/en-us/articles/360052990312-Do-Oxygen-Not-Included-DLCs-Have-support-for-macOS-and-Linux).
No publisher-supported native Linux standalone route was established.
The standalone solver tests and captured-input replays need no Steam.

[Valve's supported Offline Mode](https://help.steampowered.com/en/faqs/view/0E18-319B-E34B-B2C8)
allows repeat launches without an online Steam session after initial setup:

1. The user signs into the private client with Remember me enabled, updates
   the game and launches it online at least once. The private Steam home
   stays at `~/.local/share/oni-headless`; do not move it.
2. With the game closed, choose **Steam → Go Offline** in that client.
   Confirm **OFFLINE MODE** appears in Steam's window. Friends → Offline or
   Invisible only changes presence and does not perform this step.
3. Keep that client offline while testing here; use the online Steam client
   on Windows. Launch games through `tools/game_slot.sh` as usual.
4. Coordinate before bringing the Linux client online for updates. Close
   Steam normally so its cached offline information is retained.

If Steam asks to refresh sign-in, Valve says Offline Mode cannot bypass
that requirement: the user must sign in first. The harness never enters
credentials or bypasses license checks. Its `steam_offline` helper sets
friends presence only; it does not establish an offline account session.

## Verification, 2026-11-10

The private client initially displayed “This account logged in on another
computer” and required refreshed sign-in. After the user signed in, selecting
Steam → Go Offline displayed OFFLINE MODE. The unmodified native Linux game
then reached its main menu with the mod and license checks active:
`artifacts/liquid-instability-2026-11-10/offline-menu/result.json`,
`passed-scoped-main-menu`, `steam_blocked: false`.

The native colony owner also starts and advances in Offline Mode. The final
liquid/flood/airflow/freeze-melt batch passes all 17 checks over 1,787 owner
frames, with no authentication block and closed mass/energy accounts:
`artifacts/liquid-instability-2026-11-10/cadence-linux-batch/result.json`. The user
reports Steam running successfully on Windows during the Linux run. This
confirms client coexistence; simultaneous Windows ONI simulation was not
independently observed. No zero-login fresh setup is claimed. The full private
playtest analysis is stored beside the supplied recording in
`Playtest-review-2026-11-09.md`.
