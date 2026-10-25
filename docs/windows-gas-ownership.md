# Windows gas phase ownership (build 744825)

The missing `MA_ONI_GAS`/`MA_ONI_GAS_PAIR` RVAs do not by themselves require a
new containing-phase detour. Inspection of the pinned Windows image and its
matching PDB finds no emitted procedure **or inline site** for either
`DoGasInteraction` overload. A type/ID record is not an executable call site.
Two endpoint guards are required in the existing pressure and postprocess
hooks. The compiled bulk gas path is described below. Do not skip `UpdateData`: that
would also skip native services the owner still needs.

This is specific to SHA256
`031960f120ac1d59fd8e43f6e1a7369111c629b90c24d52186b691dfe2d8a727`,
PDB GUID `7ca64cfd-5754-4a86-9b05-4be74c76f7a3`, age 1.
Only addresses, sizes, fingerprints and original analysis are recorded here;
the licensed image, PDB and disassembly stay private.

## Inspected execution boundaries

| Boundary | RVA | Gas work and existing interception |
| --- | --- | --- |
| `SimBase::UpdateData` | `0x45d10` | Contains pressure selection and displacement passes, native temperature-task scheduling/completion, cell copies, components, disease, radiation and events. The backend writes its colony projection before this call and captures native coupling afterwards. |
| `UpdatePressure` | `0x4aea0` | Called at `0x462cc`, `0x463de`, `0x46513`. Reads old element/mass and accumulated temperature; writes target mass/temperature/element/disease and substance events. Its final integer is the **other cell**, including the diagonal selected by the third call site. The pressure hook must check both endpoints; checking only the source and its cardinal neighbourhood misses this diagonal call. |
| `DoGasPressureDisplacement` | `0x4ab50` | Called at `0x4671f`, `0x467e2`, `0x4689e`, `0x46959`. Takes three cell indices. Existing `MA_ONI_GAS_DISPLACEMENT` checks all three. |
| `UpdateLiquid` | `0x498b0` | Separate liquid transport. The existing liquid hook skips owned source cells. Liquid displacement has its own three-cell hook. |
| `SimUpdateTemperatureTask::InternalDoTask` | `0x458d0` | Runs cell-temperature pairs, state transitions and backwall heat. The temperature and transition hooks remain the coupling boundaries. |
| `PostProcessCell` | `0x4daf0` | Gas cleanup, density displacement, partial heat/phase transitions and random postprocess work; other branches process liquids and unstable solids. The containing-phase hook skips owned fluid cells and unowned fluids touching ownership, while forwarding native solids. |

The pressure and gas-displacement loops outside the callees write native flow
vectors using the callees' returned transfer. Returning zero suppresses those
flow contributions as well as the callee's mass, heat, identity and event
writes. The enclosing loops still inspect old/new identities, phase metadata,
properties and neighbour indices. `UpdateData` is not a fluid-only function.

The gas-interaction table at RVA `0x115650` has references only in startup,
interaction-table loading and destruction; no simulation reader was found.
The PDB retains the per-cell/pair function IDs and their bool signatures but
no procedure, call or inline-site record. Independently, the pinned Linux
image has no caller of its per-cell `DoGasInteraction` export; that unused
wrapper calls its pair export. This explains why installing those Linux hooks
has not produced entries in the forwarding probes. Describing the missing
Windows functions as necessarily inlined was an unsupported inference.

Full containing-function fingerprints used in the inspection:

| RVA | Bytes | SHA256 |
| --- | ---: | --- |
| `0x45d10` | 7397 | `56fb5269a258c11373132730003efdf6fbfe3ee95569daedcb5c341957eb01a3` |
| `0x4daf0` | 4035 | `647df7bc70692ca1aa0dd0dd2b949b42b0f0addd360483a0db8574e3b40b3f54` |
| `0x458d0` | 1084 | `e5491d3f383d14470da777bd37e7a5d8711f91b4fa04ef1317831914283f8843` |

These are inspection evidence, not additional installed detours. Runtime
attachment still verifies the complete file hash and the existing entry
fingerprints from `native-hook-profiles.json`.

## Why the previous domain tests passed

Both `NativeShadowProbe/SealedDomainControls.cs` and the Unity
`NativeSealedDomainValidation` explicitly set cell properties mask 3 (gas and
liquid impermeability). The harness requires those bits before accepting exact
native compatibility tuples. That fence plus the phase hooks is legitimate
bounded-domain evidence, but it cannot prove the colony owner's flag-free
path. The colony owner writes real scalar fluid projections before native
`UpdateData`, then books changes into its own mass/heat accounts; an accidental
native fluid update would otherwise look like a coupling transaction.

`STEP` and `EMITTER_DISPATCH` are forwarding counters, not suppression gates in
the current backend. The native frame fence is established by `NEXT_FRAME`,
advancement occurs at `UPDATE_DATA`, and the fence is released by `END_FRAME`.
The Windows actor forwarding probe checks that the canonical emitter hook
actually receives the native virtual dispatch. A second emitter hook or a
replacement of the outer step would not add ownership to this implementation.

## Boundary corrections and real-colony reachability

`UpdatePressure` now checks its final destination argument as well as the
source. The previous `beside_owned(source)` test only checked cardinal
neighbours and missed diagonal writes. Its conservative cardinal policy stays.

`PostProcessCell` is the containing phase for density displacement
(`DoDensityDisplacement`, RVA `0x4b5f0`, 509 bytes) and lateral gas swapping
(the direct call at `0x4ea22`). Linux also contains inlined cell-swap work, so
a detour of only the Windows density helper would be incomplete. The existing
postprocess hook now also skips an **unowned fluid** source touching an owned
cardinal neighbour. It reads the updated native element through the existing
verified CellSOA accessor (Windows element offset 8, Linux 0) and classifies
it with the imported fluid-only colony catalogue, the same mapping used by
shadow capture. Native solid sources continue through the original function.
No hook index, observer ABI, profile RVA or fingerprint changes.

This boundary is reachable in a real colony. Ownership states 0 (closed),
2 (closing) and 3 (opening) are not suppressed or shadowed. A native solid can
become fluid during an update before the owner applies its queued opening.
Usually that interval is one update; refused terrain edits can last longer.
Skipping this transient fluid border's postprocessing extends the existing
pressure boundary policy. It deliberately also pauses that source's native
fluid work for the interval; it does not promise identical unowned random
sequences or fluid evolution with an active owner.

Scalar capture is insufficient protection. In an isolated synthetic capture
experiment, a mixed owned cell held 1 kg O2 and 0.2 kg CO2, and a native solid
became 4 kg CO2 above it. Swapping the native scalar endpoints then running
real capture/adoption left **1.2000000476837158 kg O2 and 4 kg CO2**, with state
1 and no fault. Capture extracted the actual mixture but adopted only its
dominant-element proxy at the other endpoint. Scalar mass books balanced while
minority species changed. Preventing the native fluid swap preserves the
owner's real mixture.

## Focused controls and results

`NativeShadowProbe` adds six explicit experiments. Existing scenes, gates and
acceptance bounds are unchanged. The controls use the actual colony ABI with
new-colony kernel flags 12, diagnostic fluid catalogues, no impermeability
flags, no definition filtering and no correction of native tuples after a
frame. Gas/water rooms compare against native evolution from the same save;
remote unowned flow and native solid heat must remain active.

| Control | Linux before correction | Linux after both guards |
| --- | --- | --- |
| `colony-gas-hooks` | Pass | Pass, 60 frames |
| `colony-water-hooks` | Pass | Pass, 60 frames |
| `colony-gas-boundary-hooks` | Frame 0 owned mass `1.4000001 -> 1.7120001` kg | Pass, 60 frames |
| `colony-gas-density-hooks` | Frame 2 owned O2 `1.4000001` kg replaced with CO2 `4` kg | Pass, 60 frames |
| `colony-gas-lateral-hooks` | Separate lateral case added after discovery | Pass, 60 frames |
| `colony-gas-transition-hooks` | Supported-solid adoption control added after discovery | Pass: O2 `1`, total gas/liquid CO2 `4.2` kg; native input `4` kg, output/heat zero; state 1 |

The first five require 360 exact owned mass/temperature/element projections,
germ-free owned tuples and zero native mass/heat/change bookings each. The
owner must evolve the initial pressure gradient. The transition control uses
a supported solid CO2 cell beside an isolated mixed cell; the native first
transition produces liquid CO2, which the owner adopts. Its phase catalogue
includes O2, CO2 and liquid CO2. Native solid work remains enabled.

Skipping owned postprocessing changes shared native direction/RNG state
(SimData offsets `0x14`/`0x0c`), which affects later unowned updates. These
pre-existing Linux differences are diagnostic; owner-free 60-frame replay
from the same save passed exactly (`MA_COLONY_HOOK_REPLAY_CONTROL=1`).

All 86 Linux native CTests passed both before and after the guards. The
rebuilt `oni_colony_owner` CTest and nine hook-profile Python tests also pass.

Windows binaries under Wine reproduce the diagonal failure with exactly the
same values as Linux. The density case fails at frame 0, replacing owned O2
with 4 kg CO2. With both guards (the Windows worker's `guarded-v3` package),
all six controls write successful physical reports, including 1 kg O2 and
4.2 kg total CO2 in the transition control. The five isolation controls each
retain 360 exact owned projections and zero native bookings. All six
unhooked/observed forwarding comparisons, the native-body-heat comparison,
and the existing sealed gas/water controls pass and terminate normally.
The actor comparison exercises the Windows canonical emitter without STEP or
the secondary emitter-dispatch hook.

Colony probes hang **after** writing their completed report on both the
before/after Windows packages, so their process-level suites correctly time
out rather than passing. The suspected cause is a persistent solver thread
joined by the DLL's global destructor. The Windows worker owns this separate
lifecycle blocker; no forced exit was added to the harness. Physical report
success is not a clean-process-exit claim. Wine is also distinct from actual
Windows OS/MSVC or Unity acceptance; no Unity game is launched here.

## Falling solids

`MA_COLONY_TRANSITION_FALLING_CONTROL=1` places solid CO2 above the owned
mixed cell. Native solid work can transition first and swap later within
`UpdateData`, beyond one `PostProcessCell` call. The owner observes solid
sources at transition/postprocess entry and resolves the displaced projections
after the full native update. It keeps their exact mixtures and admits only
new melt; heat and germs following a displaced copy return to its real owner.
Ambiguous matches fault visibly instead of guessing an inventory.

The Linux control retains 1 kg O2 and 4.2 kg total CO2, with exactly 4 kg native
input and no native output or heat. Native controls also cover same-element
melt, heat/germs on the moved copy, and ambiguous endpoints. This does not claim
complete colony acceptance; other native gameplay writers still require the
broader Windows validation.
