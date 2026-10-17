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

```sh
git worktree add --detach /tmp/oni-native-matrix-runner HEAD
python3 tools/oni_native_matrix.py \
  --runner-source /tmp/oni-native-matrix-runner \
  --package /absolute/path/to/built-probe \
  --output artifacts/native-matrix-001 \
  --frames 120 --timeout 1800
```

The output directory must be new. The default selects gas, compressed water,
heat, transfers, carbon dioxide, conduction, gravity, registered actors,
physical checkpoints, paired capture and source-save controls: 22 game
processes including reloads. Use `--scenes water gravity` to select cases.
Actors need at least 80 frames; source-save controls need at least 16.
`--timeout` applies separately to each game and accepts up to 7,200 seconds.

`matrix.json` records hashes for both Python runner files and every packaged
file, each child result's hash, physical counters and checkpoint receipts.
Changed frozen inputs, an unsuccessful exit, a missing/failed result, a save
outside its disposable run, a mismatched source receipt or a changed source
file abort the matrix. Completed reports and logs remain intact, and the
matrix is marked successful only after every selected save and reload passes.
Eight orchestration regressions exercise these failure paths without launching
a game.

The initial private `public-matrix-save-workflow-v1` smoke cohort passes both
actual Unity processes with 30 frames per case. Its source at frame 32 /
6.400000095 s retains physical SHA-256
`f77114eb68221b67bc82254488027c0f9128da901851cb52b7bc356c94c6b30b`;
the colony file is
`328a89474408426dda00a42e84e53d8865d42481d3cb55191d77993b7770b417`.
The fresh-process load preserves that initial image, passes the paused positive
interval challenge and header/body readback, and matches all 30 continued
frames. This verifies the orchestration; the full 22-case matrix remains open.

The [Linux runner](sealed-native-domain.md#run-the-actual-linux-colony-controls)
requires the licensed game and the private desktop/Steam setup. The separate
[Windows runner and evidence](sealed-native-domain.md#additional-windows-feature-controls)
cover actual Windows Unity. This Linux matrix cannot supply Windows evidence.
