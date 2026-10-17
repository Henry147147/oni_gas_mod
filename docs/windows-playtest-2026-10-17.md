# Windows game testing, 2026-10-17

Actual Windows ONI testing passes the scoped native gas/water save-and-reload
controls, four unselected tile-hover cases, and the broader presentation and
inventory fixture. Testing also found and repaired the overlay shortcut.
The first playable colony gate remains open. These are bounded development
regions and explicit presentation inventories, not whole-colony fluid authority.

The base is `9eb807b7fea4a47b3cdc73409d148a81f95f7b3e`, with the separately
frozen `MixtureStatusDisplay.cs` candidates described below. ONI is build 744825,
Unity 6000.3.5f2, WindowsPlayer, Direct3D 11. Tests used the licensed Steam
installation and disposable Spaced Out colonies. The readiness document is
maintained separately; this report records this testing cohort only.

## Completed runs

Evidence is private under `local/windows-playtest-9eb-r1/`. Each run has its own
package identity, logs, configuration, game reports and screenshots.

| Run | Result | Scope |
| --- | --- | --- |
| `water-source` | Failed | Cached-world startup threw in `CustomGameSettings.GetCurrentClusterLayout`, before a colony or custom owner started. Retained unchanged. |
| `water-fresh` | Passed | Fresh generation; 121 completed native water frames and an admitted colony save. GPU gas-renderer texture bindings/readback also passed for the liquid scene. |
| `water-reload` | Passed | Identical restored physical checkpoint, frame-zero resave and 120 exact continuation frames; restored renderer passed. |
| `gas-shortcut-r1` | Physics/save passed; shortcut candidate incomplete | 121 completed native gas frames and an admitted save. First shortcut entered composition; the second incorrectly returned to composition. |
| `gas-reload-shortcut-r2` | Physics/reload passed; shortcut candidate rejected | Identical restored checkpoint and 120 exact continuation frames. Polling held modifiers in the native input callback did not repair the second shortcut. |
| `tile-hover-shortcut-r3` | Passed | Four actual native mouse-hover cases, individually reviewed Unity PNGs, held-reading receipts and clean presentation shutdown. All five overlay-cycle positions passed. |
| `presentation-r4` | Passed | Six atmosphere scenarios, 31 captured presentation stages, six bound inventory surfaces, nine selected-inventory checks, six additional appearance cases, 18 CPU texture-buffer probes and 38 isolated reservoir checks. |

Both native reloads also pass the saved frame-input journal reader. This checks
the recorded transcript; it does not independently solve the fluid equations.
The water equilibrium run records zero relative mass and energy drift.

## Hover and overlay results

Success requires hovering a tile to list **every non-trace gas**, with its
original mole percentage, without selection or a top-N limit. Trace omission
must never hide a hazardous-trace warning. Expanded inspection retains traces,
mass and partial pressure. This remains part of the
[first playable colony criteria](completion-plan.md).

The tested unselected hover visibly lists nitrogen 60.9997%, oxygen 25%, carbon
dioxide 10% and steam 4%. Benign hydrogen and hazardous chlorine are omitted
from the bulk list, while the explicit chlorine warning reports 0.0002% against
a 0.0001% limit. The exact-threshold case retains steam; moving the cutoff one
binary64 step above its mole fraction removes steam from the bulk list. The
warning survives both cases and the 125% UI-scale case. Two camera zooms were
reviewed. Receipts identify the actual held cell, revision and publication.

Shift+F5 originally lost to ONI's native light-overlay input. Deferring the
action alone fixed entry but still read the already-replaced light mode when
cycling. The final change remembers the mode before the F5 frame and applies
the queued cycle in `LateUpdate`, including while paused. It clears pending
input outside a colony. It changes no physical settings or readings.

Actual screenshots and status records verify composition, pressure, oxygen
partial pressure, breathability, then normal view. The sampled physical reading
and completed publication remain identical across that cycle. Contamination
appears in the breathability legend and colour even when the trace contributes
no perceptible mixture tint. Ordinary F5 still opens ONI's light overlay.
Candidate r4 adds only the outside-colony pending-input reset to the successful
r3 cycle and passes the broader fresh-game fixture.

## Appearance, inventories and limits

The broader fixture covers normal air, oxygen-rich air, thin air, CO2,
chlorine and vacuum; colour-only mode; paused composition changes; fixture
checkpoint restore; zoom; UI scaling; asteroid switching and return. Additional
cases separate oxygen fraction from partial pressure, CO2 fraction from partial
pressure, equal-mass composition changes and gas/liquid presentation. The
selected pipe, reservoir, bottle, suit, vent and machine surfaces resolve their
explicit conserved inventories. Replacement and retired-container checks pass.
These bindings do not establish production ownership of those devices.

The presentation run records 19,705 frame samples: median 6.237 ms, p95 7.367 ms
and p99 32.165 ms; maximum recorded texture batch is 16.2322 ms. This includes
mostly paused native surroundings, screenshots, UI transitions and private CPU
probes across at most 504 presentation cells. It has no baseline subtraction
and **does not pass the active simulation performance target**. The separate
native gas/water controls provide the GPU binding/readback evidence.

Remaining observations from this cohort:

- Cached-world reuse still has the startup failure above. Fresh generation
  passes; the failed invocation is not relabelled as a pass.
- The reload runner misses the late archive written under the source test
  colony's name. Only each known, newly generated private archive directory was
  moved into its run evidence after verifying its path. The runner needs a
  scoped cleanup repair before claiming automatic restoration for reloads.
- The development boundary outline can cross a hover card at some zooms.
  The status panel also samples the hovered or first supplied cell, which can
  differ from the selected object. Neither is proof of contradictory physical
  readings, but both need presentation polish.
- Whole-colony ownership, active-flow accuracy/performance, production device
  integration and the multi-cycle playable-colony gate remain unverified here.
  No Linux Unity run was added by this Windows cohort.

All seven game invocations were closed normally. Final hash and membership
comparison matches all 288 original personal save/archive files and 13 original
settings/cache files; the 707-file frozen runner is unchanged. The adapter
builds with the two retained assembly-version warnings. Seven hover-evidence
tests and four receipt-protocol tests pass.

Saved-data review: `local/windows-playtest-9eb-r1/CLOSED_RUN_REVIEW.json`, SHA-256
`98a3472f8710b09ea9ab7ede8ebda9d00dce015df437cd76e4f20dfde483c3ba`.
It retains the failed startup and rejected shortcut candidates alongside the
passing reports and screenshot hashes. Licensed game-derived artifacts remain
local. No release or Workshop upload was made.
