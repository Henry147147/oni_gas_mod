# Mixed Atmospheres 1.12.2.2

The November 22 player report on ONI build 764025 and the compatibility failure on Windows build 744825 had different causes. This hotfix includes the [Cycle 55 solver correction](playtest-1.12.2.1.md). It also fixes a crash while loading a ration box on the older Windows player.

## Ration-box crash

A clean Windows player under Proton loaded the archived base-game save, restored owner frame 3, and displayed ONI’s error dialog while spawning the ration box. It could still write frame 45. Returning to the menu then exited the process before the second load. The same save sometimes completed both loads. The error reproduced on the previous Workshop package and on the held 1.12.2.1 candidate.

At the point of failure the filter and the box referred to the same live storage. It held 20 units of field rations at 1 kg each. The exact-mass read returned 20 kg. Ordinary and reflected calls to ONI's rounded mass reader both failed at the first instruction of its Harmony patch. The routine never read the rations. The Windows exception trace confirmed a failed memory read at that entry. Separate missing-component and missing-items controls produced different stacks.

The bundled Harmony allocator can ask Windows for a page at a specific address. When that address is zero, Windows interprets it as a request to choose an address. The allocator reports success even when the returned page falls outside the range the patch can reach. Mixed Atmospheres now rejects zero in that positioned allocation overload, allowing Harmony to continue its search or use its fallback. This applies to the identified Harmony module on Windows. The game contract pins the method body. Normal unpositioned allocations and valid positioned requests continue through the original allocator.

## Reproduction and verification

The private base-game fixture has SHA-256 `21c9d74930da02d160f0f98eff4f6f4809738f75fe498beabb68d5aaf71ef716`. The first two failing 1.12.2.1 launches, the failure on the previous release, and the live storage inspection are under `artifacts/v1.12.2.1/f81c2dc` in `version-matrix/744825-proton`, `legacy-recheck/hotfix-6`, `legacy-recheck/trace/previous-3` and `legacy-recheck/trace/addon-4`. Keep those fixtures and raw logs private. A diagnostic run that stopped during database startup appears separately in `trace/addon-3`.

`tools/WindowsHarmonyPageTests` uses non-executable data pages and the actual packaged adapter under the game's Windows Mono. It checks the original allocator, installs the production guard, checks all three allocation paths, removes the guard and repeats the original controls. A failure to release a test page fails the check. Build `tools/ColonyPacingCreditHarmonyTests/mono_runner.cpp` as a Windows x64 executable. The following command runs the standalone regression against the adapter in a release folder. On Windows, omit `--wine`.

```sh
python3 tools/WindowsHarmonyPageTests/check.py \
  --game-dir /mnt/data/share/oni/OxygenNotIncluded-744825 \
  --adapter "$release/MixedAtmospheres/archived_versions/744825/MixedAtmospheres.Oni.dll" \
  --runner "$mono_runner" --wine "$wine_wrapper" --output "$private_output"
```

For a game repeat, use the matching Windows package and a fresh profile. Run `tools/oni_proton_headless.py` under `tools/game_slot.sh low` with `--mode colony-owner-load --load-save SAVE --dlc-mask 0 --frames 40 --save-colony --reload-in-process`. Verify both loads and the absence of attributed mod errors in the player log. Run the Cycle 55 check separately on build 764025 with the player's saved fluid settings. Its pass condition includes a natural Cycle 56 autosave and a fresh Windows player loading that autosave.

The [performance handoff](performance-handoff.md) records the selected settings and the repeatable colony benchmark.
