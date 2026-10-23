# Ownership coverage and cluster staging

Milestone A of the [completion plan](completion-plan.md) is **in progress**; the
release gate is closed. The production mechanism is
[native SimDLL hooks](native-backend-integration.md). Hook acceptance must audit
readers/writers and trace actual execution; projected scalar fluids are allowed,
and observer attachment cannot satisfy ownership gates. The staging and filter
code described below comes from the earlier vacuum-shadow work and is not an
enabled production importer.

## Playtest checklist

The [base-game playtest](playtest-plan.md) scopes this inventory with
`tools/playtest_checklist.py`: `apply` marks every entry in or out from the
base-game Linux registry (`runtime-identities-linux-base.json`, captured in game
with no DLC loaded), assigns each in-scope entry to one gameplay system, and
copies the system's status from the script's table of implemented systems with
their sources and in-game check; `status` prints implementation coverage plus
the separate [private-playtest acceptance blockers](private-playtest-release.md).
`gate` fails until both are complete. Broad system checks do not certify every
recipe, the final mixed-pipe path or a releasable package. Out-of-scope
entries name their reason (DLC content, Spaced Out! radiation, ONI's unchanged
light model). Entries without fluid interaction (`native`) need no adapter.

## Executable inventory

`coverage-inventory.json` contains 3,074 entries: 212 material identities,
62 native messages, 16 native imports, 31 native-hook responsibilities, 47
snapshot/event/texture channels, 215 observed call sites, 39 gameplay systems,
2,143 prefab registrations, 34 prefab converters and 275 recipe registrations.
Every entry names its intended owner, adapter and acceptance requirement.
Adapter names with `unimplemented` status are planned responsibilities, not
existing code; today every entry is `unimplemented`.

The two [publication bindings](native-domain-publication.md) are included among
the native-hook responsibilities. They leave the observer's 29 public hook
indices unchanged and still require full colony acceptance evidence.

The inventory is a conservative list of known work, not a completeness proof:

- Material identities came from one private Windows capture. The checked-in
  data contains identifiers and phase classifications, not game property tables
  or native payloads; numerical recipe/converter data stays outside Git.
- Static call-site labels do not distinguish overloads, resolve indirect
  dispatch or establish DLC applicability. A repeated generic adapter name does
  not prove that each originating action has an atomic transaction.
- All 32 gameplay DLC masks are explicit on both operating systems. Each
  configuration needs a passing runtime registry capture and per-entry in-game
  evidence. Absence requires a registry capture; native interfaces and observed
  call sites cannot be waived as absent. Unit tests never count as in-game
  acceptance. Cosmetic compatibility and numerical/performance/long-run
  requirements are separate release evidence.
- Native-hook entries additionally require explicit authoritative fluid
  ownership and complete reader/writer coverage; forwarding observation cannot
  pass them. Windows has no standalone `Step` symbol, so that obligation needs
  proof of the enclosing phase rather than an absence waiver.

```sh
python3 tools/coverage_inventory.py validate
python3 tools/coverage_inventory.py status
python3 tools/coverage_inventory.py gate
python3 tools/coverage_inventory.py gate --release
```

`validate` checks inventory integrity, source drift, omissions and evidence
shape and exits zero for a consistent *incomplete* inventory. `gate` exits one
until acceptance is complete; malformed or stale inputs exit two. The normal
build runs validation and regression tests, not the deliberately failing
release gate. A ledger of artifact hashes is an audit index, not independent
proof that the underlying in-game experiments passed.

Runtime identities are in `runtime-identities.json`. To extract a new source
without copying numerical game data:

```sh
python3 tools/runtime_inventory.py --capture /private/game-contract-local/runtime-coverage.json \
  --output /tmp/runtime-identities-candidate.json
```

Review and replace `docs/runtime-identities.json` before regenerating the main
inventory. Duplicate names are distinct registrations with separate occurrence
identities (the observed registry has 18 repeated prefab names and 24 repeated
recipe names); deduplicating them would hide work. Occurrence identities are
discovery labels, not permanent save or material identifiers.

To regenerate from an inspected private material capture, write a new candidate
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
Before returning any result it validates count, identities, dimensions,
placement, nonoverlap, outer length, every inner blob and every fluid
material/phase binding. Unknown bindings, guard fluids and fluid backwalls
reject the import. Source buffers never change, even when the final world
fails. Returned worlds retain separate captured fluid records, sanitized native
payloads and correct outer prefixes behind read-only spans.

This is a **staging API**, not a native rollback protocol, and no live loader
hook uses it. A production loader must still validate material property
support, stage custom inventories, filter definitions before native Load,
handle partial native-initialization failures, publish merged owned buffers and
reject incompatible saves. A binding string alone does not establish a material
model.

`NativeElementTable.SuppressFluidProduction` validates the inspected 164-byte
element rows against all expected indices, hashes and phases and rejects
invalid transition references and byproducts. It copies the table, preserving
opaque names and tail bytes. It disables a whole temperature-transition branch
if its target **or byproduct** is fluid, and disables both sublimation and
conversion when either product is fluid; solid-only branches are unchanged and
suppressed branches are reported. The caller must verify the game binary before
using this version-specific interface. Definition filtering does not replace
transitions, their energy accounting, or native/managed emitters, chunks,
buildings, backwalls, interactions and recipes; those remain ownership gates.

## Verification

- Managed/native staging and filter scenarios pass on Linux and Windows
  (reordered and corrupted worlds, overlap, framing, unknown mappings, owned
  buffers, byproduct-only transitions, conversion remnants, malformed rows).
- Python regression checks cover omitted interfaces/materials, source drift,
  missing ownership/tests, DLC matrix corruption, insufficient evidence,
  runtime identities, duplicate labels and exclusion of numerical properties.
- The isolated [native harness](../tools/NativeShadowProbe/README.md) passes its
  import, solid-heat and filtered/unfiltered ice controls with this code on
  Windows and Linux, plus the expected sanitize failure.
- The Windows main-menu probe captured the runtime registry (2,143 prefabs with
  2,125 distinct labels, 34 converters, 275 recipes with 251 distinct labels).
  Loaded content reported only `EXPANSION1_ID`; that is not a colony's DLC mask
  and counts as no gameplay-matrix acceptance.

No disposable-colony ownership, multiphase solver, gameplay conversion or
release gate is certified by these checks.
