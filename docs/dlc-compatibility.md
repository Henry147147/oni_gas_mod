# DLC compatibility controls

**Current priority, 2026-10-27:** the user's private Windows playtest requires
fully working Spaced Out!, including multiple asteroids, rocket interiors,
transport and cluster persistence. The existing ZIP is unsuitable for that
playtest. The startup restriction remains until those paths are implemented
and verified; removing it alone is not support.

Initial implementation now captures a validated world-ID layout from the live
cluster, separating asteroid and rocket rectangles, rejecting overlapping or
out-of-grid regions, and keeping unallocated cells outside world ownership.
Base-game geometry description uses this same layout. Synthetic controls cover
adjacent worlds, reserved holes, rocket removal, stable ordering and invalid
identities/geometry. The ordinary DLC restriction remains in place. An explicit
development control, `MA_COLONY_SPACED_OUT_CONTROL=1`, admits the complete
fixed cluster grid to the existing solver, keeping unallocated cells closed
and refusing any fluid that would be omitted outside registered worlds.
It is not player-ready: rocket removal/reuse, the remaining inter-world
transports and complete cluster gameplay acceptance remain required exits.

The normal DLC-enabled WindowsPlayer diagnostic activated `EXPANSION1_ID`
through its usual menu preferences and measured **nine worlds on a 736×304
grid**. The original candidate correctly stayed inactive; that run establishes
installed content and layout, not mod ownership. The development owner needs
its separate game run before activation can be reported as passing.

The Proton runner requires `--spaced-out-control` together with an explicit
Spaced Out! DLC mask for that development run. Ordinary owner runs continue
to strip inherited development environment variables. The explicit control
is recorded as an environment override and cannot certify ordinary DLC
acceptance. The first attempt without that runner opt-in stayed inactive;
it is not an owner-admission result.

The explicit `48f9286` attempt reached the nine-world layout, then failed the
harness's mod-error check during world-germ configuration. Spaced Out!'s inert
radiation disease uses the game's exact idempotent temperature sentinel: all
four bounds and four half-lives are positive infinity. The adapter rejected
that valid registry entry and disabled owner germ evolution for the table.
Validation now admits only that complete sentinel; the existing temperature
calculation already returns an unchanged population. Synthetic controls cover
whole and fractional populations at finite temperatures and reject partial
or NaN sentinels. This repair does not establish cluster ownership or save/load
acceptance; those require a fresh game run.

The development owner also tracks completed rocket-template callbacks. At the
next completed native publication it queues ownership of the new world's
non-solid cells, including vacuum, without importing an existing world's
inventory again. The routing map excludes reserved holes and checks actual
world IDs. Layout controls distinguish additions and removals and reject a
moved or reused live identity. Rocket removal still refuses the experimental
path until complete-mixture evacuation is implemented; no normal DLC activation
or rocket gameplay acceptance is claimed by this addition.

The new `spaced-out-worlds` development scenario constructs a sealed mixture
probe in every actual generated asteroid. It checks both constituent masses
and ownership in each world, excludes a reserved grid cell, changes the active
camera world and returns while requiring the same advancing owner session and
closed mass/energy ledgers. Merely reporting several world containers cannot
pass this control. Rocket and inter-world transport controls remain separate.

The first `c891a53` cluster run establishes running ownership of all nine
asteroids and owner germ evolution: 23 frames, no rejected advances or skipped
relaxations, and maximum relative mass/energy errors of 2.28e-16/1.84e-16.
It does **not** pass the scenario. Sealing the probe in world 8 displaced
0.01749448 kg of its original oxygen into the centre before the source was
added; that probe measured 1.01749448 kg instead of the required 1 kg. The
fixture now clears the sealed centres and verifies exact vacuum before adding
the unchanged 1 kg oxygen/0.5 kg carbon-dioxide source. Its mass bounds, camera
checks and final observation frames are unchanged. Cold/menu controls were
not reached in that failed run.

The `3c697fc` repeat passes the same nine-world control, a source save,
fresh-process load and return through the menu into a second load. It runs
23 owner frames in the source and 32 total frames after each load. Both load
paths exactly reencode the restored owner image (wrapper 12, core 23); the
menu control verifies teardown and a new owner session. Germ evolution stays
in owner mode 1. Across these runs the maximum relative mass/energy errors are
2.19e-16/3.24e-16. This is WindowsPlayer under Proton, not Windows OS evidence.
The managed fixture is `3c697fc`; native DLLs retain their `c891a53` build
provenance, with identical native sources and recorded binary hashes. These
passes do not certify rockets, transport or byte-exact active continuation.

New rocket cells also need the correct open-space sink. A native capability
now replaces selected space flags atomically after waiting for prior owner
work; it changes no inventory or clock and uses the existing saved space mask.
Template preparation clears a reused slot's old mask before sources arrive;
completed publication classifies its space/interior cells before opening them.
Synthetic controls cover atomic refusal, space loss, reuse as an interior,
unchanged time/inventory at the metadata edit and exact cold reencoding.
The `spaced-out-rocket` control builds an actual platform, engine and capsule
through the normal building lifecycle, waits for the new interior's ownership,
and checks added oxygen/carbon dioxide over its sealed cabin. It remains an
unrun control until its game result is recorded.

The first `13e5345` rocket run passes the nine-asteroid checks again, but fails
fixture construction before any interior is created. Direct generic building
placement omitted the rocket's craft/attachment context; native engine and
capsule components then threw during spawn. The fixture now uses the game's
platform/module construction commands, with its instant-build setting restored
immediately, and waits for the actual paired interior door before reading its
world. No rocket pass or save/load result is taken from that failed run.

The corrected `178aab3` construction control passes: world 9 is registered as
the tenth cluster world, all 996 non-solid cells become owned, and its cabin
retains the added 0.25 kg oxygen and 0.125 kg carbon dioxide over the fixed
eight-frame observation. The source saves at frame 48. Cold and menu loads
both reach frame 56, exactly reencode the same restored wrapper-12/core-23
image and retain all ten worlds; the menu path verifies a second owner session.
The maximum relative mass/energy errors across the runs are 5.09e-16/4.00e-16.
These are Proton controls with `178aab3` managed code and identical `13e5345`
native binaries, whose original provenance is retained. They establish rocket
creation and restoration, not flight, removal or cargo transfer.

`spaced-out-rocket-ports` follows that construction control. It connects the
actual gas/liquid ports in both directions, requires their native paired
components to share the same storage, seeds 0.75 kg/0.25 kg of two constituents at each input,
and waits 16 owner frames for ordinary callbacks. It requires both constituents
at the opposite world's output, empty source/storage, closed world ledgers and
the exact pipe/storage boundary audit. It does not call transfer callbacks or
replace the game's rates.

The `76b36f8` control passes all four directions: every opposite output holds
exactly 0.75 kg/0.25 kg, with empty input pipes/shared native stores and an exact
pipe account. The source saves at frame 68 with four occupied pipes holding
4 kg. Cold and menu loads both reach frame 77 and exactly reencode the same
wrapper-12/core-23 owner image; the menu load verifies a new owner session.
Maximum relative mass/energy errors across the runs are 5.52e-16/6.02e-16.
These are Proton controls with `76b36f8` managed code and the unchanged
`13e5345` native binaries, retaining that build provenance. They do not certify
ordinary activation, rocket removal/flight, teleporters or cargo.

Rocket retirement also needs to finish edits queued by the last native frame
before freeing its cells. A new explicit native input boundary applies the
existing ordered edits and precise receipts without consuming a fluid frame.
It runs no flow, reservoir release, phase conversion, trace/space sink or germ
growth. Deferred edits remain visible and cannot be declared settled. Tests
cover every constituent of a mixed extraction, germ edits without growth,
refused heat/retry, durable outcomes, same-frame view/history invalidation and
exact cold reencoding. Its managed caller fences native work and publishes
callbacks through the normal zero-time game step. Ordinary save semantics are
unchanged; this boundary alone does not implement rocket evacuation.

The first `c269a9e` in-game boundary control fails after all nine-world probes
pass. The diagnostic `7f0fb8f` repeat identifies the refusal as the mass ledger's
unbooked-constituent check; cold/menu phases are not run. Recomputing the actual
cells in `aec8dff` does not fix it: the same refusal reports an absent Latex
constituent as 0 kg. The full scan remains as an independent lifecycle audit,
with the existing zero-scale check and mass/energy bounds unchanged.

A standalone reproduction deliberately fills the native caller's x87 register
stack. The original calling-thread execution, and a new worker inheriting that
state, fail; the control passes after using the solver's own worker with a clean
x87 register/tag state at thread entry. Saving rounding/control words alone
does not clear occupied registers. Lifecycle commands now use that same worker,
without advancing the fluid clock, and return command exceptions to their
caller. Standalone coverage includes both fresh and warmed topology/trace
boundaries. A replacement game control is still required; these failed attempts
provide no lifecycle acceptance.

The `31d53b7` replacement passes source/cold/menu on WindowsPlayer under
Proton. Its zero-time boundary holds owner frame 24, native ticket 4524 and
4.8000000715255737 s throughout; the precise extraction returns 0.25 kg oxygen
and 0.125 kg carbon dioxide, and the return leaves 1.125 kg/0.5625 kg in the
sealed source with an exact pipe account. The actual source saves frame 24;
the older harness entry snapshot reports frame 23, which is retained as-is.
Both loads reach frame 32 and exactly reencode the same wrapper-12/core-23
image; the menu path verifies a new session. Maximum relative mass/energy
errors across the runs are 2.73e-16/2.86e-16. The scenario reporter now refreshes
its completed publication after a lifecycle scenario returns. These controls
remain experimental and do not certify ordinary DLC activation or Windows OS.

### Rocket retirement

The development adapter now prepares a complete retirement inventory on the
solver worker before changing ownership. Its bounded transient `MARET001`
handoff carries both phases, actual per-ID germ removals, full-range mechanical
accounts and precision-energy remainders. Owner-held sub-ULP parcels and
reservoirs anchored in the interior also leave; reservoir contents already
outside the CFD ledger are not debited twice. A stale preparation or queued
input refuses commit without changing the source. This introduces no new
persistent checkpoint fields or solver arithmetic.

After commit, the empty cells stay quarantined until the game finishes its
native grid cleanup. Saves defer/refuse during that interval. The adapter
holds the game's early unregister so another capsule cannot reuse the live
slot. Landed recovery creates the ordinary per-species chunks; airborne
recovery uses the game's debris stores and travel lifecycle. Retained pipe
spills are included. In-flight carbon-dioxide particles move to the landed
destination, or become native debris items before their old pool entries
retire. Solids, crew and ordinary pickupable recovery retain the game's path.
Unexpected late inventory refuses cleanup rather than abandoning its anchor.

Standalone tests cover stale/invalid/cancelled preparation, hidden liquids,
fractional germs, an actual 1e-240 kg precision parcel, a retained reservoir,
quarantine/save refusal, exact cold restoration and clean slot reuse. Managed
wire tests check both phases, trace/germ/energy preservation, full-range
mechanics, source identity and every byte truncation; installed-game hook tests
pin both recovery destinations and the allocation fence. The new
`spaced-out-rocket-retire` game control dismantles the constructed capsule with
its nonempty mixed ports, checks recovered constituents, rebuilds it in the
same slot and verifies a fresh mixture. That game result is still required;
ordinary DLC support remains disabled.

The first `14ec7b4` retirement game control passes worlds, rocket creation and
all four mixed-port directions, then completes recovery/unregister of world 9.
It fails at owner frame 71, two frames after the retirement boundary, with
`flow: Private entropy diagnostic is nonfinite`; constituent recovery/rebuild
and cold/menu checks are not accepted. This failure blocks DLC activation.
The Proton runner has an explicit `--capture-owner-fault` diagnostic to write
the pre-flow owner dump inside that run's private output directory. It marks
an environment override and cannot be mistaken for ordinary acceptance.

The `d89800c` diagnostic repeat fails earlier: native grid cleanup reenters
while complete-mixture recovery is still running, so the adapter correctly
refuses the free at frame 69. No failing flow occurs and no flow dump is
expected from that attempt. The zero-time game publication can dispatch the
game's scheduled callbacks recursively. Cleanup now records the delete request
and runs it from a later Unity `LateUpdate`, after the recovery callback has
returned and outside any unfinished sim step. Duplicate nested requests are
suppressed; saves stay blocked until completion. The original entropy failure
is not waived or yet proven resolved by this ordering repair.

The `a2f963f` repeat avoids the early-free refusal but still fails the next flow
after 71 completed owner frames. Its private pre-flow dump reproduces that
failure offline: every cell passes the initial EOS/entropy scan, then the
advance throws. A smaller retirement regression also advances a warm physical
tick before any cold restore and passes. Recovery/reuse and loads remain
unaccepted while the larger state's failing stage is investigated.

The captured frame-72 failure is now reproduced and fixed offline: a valid
cell carries about 1e-313 kg of gas before trace removal. The diagnostic's
`R*T/partial` overflows although its mass-weighted entropy is finite. An
exceptional logarithmic fallback avoids the ratio; ordinary finite diagnostic
arithmetic, trace thresholds, physical evolution and checkpoint encoding are
unchanged. The same private capture advances from 14.200000211596489 to
14.400000214576721 seconds with the fix. A regression compares the exceptional
result with a wide evaluation and the ordinary path bit for bit. Actual rocket
recovery/reuse and cold/menu restoration still require a new game control.

The entropy repair passes the full build (386 Python tests, eight skipped;
86 native CTests; all managed suites). Clean old/new builds of all 85
`ma_mac_*` executables retain their physical outputs and exit codes; only the
validation timing fields differ. The long Riemann gate remains unrun.

The `1ccbe75` actual source now passes retirement and slot reuse: it recovers
1 kg oxygen, 0.375 kg carbon dioxide, 0.75 kg water and 0.25 kg polluted water,
closes the removed cells and reuses the same world-9 rectangle. The new cabin
contains none of the former CO2/liquids and retains its new 0.125/0.0625 kg gas
source. The source ends at frame 87 with relative mass/energy errors
2.37e-16/5.56e-16. Fresh-process load reaches frame 96, reencoding the restored
wrapper12/core23 owner exactly. The menu-load check remains queued. These are
WindowsPlayer/Proton development controls, not ordinary activation or Windows OS
acceptance.

### Inter-asteroid teleporter ports

The `spaced-out-warp-ports` control discovers the actual generated sender and
receiver pairs with the game's normal world-generation tags. It completes
their activation work, connects each endpoint's own gas and liquid pipes,
then seeds 0.75/0.25 kg mixtures. Ordinary callbacks must move both identities
in both directions, empty each input/shared store, and keep the owner and pipe
accounts closed. No transfer callback is invoked manually. The scenario builds
against the installed SDK; its actual game/save/load checks remain unrun.

### Rocket flight

`spaced-out-rocket-flight` fuels the constructed CO2 engine and selects a valid
adjacent space hex. It issues the normal launch command with the same temporary
instant-build fixture switch used for construction, then restores that switch
before game updates. The actual engine animation, fuel use and world-parent
changes must reach `InFlight`; the advancing cabin must retain both gases and
closed world/pipe accounts at the destination. It does not certify preflight
crew or obstruction checks. The scenario compiles against both installed SDKs;
its game and in-flight save/load checks remain unrun.

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

## Local content blocker

The private Linux `dlc-readers-aquatic-r2` run reached the menu and passed
native attachment, then refused before world generation: requested mask 16,
observed active IDs empty. Its fresh KPlayerPrefs retained `DLC5_ID.ENABLED: 1`
and the four disabled flags. Local inspection confirms these are the game's
ordinary preference keys. The inspected Linux installation contains none of
`expansion1_bundle`, `dlc2_bundle`, `dlc3_bundle`, `dlc4_bundle` or `dlc5_bundle`;
the game requires installed content as well as its platform subscription.
This does not establish the account's ownership status. Installing the licensed
content through its normal platform is required before those Linux rows can
run. No platform checks or content files were altered.

## Remaining exits

The first pack reader adapters are in `ColonyDlcFluidReaders`:

- Aquatic habitat checks use the owner's total physical liquid occupancy,
  including liquid under a gas compatibility projection. The game's existing
  caller-supplied substantial-liquid threshold remains the threshold. Its
  swimming and suffocation monitors reach this through
  `Grid.IsSubstantialLiquidUnsafe`; stored creatures keep their original rule.
- Bionic refill scoring counts breathable minority oxygen using the same
  owner breathing rule as the gas provider. It retains the game's sample
  denominator, binary32 addition order, world boundaries and other safety
  flags. Native/unowned samples keep their existing element-tag rule.

The `dlc-fluid-readers` game scenario constructs both hidden-phase cases and
calls the actual refill/habitat methods. With Aquatic selected it also spawns
the pack's puffer fish and checks its own suffocation monitor. A base-game
query check cannot establish that Aquatic content check. The private Linux
`dlc-readers-base-r3` run passes both query checks on core23/profile5: actual
refill scoring sees the hidden oxygen, and the habitat method sees physical
liquid below a gas projection while retaining its caller's threshold. The
Aquatic creature check remains blocked on installed content. These are reader
adapters: exact bionic tank admission/consumption, all-identity storage, aquatic conversions, rocket
cargo and inter-asteroid transport remain separate work.

The same reader implementation also passes the existing Linux pacu and hazard
controls in `base-liquid-readers-r5`: swimming, scalding, dense-air ear damage
and submerged breath holding. These are base-game regressions, not pack
gameplay acceptance.

Standalone checks pass: 86 Linux Release CTests, 378 Python tests (four
skipped), and 227 managed boundary scenarios. The adapter compiles against
both installed game assemblies; the Windows script passes PowerShell parsing
and refuses execution on Linux. No new DLC game run has been accepted.

The private receipt/reader packages have no portable Linux provenance. An
offline Ubuntu 22.04 build passes all 86 CTests, but its ABI check rejects the
existing solver's `hypot@GLIBC_2.35` import against the declared GLIBC 2.34
ceiling. The current-host diagnostic package is not a portable release.
No ABI pass was recorded and no solver arithmetic was changed to hide this
failure. Native Windows compilation and actual Windows OS runs remain separate
checks; compiling the managed adapter against Windows assemblies proves neither.

Ordinary Spaced Out! activation still explicitly refuses startup. Development
controls above establish multi-asteroid ownership, rocket creation and paired
mixed ports, including source/cold/menu restoration. Rocket retirement,
teleporter/cargo and full cluster gameplay acceptance remain open. Bionic
oxygen and aquatic ecosystem adapters also remain incomplete. The matrix is a
compatibility check, not proof of every pack's gameplay systems.

Exact stored-fluid heat/biology and nonempty schema5 cold rebinding remain open
in [mixed pipes](mixed-pipes.md). Native receipts remove the former multi-kJ
missing-credit defect across queued thermal proxy resets. The user classified
the measured remaining 0.703 J residual as non-consequential on 2026-10-26;
it no longer blocks acceptance, while its original diagnostic remains recorded.
Native scheduling
capture/restore is wired to a development schema5 control with pristine empty
stores. Its actual Linux cold/menu controls now pass restoration, but the
uninterrupted/restored owner and pipe observations differ on the first resumed
frame; active continuation remains unaccepted. A restored image's
exact reencoding proves restoration only; the matrix explicitly leaves byte-exact **active**
continuation unverified. The [F3 levelling and F4 conduction proofs](base-game-floor.md)
also remain open, with their original grids, metrics and bounds.
