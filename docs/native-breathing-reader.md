# Reading owned mixtures for breathing

The native-domain adapter now patches ONI's world breathing-cell reader.
For a cell owned by the custom engine, `GasBreatherFromWorldProvider` receives
the actual available oxygen mass from one completed physical mixture. The
same material and breathing model supplies the hover, overlay and native-reader
decision. Scalar compatibility oxygen and total mixture mass cannot supply a
breathing decision.

The reader applies the configured oxygen fraction/partial pressure, hard-mode
upper oxygen bound, CO2 fraction/partial pressure and contaminant limits. A
rejected atmosphere returns zero breathable mass and the game's vacuum
sentinel. A permitted atmosphere returns only gaseous oxygen mass, rounded
down to binary32 so the advertised amount never exceeds the physical source.
The original six-cell search and duplicant oxygen thresholds remain ONI's
responsibility. Outside the owned domain the original reader runs normally.
Missing physical data faults the owner; a failed owner cannot fall back to
the scalar oxygen projection.

The `breathing-read` control invokes the actual private reader for all six
owned cells and the actual public six-cell selection method with a live
duplicant's `OxygenBreather`. Initial mixed air must be breathable, advertised
oxygen must differ from scalar total mass, and the native selection must pick
the largest permitted oxygen supply. An outside native granite cell retains
its original result. The game then admits 0.125 kg CO2 through its actual
explicit-material `ModifyMass` path. At least one owned cell must fail the CO2
policy while its scalar element remains oxygen. Every native result must
match its completed physical reading. The portable reviewer independently
checks the fixed control policy, mass rounding, cell revisions, selection and
once-only transfer account.

```sh
python3 tools/oni_native_matrix.py \
  --runner-source /path/to/frozen/source --package /path/to/frozen/probe \
  --output artifacts/native-breathing-reader-001 --scenes breathing-read \
  --frames 120 --timeout 1800
```

Linux and Proton expose `native-breathing-read-domain`; the Windows runner uses
`-NativeDomain breathing-read`. Both adapters compile. All 118 actual-declaration
snapshot/message groups pass against Windows and Linux, including the native
helper's inspected signature. The Python runner/evidence suite passes 192
checks with four optional/platform skips. The actual Linux `breathing-reader-unity-matrix-v1` source/save and fresh-process
reload both pass, using frozen `2cdeae7` and native compatibility build v11.
At source frame 5, six mixed-air cells advertise 0.2134052–0.346186668 kg oxygen,
while their scalar total masses are 1.1563–1.1708 kg. The live duplicant's native
search selects the largest available oxygen parcel. At frame 121, five cells
correctly fail both CO2 bounds (failure mask 48); a sixth remains breathable
at CO2 fraction 0.003441 and partial pressure 350.095 Pa. The same native search
selects that remaining cell's 0.308366627 kg oxygen. The 24 actual owned reads,
unchanged outside result and once-only CO2 account pass.

The ordinary source save verifies its complete header and 5,130,012-byte body.
Its physical image hash is
`feac2e080c511bda3350f9a6514ff1c8a7e4142e960931d1f10bb69d6e959947`
and colony-file hash is
`b33fe48e613712d62432e6bcffb8a1d276f2d1e3aab074cb8c09375e650d34f1`.
The fresh process passes its paused frame-zero autosave, both native startup
batches and 120 exact continued physical frames. Its autosave file hash is
`1dd1ed7083ef234d0f20f45d1a241ca35bf1bc55ee24c3bb1a6f6657d58b1a4a`
and final physical image is
`a82f5fffd57d60ccd57bb96ecab31afb5918cad0df3580641a4da8a997f92348`.
The original source stays unchanged. Windows Unity reader validation remains
open.

This implements the atmosphere reader. Production duplicant consumption,
radius/bulk reservations, coupling actual consumed oxygen to metabolic
products, food/body inventories, suit/tube providers and persistent live
actors remain open. In particular, vanilla's early requested-mass CO2 emission
cannot establish a conserved metabolism transaction.
