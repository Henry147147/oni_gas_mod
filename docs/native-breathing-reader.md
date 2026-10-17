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
checks with four optional/platform skips. Real Unity source/save and reload
validation remains pending.

This implements the atmosphere reader. Production duplicant consumption,
radius/bulk reservations, coupling actual consumed oxygen to metabolic
products, food/body inventories, suit/tube providers and persistent live
actors remain open. In particular, vanilla's early requested-mass CO2 emission
cannot establish a conserved metabolism transaction.
