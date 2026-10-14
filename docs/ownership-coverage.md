# Ownership coverage and cluster staging

Milestone A is **in progress**, not complete. The public release gate is closed.
The [accepted completion plan](completion-plan.md) remains the execution target.

## Executable inventory

`coverage-inventory.json` currently contains 590 entries: 212 material
identities, 62 native messages, 16 native imports, 47 snapshot/event/texture
channels, 215 observed call sites and 38 required gameplay systems. Every entry
names its intended owner, adapter and acceptance requirement. Adapter names
with `unimplemented` status are planned responsibilities, not existing code.

The inventory is a conservative list of known work, not a completeness proof.
Material identities came from one private Windows capture. The checked-in data
contains identifiers and phase classifications, not game property tables or
native payloads. Static call-site labels in the historic audit do not distinguish
overloads, resolve indirect dispatch, enumerate prefabs/recipes, or establish
DLC applicability. In particular, a repeated generic adapter name is not proof
that each originating gameplay action has an atomic transaction.

All 32 gameplay DLC masks are explicit on both operating systems. A passing
runtime registry capture and per-entry in-game evidence are required for each
configuration. Absence requires a registry capture; native interfaces and
observed call sites cannot be waived as absent. Unit tests never count as
in-game acceptance. Cosmetic compatibility and numerical/performance/long-run
requirements remain separate release evidence.

```sh
python3 tools/coverage_inventory.py validate
python3 tools/coverage_inventory.py status
python3 tools/coverage_inventory.py gate
python3 tools/coverage_inventory.py gate --release
```

`validate` checks inventory integrity, source drift, omissions and evidence
shape and exits zero for a consistent *incomplete* inventory. `gate` exits one
until acceptance is complete; malformed/stale inputs exit two. The normal build
runs validation and regression tests, not the deliberately failing release
gate. A ledger of artifact hashes is an audit index, not independent proof that
the underlying in-game experiments passed; review the referenced private runs.

To regenerate from an inspected private identity capture, write a new candidate
and review it before replacing the checked-in inventory. Generation refuses to
overwrite existing evidence:

```sh
python3 tools/coverage_inventory.py generate \
  --materials /private/game-contract-local/contract.json \
  --inventory /tmp/coverage-candidate.json
```

## Implemented staging boundary

`NativeColonyImport.StageNewColony` consumes owned copies of length-prefixed
native v15 world blobs and an independently supplied expected cluster layout.
It validates count, identities, dimensions, placement, nonoverlap, outer length,
every inner blob and every fluid material/phase binding before returning any
result. Unknown bindings, guard fluids and fluid backwalls reject the import.
All source buffers remain unchanged even if the final world fails. Returned
worlds retain separate captured fluid records, sanitized native payloads and
correct outer prefixes. Public buffers use read-only spans; exported copies and
later changes to caller dictionaries do not change staged state.

This is a **staging API**, not a native rollback protocol. No live Unity loader
hook is enabled. A production loader must still validate material property
support, stage custom inventories, filter definitions before native Load,
handle partial native-initialization failures, publish merged owned buffers,
and reject incompatible saves. A binding string alone does not establish a
complete material model.

`NativeElementTable.SuppressFluidProduction` validates the inspected 164-byte
element rows against all expected indices/hashes/phases and rejects invalid
transition references and byproducts. It copies the table and preserves its
opaque names/tail. It disables an entire temperature-transition branch if its
target **or byproduct** is fluid, and disables both sublimation and conversion
when either product is fluid. Solid-only branches remain unchanged. Suppressed
branches are reported explicitly. The caller must verify the game binary before
using this version-specific interface.

Definition filtering does not replace transitions, their energy accounting, or
native/managed emitters, chunks, buildings, backwalls, interactions and recipes.
Those remain unresolved ownership gates. The standalone harness now uses the
shared filter and staged import before its native load; its bindings describe
captured test inventories, not production thermodynamic support.

## Verification from this implementation

- 20 managed/native scenarios pass on Linux and Windows, including reordered
  multiple worlds, a corrupted last world, overlapping/missing worlds, framing,
  unknown mappings, owned buffers, byproduct-only fluid transitions, conversion
  remnants, idempotence and malformed late element rows.
- 12 Python regression checks pass for omitted interfaces/materials, source
  drift, missing ownership/tests, DLC matrix corruption and invalid or
  insufficient acceptance evidence.
- All six isolated Linux native experiments pass with the shared staging and
  filtering code. This includes the expected sanitation failure, plus actual
  native import, solid heat transfer and filtered/unfiltered ice controls.
  Private evidence: `/mnt/data/share/oni/mod-development/shadow-evidence-cluster-staging-v1`.
- Existing 56 native reference scenarios still pass. The ONI probe builds
  against the licensed assemblies with its two existing Unity reference warnings.

No disposable colony ownership, integrated multiphase solver, gameplay
conversion or release gate has been certified by these checks.
