# Mixed Atmospheres 1.12.0

## November 20 playtest

The submitted Windows save restores at simulation frame 128272, with 83,495
owned cells. The log identifies game build 764025 and the same 2,860 managed
dependencies as 1.11.0. The recording spends a long stretch in the pause menu. The
figures below use the five complete, unpaused 150-tick windows in the submitted
`Player-prev.log` with the 3× button selected. Their median speed was 0.95 game
seconds per real second on the player’s Ryzen 9 9950X. The median owner
update took 184.5 ms, including 155.1 ms in the flow stage.

A replay of the same save and 90 captured input frames located two expensive
regions. The 11,768-cell base region spent most of its time in pressure,
transport and interface sharpening. The 7,088-cell surface region needed an
average of 7.3 flow intervals per tick. Other work in the flow stage included
reading about 16,500 cells per tick for temperature-driven conversions, even
when only a few dozen products resulted.

Version 1.12.0 tests conversion candidates in parallel using the existing
temperature and liquid-volume reading. Workers return only cells crossing a
transition. The owner still admits products in cell and rule order. On the
submitted journal the median scan and planning time in nine sampled frames
fell from 6.3 to 1.2 ms. The cold restore and 90 ticks ended with the same
owner fingerprint `2420d7f24fedf5c3` and maximum relative mass error
8.782 × 10⁻¹³. A separate whole-host comparison without the stage logger
averaged 123.2 ms before and 121.2 ms after across two runs of each version
with the first ten ticks excluded. The 2 ms difference in total tick time was within the spread
of the individual runs.

The submitted log also recorded 99,474 checks for changes to actor positions
at zero game time. They consumed 274 seconds of the Unity main thread.
The check now follows actors that can move. Fixed buildings are registered at
spawn and removed at cleanup. Positive simulation ticks and explicit save
flushes still capture the full scene. In two 46-second paused inspections,
both packages saved at frame 128314. Across the two complete 15-second
windows after each save, the old check consumed 3,099.0 ms in 1,229 calls
(2.52 ms/call) and the new one consumed 234.9 ms in 1,472 calls
(0.16 ms/call), a 94% reduction per check.

## Player controls

Under **Settings → Storage**, loose debris and stored items have independent
sublimation switches. Oxylite on the ground still emits oxygen. The storage
switch also protects oxylite inside a container. Both switches start off.

Nineteen storage building types have separate capacity multipliers, from 1×
through 100×. Each defaults to 1×. Restart ONI after changing a capacity.
The compact status panel and settings window share the game's slate panels,
mauve headers and flat buttons. At the recording’s 2560 × 1440 resolution, the
settings window goes from 1376 × 1088 to approximately 933 × 707 pixels.
The status panel width drops from 525 to 373 pixels.

The packaged release passed eight in-game storage checks in the Windows
player under Proton. A 2× storage bin reported 40,000 kg
and a 3× gas reservoir and its inlet both reported 3,000 kg. The item scenario
checked loose dirt, loose oxylite, protected and unprotected storage, and dirt
resuming emission after removal.

The package supports builds 744825, 763796 and 764025 on both platforms.
The packaged source revision is `cb39fa5`. The Linux compatibility build
passed all 94 native tests.

The core library shipped on each platform is byte-identical to 1.11.0.
All 53 standalone `ma_mac_*` executables from the Ubuntu compatibility builds
are also byte-identical across these two releases. The game backend and the
managed adapter contain the changes.

## Packaged playtest

With the Ryzen 9 5900X test host reserved for the Windows player under Proton,
each package ran for 455 ticks from the submitted save with 3× selected. Across
the same two complete unpaused windows (frames 128423–128722), mean simulation
time per tick fell from 151.1 to 140.9 ms, about 7%. Actual game speed was
0.787× on 1.11.0 and 0.786× on 1.12.0. Active play still fell below 1× on
this host, and 3× remains open. Both isolated runs passed the colony and
shutdown checks under Proton.

In an initial 605-tick comparison the game shared CPU cores with the patch
suite. Across the three unpaused windows the 1.11.0 rates were 0.65×,
0.58× and 0.54×. The 1.12.0 rates were 0.66×, 0.34× and 0.59×. These
windows included concurrent build and replay work with varying load.
Both packages reached frame 128880 and wrote the cycle 44 autosave.
The second run passed the colony checks, then Proton returned exit code 5
after Unity logged its completed simulation and input shutdown.

All six patch acceptance steps passed at `cb39fa5`. The offline run covered
six generated colonies and six fixed replays, the managed suites (including
540 settings checks) and 565 Python cases (four skipped). Packaging passed
94 Linux native tests and 95 Windows native tests. In-game checks covered
terrain, heat, phase changes, doors, mixed pipes, reservoir accounting,
throughput, vent duty and reloading the fixed meteor save twice in one
Windows player process under Proton. All six platform and game-build adapter
contracts matched their respective installed game.
