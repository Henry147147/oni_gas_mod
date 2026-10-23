# Private Linux playtest completion

Status reviewed 2026-10-23 against the accepted [base-game plan](playtest-plan.md)
and its overriding [user amendments](completion-plan.md#amendments).
The mod is **not ready for a full private colony playtest**. Catalogue support
exists for the loaded 29 gases and 49 liquids; that does not establish every
material's gameplay, phase change or machine integration.

`python3 tools/playtest_checklist.py status` lists historical adapter coverage.
Its 2,584 implemented entries are system-level implementation records, not
2,584 independent acceptance results. `gate` also checks the completion items
in [private-playtest-status.json](private-playtest-status.json) and fails while
any remains open. These items record existing requirements; they do not change
physics gates, frozen arenas, bounds or the user's decisions.

| Completion item | Current evidence and remaining work | Owner |
| --- | --- | --- |
| Full mixed gas/liquid pipes and ports | Explicit pump/storage 27/27, atomic world output 10/10 and real teardown 10/10 pass. Normal mixed ownership remains off. Finish vents, recipe-specific consumers/storage/reservoirs, phase damage/returns, all readers and activation. | Pipe worker |
| Heat and germs | Correct pipe body capacity uses registered thermal mass, not construction mass. Corrected thermal equilibrium agrees, but the approximately 1.21 J energy residual exceeds the fixed endpoint bound and remains unresolved. Every germ identity must grow/die by medium; minority retention alone is insufficient, in pipes and world cells. | Pipe worker / gameplay owner |
| Conserved gameplay transactions | Existing life-support, plant, critter, source and machine scenarios pass on the packet path. Verify recipe-only consumption, blocked input/output and power/heat accounting on the final mixed path, including storage, bottles, suits and rocket tanks/cargo. A generic converter preflight is not a per-recipe atomicity proof. | Gameplay owner / pipe worker |
| Exact saves with mixed activity | Historical owner saves/load/overwrite and missing-mod rejection pass. Mixed saves are explicitly refused. Finish the pre-header quiescence barrier, durable ports/retries/component scheduling, in-flight manual/autosaves, cold/menu reload, interrupted-save checks and byte-exact continuation. Old saves retain their recorded profile. | Pipe worker / gameplay owner |
| Whole-asteroid reliability | Every base-game asteroid imports; older Oceania/Volcanea checks faulted. Repeat their bounded checks on the selected saved profile. The prepared profile-18 late colony stalls after frame 4,383; pass that region before resuming ten cycles. One legacy transfer fault at frame 456 did not recur in a 601-frame retry and is not claimed fixed. | Solver / gameplay owners |
| Numerical acceptance for the selected profile | F1 static split currently fails the vanilla comparison. F2 requires accepted external-heat/kinetic correction; F3/F5 dynamic references and compression/phase corrections remain open; F4 lacks a completed aligned solid/fluid measurement. Finish affected evaporation, steam and freeze/melt checks. New kernel integration is diagnostic until accepted. | Solver / gameplay owners |
| Ordinary player build and presentation | Select only an accepted profile for new colonies; no development fixture required. Verify normal startup, full world/pipe/storage/spill inspection and sensors, responsive pause/menu, clean shutdown, clear faults and missing/incompatible-mod saves. | Gameplay owner |
| Ten-cycle colony acceptance and package | After the shorter failure-region check passes, run the prepared colony ten cycles with every duplicant alive, no owner fault/fallback and closed ledgers, including autosaves. Validate one immutable final bundle, then provide private Linux install/run/known-behaviour instructions and hashes. | Gameplay owner |

The separate Terra 2× target remains open. Latest clean whole-game measurements
are approximately 169 ms owner advance / 258 ms observed frame spacing; the
private approximately 83 ms kernel prototype excludes owner/publication work
and has no game-speed acceptance. Report mean/p95/max, completed frames,
faults/fallback availability, mass/energy errors and trace totals for final
runs, with instrumentation off and explicit CPU/game windows. The accepted
private-playtest scope does not make real-time speed a feature-cutting gate.

Windows, DLC, Workshop publication and the broader public-release matrix follow
this private Linux base-game playtest. No public release is authorized here.
