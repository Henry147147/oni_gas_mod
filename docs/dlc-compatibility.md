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
It is not player-ready: dynamic rocket allocation/removal, inter-world
transports and complete cluster persistence remain required exits.

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

Spaced Out! still explicitly refuses startup. Multi-asteroid ownership,
transfers, rocket interiors,
bionic oxygen and aquatic ecosystem adapters remain to be implemented and
verified before their rows can pass. The matrix is a compatibility check,
not proof of every pack's gameplay systems.

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
