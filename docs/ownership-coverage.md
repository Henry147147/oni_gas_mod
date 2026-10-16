# Ownership coverage and cluster staging

Milestone A is **in progress**, not complete. The public release gate is closed.
The [accepted completion plan](completion-plan.md) remains the execution target.
The selected mechanism is now [native SimDLL hooks](native-backend-integration.md).
Staging/filter controls below are historical vacuum-shadow work, not an enabled
production importer. Hook acceptance must audit readers/writers and trace actual
execution; scalar projected fluids are allowed. Observer attachment cannot
satisfy ownership gates.

## Executable inventory

`coverage-inventory.json` currently contains 3,071 entries: 212 material
identities, 62 native messages, 16 native imports, 28 native-hook responsibilities,
47 snapshot/event/texture channels, 215 observed call sites, 39 gameplay systems, 2,143 prefab
registrations, 34 prefab converters and 275 recipe registrations. Every entry
names its intended owner, adapter and acceptance requirement. Adapter names
with `unimplemented` status are planned responsibilities, not existing code.

The inventory is a conservative list of known work, not a completeness proof.
Material identities came from one private Windows capture. The checked-in data
contains identifiers and phase classifications, not game property tables or
native payloads. Static call-site labels in the historic audit do not distinguish
overloads, resolve indirect dispatch, or establish DLC applicability. Runtime
identities are recorded in `runtime-identities.json`; the private numerical
recipe/converter data stays outside Git. In particular, a repeated generic adapter name is not proof
that each originating gameplay action has an atomic transaction.

All 32 gameplay DLC masks are explicit on both operating systems. A passing
runtime registry capture and per-entry in-game evidence are required for each
configuration. Absence requires a registry capture; native interfaces and
observed call sites cannot be waived as absent. Unit tests never count as
in-game acceptance. Cosmetic compatibility and numerical/performance/long-run
requirements remain separate release evidence.

Native-hook evidence additionally requires explicit authoritative fluid ownership
and complete reader/writer coverage. Forwarding observation cannot pass those
entries. Windows has no standalone `Step` symbol: that obligation requires proof
of the enclosing phase, rather than an absence waiver.

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

To extract a new runtime identity source without copying numerical game data:

```sh
python3 tools/runtime_inventory.py --capture /private/game-contract-local/runtime-coverage.json \
  --output /tmp/runtime-identities-candidate.json
```

Review and replace `docs/runtime-identities.json` before regenerating the main
inventory. Duplicate names represent distinct registrations and retain separate
occurrence identities. The observed registry has 18 repeated prefab names and
24 repeated recipe names; silently deduplicating them would hide work. Occurrence
identities are discovery labels, not permanent save or material identifiers.

To regenerate from an inspected private material identity capture, write a new candidate
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
- 17 Python regression checks pass for omitted interfaces/materials, source
  drift, missing ownership/tests, DLC matrix corruption and invalid or
  insufficient acceptance evidence, runtime identities, duplicated registration
  labels, uninitialized registries and exclusion of numerical game properties.
- All six isolated Windows and Linux native experiments pass with the shared staging and
  filtering code. This includes the expected sanitation failure, plus actual
  native import, solid heat transfer and filtered/unfiltered ice controls.
  Private evidence: `/mnt/data/share/oni/mod-development/shadow-evidence-cluster-staging-v1`.
  Windows evidence: `/mnt/data/share/oni/mod-development/ownership-validation-9baec3f8`.
- Existing 56 native reference scenarios still pass. The ONI probe builds
  against the licensed assemblies with its two existing Unity reference warnings.
- The updated Windows Unity probe passes its six scoped checks using the
  native56 library and reaches the main menu through Steam. It captures 2,143
  prefabs (2,125 distinct labels), 34 ElementConverters and 275 recipes (251
  distinct labels), with no missing prefab components. Loaded content reported
  only `EXPANSION1_ID`; this is not a colony's selected DLC mask and counts as
  no gameplay-matrix acceptance. Its temporary mod is archived, no colony was
  loaded, and mods/preferences were restored byte-for-byte. Evidence:
  `/mnt/data/share/oni/mod-development/runtime-discovery-steam-e62f3081`.

An earlier direct-executable launch failed Steam initialization before the menu.
Steam's automatic restart also failed during native startup. Both task-owned
processes were closed and settings restored before the successful Steam launch.
Those attempts provide no registry or colony evidence.

No disposable colony ownership, integrated multiphase solver, gameplay
conversion or release gate has been certified by these checks.
