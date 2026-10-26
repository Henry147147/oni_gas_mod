# DLC compatibility controls

The 2026-10-26 request covers Spaced Out!, Frosty, Bionic, Prehistoric and
Aquatic: every one of their 32 selections on Linux and Windows x64. This
extends the [base-game playtest](playtest-plan.md); it does not certify the
remaining content adapters or change any accuracy gate.

`tools/oni_dlc_matrix.py plan` prints the 64 OS/content rows, initially
unrun. The mask's fixed bit order is:

| Bit value | Content | Game ID |
| --- | --- | --- |
| 1 | Spaced Out! | `EXPANSION1_ID` |
| 2 | Frosty | `DLC2_ID` |
| 4 | Bionic | `DLC3_ID` |
| 8 | Prehistoric | `DLC4_ID` |
| 16 | Aquatic | `DLC5_ID` |

Mask 0 selects the base game; 31 selects all five. The empty string is the
game's vanilla ID. The text `VANILLA_ID` is invalid.

## Running a selection

The Linux and Proton harnesses accept `--dlc-mask 0..31`. They set all five
ordinary menu preferences in a fresh private profile and enable the test mod
for every content ID. The Windows runner accepts `-ColonyOwner -DlcMask N`;
it preserves unrelated preferences and restores the original file afterwards.
Steam and the game retain their normal ownership checks.

The managed development request records `DlcMask`. Before world generation
or loading, the game compares it with `DlcManager.GetActiveDLCIds()`. A missing
pack, unknown ID or different selection fails the run. The host also checks
the actual runtime and colony content IDs; writing preference flags alone
cannot establish coverage. Existing requests without a mask retain their
previous selection behaviour.

From a frozen checkout and an immutable private package, run one target:

```sh
python3 tools/oni_dlc_matrix.py run --target linux \
  --runner-source /private/frozen-runner --package /private/linux-package \
  --output /private/new-linux-evidence
```

Use `--target windows` on Windows with Python and PowerShell 7, or
`--target proton --proton /private/installed-proton` on Linux. The Proton
route is additional WindowsPlayer evidence; it cannot fill Windows OS rows.
`--masks 0 20` limits a diagnostic run to those selections and cannot pass
full target coverage. On Linux, each individual game goes through
`/home/henry/oni-agents/game-slot.sh high`, releasing the slot between phases.
`--game-slot` can name the same shared wrapper in another home directory;
a missing wrapper refuses the run. Queue time does not consume the inner
game's timeout. Run the outer matrix directly: putting the whole matrix under
one game-slot call would keep the release chain waiting across several games.
Builds and standalone checks use `run-locked.sh high` separately.

For each selection, the runner creates a new colony, saves it, loads that
source in a fresh process, and tests an in-process return through the menu
and second load. Spaced Out! rows require more than one world. Every phase
requires a running `MacClosedWorld`, closed mixed-pipe audits, the expected
content IDs, and successful game-side completion. Loads must reencode the
restored owner image; menu loads must verify teardown and the menu boundary.
The source save must remain unchanged. Runner/package hashes are checked
before and after each phase. The first rejection stops the matrix and leaves
later rows unrun, preserving the failure and source receipts in `matrix.json`.
Saves, runtime reports and captures stay private.

## Remaining exits

Standalone checks pass: 86 Linux Release CTests, 371 Python tests (four
skipped), and 227 managed boundary scenarios. The adapter compiles against
both installed game assemblies; the Windows script passes PowerShell parsing
and refuses execution on Linux. No new DLC game run has been accepted.
Spaced Out! still explicitly
refuses startup. Multi-asteroid ownership, transfers, rocket interiors,
bionic oxygen and aquatic ecosystem adapters remain to be implemented and
verified before their rows can pass. The matrix is a compatibility check,
not proof of every pack's gameplay systems.

Exact stored-fluid heat/biology and schema5 cold rebinding remain open in
[mixed pipes](mixed-pipes.md). In particular, queued absolute thermal proxy
resets have a measured energy-creation defect; ordinary reader/storage
controls do not close it. Native pipe schedule capture exists, but live
schema5 saves remain gated. A restored image's exact reencoding proves
restoration only; the matrix explicitly leaves byte-exact **active**
continuation unverified. The [F3 levelling and F4 conduction proofs](base-game-floor.md)
also remain open, with their original grids, metrics and bounds.
