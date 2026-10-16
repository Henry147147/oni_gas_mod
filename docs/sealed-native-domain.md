# Sealed native fluid domain

This is the first bounded takeover experiment for the Linux colony work. The
custom native engine advances a six-cell room while the surrounding ONI colony
retains its original simulation. The compressed-water equilibrium control
passes in the actual Linux colony. **Gas takeover acceptance currently fails:**
an uncovered native writer changes a compatibility cell. The experiment stops
instead of accepting that result. Both are actor-free development scenes;
whole-colony ownership, first-playable and release gates remain closed.

## Execution and ownership

The mod attaches the exact-build native hooks before initialization. After a
disposable colony starts, the validation driver builds an unoccupied room with
an impermeable, adiabatic Unobtanium border. It verifies the border, absence of
objects, disease and radiation before enabling the domain.

The actual positive `ProcessNextFrame` return supplies the timestep and a
monotonic ticket. `UpdateData` advances the custom `MacClosedWorld` on the native
simulation worker, once for the global timestep. A mutex spans custom work and
the original update so a managed reader cannot consume a partially completed
native frame. Synchronization-only passes never advance custom time. There is
no managed advance timer.

The physical model retains constituent mass, U and canonical face momentum.
It uses the existing closed MAC pressure/transport solver, its compression
coupling, a declared ideal-gas/elastic-liquid reference and immutable candidate
advances. The air case contains nitrogen, oxygen and helium at trace abundance.
The water control initializes 1,100 kg in each cubic metre at 300 K: its
mechanical pressure is 220 MPa, with elastic energy retained separately from
thermal energy. These are explicit development materials, not a complete ONI
material catalogue or free-surface solver.

The driver first queues ONI's gas/liquid impermeability flags and verifies them
in the original native snapshot. Flag installation can displace fluid before
ownership begins. After activation it restores deliberately unbalanced,
nonvacuum scalar cells, then requires an exact match before starting the
30-frame acceptance interval. A missing projection fails within ten frames.

Native per-cell post-processing and pressure, gas, liquid, thermal and transition
entry hooks attempt to exclude owned cells. Native functions continue outside
the rectangle. These hooks plus the impermeability flags are insufficient in
the current gas run: some native work bypasses them. In particular, installed
gas-interaction hooks have zero calls; their exported symbols do not prove
coverage of inline code. A hot/cold Granite pair outside the room must continue
to conduct heat. That independent control passes before the writer failure.

The scalar native cells are compatibility data. To challenge the exclusions,
the driver first lets original ONI move a deliberately unbalanced scalar mass
pattern. It restores that pattern after activation and requires the original
element/mass/temperature/disease tuples to remain byte-identical while custom
physical state advances. This
control distinguishes a blocked native fluid phase from a native state that
would never have moved. It does not prove coverage of every game writer.

Each completed custom frame is copied through a separate `ma_oni_domain_*`
extension. An owned copy of all 47 native channels retains the surrounding
colony's events and nonfluid data. Only owned-cell mass, temperature and element
projections change. Immutable native thermodynamic readings feed the shared
mixture renderer and inspection path; scalar projections never reconstruct the
physical inventory. Publishing fails without replacing the last valid owned
snapshot, and a fault never enables scalar fallback.

The solver temporarily selects nearest rounding, masked exceptions and
FTZ/DAZ off on its executing thread. It restores the complete caller environment
before returning to Unity/native code. The normal strict arithmetic checks stay
enabled inside the solver.

## Run the actual Linux colony controls

Build the native backend and the normal ONI adapter, then create a fresh package:

```sh
python3 tools/build.py --oni-native-observer --build-dir build-oni-observer \
  --game-dir /path/to/OxygenNotIncluded
python3 tools/package_probe.py \
  --native build-oni-observer/libmixed_atmospheres.so \
  --native-observer build-oni-observer/libmixed_atmospheres_oni.so \
  --output artifacts/sealed-domain-package
python3 tools/oni_headless.py run --package artifacts/sealed-domain-package \
  --mode native-domain --frames 30 --timeout 600
python3 tools/oni_headless.py run --package artifacts/sealed-domain-package \
  --mode native-liquid-domain --frames 30 --timeout 600
```

Both runs use separate fresh profiles on the authenticated virtual desktop.
`native-domain.json` records physical progress, conservation, suppressed native
phases, projection controls, solid conduction and renderer publication. The
runner requires these facts and the native backend report, in addition to the
normal colony/probe report. It also requires the actual game's `Grid` fields
to match the physical projection, texture writes for the current publication
generation, and a save attempt rejected before native saving or any output.
It rejects idle workers, changed compatibility cells, missing controls,
nonfinite diagnostics and claims of whole-colony or playable readiness. A
passing offline native test is insufficient to pass this game-level gate.

The old `snapshot-copy` mode still stages the complete generated colony and
still rejects its unsupported incompressible liquid placement. A passing sealed
water experiment does not repair that importer or authorize full-world import.

## Current evidence

Actual native Linux Unity runs on ONI 744825, 2026-10-16, use a private
authenticated Xvfb desktop with rendering enabled. They are normal licensed
game processes, not a mocked simulation or Unity `-nographics` mode.

| Run | Result |
| --- | --- |
| `20261016T181635Z-d3a772` | Failed. Initially paused because of an incorrect speed-control call; manually unpaused for diagnosis. It does not count as an automatic pass. |
| `20261016T182613Z-13cfc2` | Failed. Original-flow control moved, but restored compatibility cells did not remain fixed. |
| `20261016T183805Z-270de8` | Failed. Additional gas entry hooks did not stop native transport; their counters stayed zero. |
| `20261016T185522Z-8a9584` | Failed before physical advancement. Flow-flag installation displaced scalar gas during bootstrap. |
| `20261016T191212Z-b81fc7` | Failed after ten custom native frames (2.0 s, 210 solver substeps). The restored scalar pattern was verified at frame one, then two scalar masses were exchanged. The original-flow control, flow flags, continuing native solid heat, copied native channels and retained physical frame checks succeeded. Renderer, game-grid projection and save-guard acceptance were not reached. |
| `20261016T191955Z-858237` | Passed compressed-water equilibrium control: 32 native frames, 30 after projection priming at frame two, 6.4 s and 672 solver substeps. Original-flow contrast, unchanged native compatibility tuples, copied channels, retained physical frames, actual game-grid projection, current-generation texture writes, native solid heat and save rejection all pass. No mod failure lines were reported. |

The gas run's maximum relative constituent-mass drift was `5.29e-16` and total
energy drift `9.87e-16`; oxygen transport changed the target cell. Solver thread
4 differed from activation thread 3. These establish physical solver progress,
not exclusive native ownership. Failure preserves the last valid owned snapshot,
stops custom advancement and keeps saving disabled. It cannot repair the
already observed native write.

The water run retains exactly 1,100 kg per cubic metre at 300 K and approximately
220 MPa, with zero reported mass or energy drift. The native compatibility
pattern remains `[1000, 1200, 1100]` in each row, while the actual game grid reads
the custom 1,100 kg target-cell projection. Its physical state is an equilibrium
control: this does not establish moving water, gravity, free surfaces or
multiphase transport. The saved screenshot is `native-domain.png` in that run's
private colony-validation directory. Reports retain whole-colony and
first-playable readiness as false, with saving disabled.

Private reports, game logs and captures remain under
`<headless-state>/runs/<run-id>`; build and sanitizer logs are in
`artifacts/headless-working-20261016`. Neither licensed game data nor private
reports are redistributed. All ten native CTests pass in Release and with
ASan/UBSan, including instrumented Funchook/diStorm dependency code. Current
Windows code cross-compiles; ABI 2 runtime execution and domain activation are
not validated on Windows, where domain start explicitly rejects.

## Limits and next work

Only the fixed sealed scene is enabled. Direct known native mutation packets
targeting its cells or border reject before dispatch; every other native writer
still needs an adapter and dedicated coverage. Production actors, duplicant
breathing, machines, construction, cross-boundary fluid/solid heat and open
boundaries are not implemented for this domain. They must not enter it.

Saving is disabled while the development domain owns fluids. Its physical
state is not an integrated ONI checkpoint, and an ordinary scalar save cannot
resume it. Failure stops custom advancement while native fluid exclusions stay
installed until native shutdown; the disposable session must end.

The immediate next step is to identify the uncovered native writer and replace
its containing task or phase where necessary. Repeat the adversarial gas and
compressed-water controls through 30 native frames after projection priming,
then verify the actual game grid, renderer, save guard and fault teardown.
Do not weaken the unchanged-native-cell assertion to obtain a passing result.

After that gate passes, add actor transactions and native callbacks, real
closed-domain checkpoints, an occupied development colony, then open/multiphase
geometry and complete reader/writer coverage. Native worker execution alone
is narrower evidence than a playable overhaul.
