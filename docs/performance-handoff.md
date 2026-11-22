# Resuming performance work after the November 22 hotfixes

Performance work was deferred on November 22 to qualify the Cycle 55 crash fix. Version 1.12.2.2 retains the saved fluid update rate and the Every tick default. Its release acceptance uses the established colony regression tests. A new full-cycle speed measurement on the November 21 save was deferred.

## Where to resume

The last measured Workshop package was **1.12.2, revision `0da7afd`**. ONI build 764025 in the Windows player under Proton ran a complete colony cycle at 2.078 game seconds per real second on the test host. The same cycle on 1.12.1 averaged 0.886×. Both runs included the Cycle 46 autosave. The player chose full detail with fluids every eighth tick. This advances fluid movement for approximately 75 seconds per 600-second game cycle. The seven-option speed and fidelity comparison is in the [1.12.2 measurements](playtest-1.12.2.md#measured-game-speed-on-the-november-21-colony).

Use the November 21 manual save for this comparison. It opens at frame 133995 and has SHA-256 `c59a2df1726e476cebc193efe05f8503fcb0d84b8c6d92bdf0763fd777364f1f`. The Cycle 55 save opens at frame 161779 and is the separate crash regression. Keep both inputs private and run games on copies.

1. Use the candidate's packaged binaries and the same Ryzen 9 5900X host, Windows player under Proton, 1920 × 1080, Spaced Out and 3× game speed. Take the whole CPU lock through `tools/game_slot.sh whole`. Open the November 21 save, select **Gameplay → Region simulation → Fluid update rate → 1 in 8 ticks**, and close settings. Use two flow parts, liquid sharpening on and the save's pressure tolerance of 10⁻⁶.
2. Allow 150 ticks for loading and the settings change. Measure all twenty consecutive 150-tick windows from 134146 through 137145. Check that each log window is complete, unpaused, at speed button 3, with zero dropped samples and unmatched publications. Divide total game seconds by total wall seconds. Cross-check with a host monotonic clock between the end of warm-up and the last window. Keep the natural autosave and verify its frame.
3. The retained private artifacts for the last measured package are under `artifacts/v1.12.2/0da7afd/`. They include `preferred-full-cycle/Player.log`, `selected-ordinary-cycle.json`, `external-cycle-clock.json` and `ordinary-player.py`. For a fresh 1.12.2.1 timing on this checkout, run `python3 artifacts/v1.12.2.1/qualify.py f81c2dc performance` once the game slot is free. It writes a separate run under `artifacts/v1.12.2.1/f81c2dc/`. The original 1.12.2 reference advances 599.91 game seconds in 288.67 wall seconds.
4. For work on the Every tick solver, use the frozen owner, 450 input frames and disease table in `local/perf/nov21-late-window/`. Compare the last 300 frames after warm-up. Preserve the final owner fingerprint, relative mass and energy discrepancies, regional stage timings and worker count from each run. A change to subnormal physics can change the fingerprint. Inspect which physical fields first differ against the crash fix before attributing a change to performance work.

From the project root, the Linux replay command is

```sh
capture=local/perf/nov21-late-window
tools/run_locked.sh whole build-production/ma_colony_benchmark \
  --owner-replay "$capture/owner" "$capture/inputs.txt" "$capture/diseases.bin" \
  --frames 450 --warmup 150 --workers 12
```

The Windows native replay is in `artifacts/v1.12.2/8a03941/frozen-windows.json`. The Linux pair is in `artifacts/v1.12.2/bb3d6f2/frozen-linux-{08faa66,8a03941}.json`. Each report includes stage costs and a final owner fingerprint. Use a fresh full-cycle game run to evaluate any candidate that improves the native replay. Existing settings, fixed solver gates and conservation limits are part of that comparison.
