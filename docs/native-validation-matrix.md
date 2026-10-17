# Native Unity save/load matrix

`tools/oni_native_matrix.py` runs the implemented bounded native scenes in
fresh Linux Unity processes. Every scene writes an ordinary paired colony
save, then another process reloads that file, runs the initial paused autosave
control and compares each resumed physical frame against independent
continuation. This is a regression matrix for development scenes; first-playable,
whole-colony and release readiness remain false.

Use a frozen runner checkout and an already built package. The runner and
package must remain unchanged until the matrix finishes. Another developer can
continue pushing to `main` while these frozen inputs run.

Runner Git metadata is recorded only when the runner directory itself is a
checkout root. An archive inside another changing checkout reports null Git
metadata; the enclosing repository's HEAD cannot identify its source. The
recorded runner file hashes and package revision/hashes remain independent.
Earlier archive cohorts may contain inherited runner Git fields: use their
frozen file hashes and recorded archive creation revision for source identity.

```sh
git worktree add --detach /tmp/oni-native-matrix-runner HEAD
python3 tools/oni_native_matrix.py \
  --runner-source /tmp/oni-native-matrix-runner \
  --package /absolute/path/to/built-probe \
  --output artifacts/native-matrix-001 \
  --frames 120 --timeout 1800
```

The output directory must be new. The default selects gas, compressed water,
water point transfers, gas/water heat, gas transfers, carbon dioxide,
conduction, gas/water gravity, gas/water registered actors, physical
checkpoints, paired capture and source-save controls: 30 game
processes including reloads. Use `--scenes water gravity` to select cases.
Gas and water actors need at least 80 frames; source-save controls need at least 16.
`--timeout` applies separately to each game and accepts up to 7,200 seconds.

`matrix.json` records hashes for both Python runner files and every packaged
file, each child result's hash, physical counters and checkpoint receipts.
Changed frozen inputs, an unsuccessful exit, a missing/failed result, a save
outside its disposable run, a mismatched source receipt or a changed source
file abort the matrix. Completed reports and logs remain intact, and the
matrix is marked successful only after every selected save and reload passes.
Eleven orchestration regressions exercise these failure paths without launching
a game.

For the Windows Unity player on a Linux host, add `--proton /path/to/proton`
and `--game-dir /path/to/windows-game`, and supply a Windows probe package.
The [Proton runner](proton-game-validation.md) creates one fresh prefix per
game. Windows reports retain their C:/Z: paths; the matrix loads only the
runner's authenticated host save inside the corresponding evidence directory.
Both raw and host file hashes must match. The extra runner/imported scene map
are frozen too. Reports identify WindowsPlayer on Linux and keep
`actual_windows_os: false`; a successful Proton matrix cannot certify the
Windows OS or the full overhaul.

The initial private `public-matrix-save-workflow-v1` smoke cohort passes both
actual Unity processes with 30 frames per case. Its source at frame 32 /
6.400000095 s retains physical SHA-256
`f77114eb68221b67bc82254488027c0f9128da901851cb52b7bc356c94c6b30b`;
the colony file is
`328a89474408426dda00a42e84e53d8865d42481d3cb55191d77993b7770b417`.
The fresh-process load preserves that initial image, passes the paused positive
interval challenge and header/body readback, and matches all 30 continued
frames. This verifies the orchestration; the full 22-case matrix remains open.

The frozen 22-case `full-native-feature-matrix-v2` cohort passes gas save/reload
and water save/reload, including 120 exact continued frames on each reload.
It stops before launching the heat scene when Steam exits; the missing-result
failure is retained. Remaining scenes and the newly added water-transfer case
require a live authenticated session and fresh evidence directories.

The separate `liquid-point-transfer-unity-matrix-v1` cohort passes both actual
Unity water-transfer processes using frozen `0e6dc89` inputs. Its source saves
at physical frame 122 / 24.400000364 s after both actual mass callbacks, and the
reload matches all 120 frames through 48.400000721 s after the paused autosave
challenge. The ordinary save/body receipts and complete physical hashes are
recorded in the [liquid evidence](sealed-native-domain.md#filled-liquid-point-sources).

The `liquid-actors-and-native-features-unity-v1` frozen `2894420` / compatibility
v6 cohort passes water actors, gas heat, gas transfers, carbon dioxide and
conduction through both save and reload, including all 120 resumed frames.
Its gravity source process stops during mod loading: no free near trampoline
page is found (`regions=2610`, `gaps=0`, `attempts=0`). The mod disables itself
without entering simulation. That first failure and all ten completed cases
are retained; gravity, gas actors, checkpoint, pair and source-save cases are
still required. Later heat-rounding edits do not change these frozen inputs.

The separate `conservative-heat-unity-matrix-v1` frozen `67e1475` / compatibility
v7 pair passes actual liquid heat, ordinary saving and fresh-process reload,
with every one of 120 continued frames matching. The later
[same-process reload](native-menu-reload.md) also passes both loaded sessions
in one Linux Unity process; it does not replace the incomplete feature matrix.

The separate actual Windows `windows-native-domain-current-r1` and
`windows-native-current-reload-r1` cohorts pass current conservative liquid
heat/save, fresh loading and two loaded sessions through the main menu in one
process. Each load matches all 120 continued physical frames. The follow-up
`windows-liquid-inspection-r1` cohort repeats exact continuation and verifies
supplied fluid pressure and liquid breathing status in native hover/details.
See [the scoped Windows results](sealed-native-domain.md#current-windows-conservative-liquid-heat-and-reload).
These runs do not complete the remaining Linux matrix cases or the full-colony
and liquid fill rendering gates.

The [Linux runner](sealed-native-domain.md#run-the-actual-linux-colony-controls)
requires the licensed game and the private desktop/Steam setup. The separate
[Windows runner and evidence](sealed-native-domain.md#additional-windows-feature-controls)
cover actual Windows Unity. This Linux matrix cannot supply Windows evidence.
