# Mixed Atmospheres 1.12.2.3

This patch includes the [Cycle 55 fluid fix](playtest-1.12.2.1.md) and the [Windows ration-box fix](playtest-1.12.2.2.md). It also fixes a third failure found while testing the November 21 colony with different storage limits.

The Windows player on build 764025 loaded the colony with storage bins at 5× and saved at owner frame 134117. A fresh player loaded that save at 10× and rebound all 15 pending requests. One belonged to a critter eating fluid. Its callback tried to credit the meal using a foraging target that the game does not save. The critter had yet to choose another target, and the game stopped on the first positive fluid step.

For a positive draw the callback receives the element and mass the simulation actually removed. Both the diet and storage paths now use that element while the colony owner is active. This keeps the meal independent of the critter's next foraging decision. Empty draws still follow the game's existing retry logic. The change is in the managed game adapter. Colonies use their existing saves and saved fluid settings.

## Repeating the load

The private regression fixture is the save the 5× run wrote, SHA-256 `2c775a2db6b633571b36d30c036f068ce6611dbd68b5f548db8ecde72659dda1`. Its held-package receipt is under `artifacts/v1.12.2.2/1d042e7/submitted-windows-reload`. Run the following with a fresh output directory and the package being tested.

```sh
tools/game_slot.sh low python3 tools/oni_proton_headless.py \
  --proton build-proton-official10/proton-10.0-4b \
  --game-dir /mnt/data/share/oni/OxygenNotIncluded --package "$package" \
  --mode colony-owner-load --dlc-mask 1 --load-save "$save" \
  --settings-preset artifacts/v1.12.1/all-storage-10x.json \
  --frames 120 --speed 3 --save-colony --gpu-display :99 --output "$output"
```

Check that all 15 requests rebind, the colony advances at least 120 frames, and it writes another save with no mod errors. The separate `spaced-out-critters` scenario exercises the gas and liquid storage paths and checks mass across the creatures, pipes and world.
