"""Private readiness must not be inferred from historical adapter coverage."""
import copy
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import playtest_checklist as checklist


class PrivateReadinessTests(unittest.TestCase):
    def setUp(self):
        self.inventory = json.loads(checklist.INVENTORY.read_text())
        self.acceptance = json.loads(checklist.PRIVATE_STATUS.read_text())

    def test_all_implemented_still_blocks_unaccepted_private_release(self):
        for row in self.acceptance["requirements"]:
            row["status"] = "open"
        for entry in self.inventory["entries"]:
            if entry.get("playtest") == "in":
                entry["status"] = "implemented"
        report = checklist.readiness(self.inventory, self.acceptance)
        self.assertFalse(report["private_playtest_ready"])
        self.assertEqual(set(report["open_acceptances"]), checklist.PRIVATE_REQUIREMENTS)
        self.assertEqual(report["open_systems"], [])

    def test_missing_duplicate_and_unrecognized_requirements_refuse(self):
        for mutate in (lambda rows: rows.pop(), lambda rows: rows.append(rows[0]),
                       lambda rows: rows[0].update(id="unrelated")):
            candidate = copy.deepcopy(self.acceptance)
            mutate(candidate["requirements"])
            with self.assertRaises(ValueError):
                checklist.readiness(self.inventory, candidate)

    def test_passed_acceptances_still_need_complete_implementation_scope(self):
        for row in self.acceptance["requirements"]:
            row["status"] = "passed"
        for entry in self.inventory["entries"]:
            if entry.get("playtest") == "in":
                entry["status"] = "implemented"
        self.assertTrue(checklist.readiness(self.inventory, self.acceptance)["private_playtest_ready"])
        entry = next(row for row in self.inventory["entries"] if row.get("playtest") == "in")
        entry["status"] = "unimplemented"
        self.assertFalse(checklist.readiness(self.inventory, self.acceptance)["private_playtest_ready"])
        entry["status"] = "implemented"
        entry.pop("playtest")
        self.assertFalse(checklist.readiness(self.inventory, self.acceptance)["private_playtest_ready"])

    def test_deferred_acceptances_need_a_recorded_decision(self):
        for row in self.acceptance["requirements"]:
            row["status"] = "passed"
            row.pop("decision", None)
        for entry in self.inventory["entries"]:
            if entry.get("playtest") == "in":
                entry["status"] = "implemented"
        row = self.acceptance["requirements"][0]
        row["status"] = "deferred"
        with self.assertRaises(ValueError):
            checklist.readiness(self.inventory, self.acceptance)
        row["decision"] = "docs/missing-decision.md"
        with self.assertRaises(ValueError):
            checklist.readiness(self.inventory, self.acceptance)
        row["decision"] = "docs/completion-plan.md"
        report = checklist.readiness(self.inventory, self.acceptance)
        self.assertTrue(report["private_playtest_ready"])
        self.assertEqual(report["deferred_acceptances"], [row["id"]])
        self.acceptance["requirements"][1]["status"] = "open"
        self.assertFalse(checklist.readiness(self.inventory, self.acceptance)["private_playtest_ready"])

    def test_invalid_scope_status_or_evidence_cannot_pass(self):
        candidates = []
        for changes in ({"status": "waived"}, {"evidence": "../private/save.sav"},
                        {"evidence": "docs/missing-acceptance.md"}):
            candidate = copy.deepcopy(self.acceptance)
            candidate["requirements"][0].update(changes)
            candidates.append(candidate)
        candidate = copy.deepcopy(self.acceptance)
        candidate["scope"] = "public-release"
        candidates.append(candidate)
        for candidate in candidates:
            with self.assertRaises(ValueError):
                checklist.readiness(self.inventory, candidate)

    def test_linux_acceptance_does_not_pass_unverified_windows_player(self):
        for row in self.acceptance["requirements"]:
            row["status"] = "open" if row["id"] == "windows-player-acceptance" else "passed"
        for entry in self.inventory["entries"]:
            if entry.get("playtest") == "in":
                entry["status"] = "implemented"
        report = checklist.readiness(self.inventory, self.acceptance)
        self.assertFalse(report["private_playtest_ready"])
        self.assertEqual(report["open_acceptances"], ["windows-player-acceptance"])

    def test_base_game_passes_cannot_certify_the_required_spaced_out_playtest(self):
        for row in self.acceptance["requirements"]:
            row["status"] = "open" if row["id"] == "spaced-out-cluster-playtest" else "passed"
        for entry in self.inventory["entries"]:
            if entry.get("playtest") == "in":
                entry["status"] = "implemented"
        report = checklist.readiness(self.inventory, self.acceptance)
        self.assertFalse(report["private_playtest_ready"])
        self.assertEqual(report["open_acceptances"], ["spaced-out-cluster-playtest"])
        self.acceptance["scope"] = "linux-windows-base-game-private-playtest"
        with self.assertRaises(ValueError):
            checklist.readiness(self.inventory, self.acceptance)


class ReleaseScopeTests(unittest.TestCase):
    """The release scope is the base game plus Spaced Out!; nothing stays unexplained."""

    def setUp(self):
        self.inventory = json.loads(checklist.INVENTORY.read_text())
        self.entries = {entry["id"]: entry for entry in self.inventory["entries"]}
        self.base = json.loads(checklist.BASE.read_text())
        self.spaced_out = json.loads(checklist.SPACED_OUT.read_text())

    def apply(self, scope="release"):
        candidate = copy.deepcopy(self.inventory)
        checklist.apply(candidate, self.base, self.spaced_out if scope == "release" else None)
        return candidate

    def test_checked_inventory_is_the_release_scope_and_is_reproducible(self):
        self.assertEqual(self.inventory["playtest_scope"], "release")
        self.assertEqual(self.apply()["entries"], self.inventory["entries"])

    def test_every_registry_identity_is_in_scope_and_only_dispositions_stay_out(self):
        out = [entry for entry in self.inventory["entries"] if entry["playtest"] == "out"]
        self.assertTrue(out)
        for entry in out:
            self.assertIn(entry["playtest_disposition"], checklist.OUT_DISPOSITIONS)
            self.assertEqual(entry["status"], "unimplemented")
            self.assertTrue(entry["playtest_reason"])
        for identity in (item["id"] for item in self.spaced_out["entries"]):
            self.assertNotEqual(self.entries[identity].get("playtest_reason"), "not in the base-game registry")
            self.assertTrue(self.entries[identity]["playtest"] == "in" or self.entries[identity]["playtest_disposition"])

    def test_base_scope_is_still_reproducible(self):
        base = self.apply("base")
        self.assertEqual(base["playtest_scope"], "base")
        spaced_out_only = {item["id"] for item in self.spaced_out["entries"]} - {item["id"] for item in self.base["entries"]}
        for identity in spaced_out_only:
            entry = next(item for item in base["entries"] if item["id"] == identity)
            self.assertEqual((entry["playtest"], entry["status"]), ("out", "unimplemented"))
        self.assertFalse([item for item in base["entries"] if item.get("playtest_disposition")])

    def test_refused_pack_entries_are_never_implemented(self):
        refused = [entry for entry in self.inventory["entries"] if entry.get("playtest_disposition") == "refused-pack"]
        self.assertIn(self.entries["system/bionics"], refused)
        self.assertEqual({entry["playtest_pack"] for entry in refused}, {"Bionic", "Frosty", "Aquatic"})
        for entry in refused:
            self.assertEqual((entry["playtest"], entry["status"]), ("out", "unimplemented"))
            self.assertIn("stays on ONI's own simulation", entry["playtest_reason"])
            self.assertNotIn("implementation", entry)
        bionic = {entry["fact"]["method"].split("+")[0].split(".")[0] for entry in refused if entry["kind"] == "managed-callsite" and entry["playtest_pack"] == "Bionic"}
        self.assertEqual(bionic, checklist.REFUSED_CLASSES["Bionic"])
        # Passing every scenario cannot implement a refused entry.
        everything = {name for _, names in checklist.SCENARIO_SYSTEMS.values() for name in names}
        candidate = copy.deepcopy(self.inventory)
        passed, checklist.PASSED_SCENARIOS = checklist.PASSED_SCENARIOS, everything
        before = copy.deepcopy(checklist.SYSTEMS)
        try:
            checklist.SYSTEMS.update(checklist.scenario_systems(everything))
            checklist.apply(candidate, self.base, self.spaced_out)
        finally:
            checklist.PASSED_SCENARIOS = passed
            checklist.SYSTEMS.clear(); checklist.SYSTEMS.update(before)
        for entry in candidate["entries"]:
            if entry["playtest"] == "out": self.assertEqual(entry["status"], "unimplemented")

    def test_refusal_is_covered_by_a_guard_at_every_start_and_load_path(self):
        for name in ("ColonyOwner.cs", "ColonyOwnerPersistence.cs"):
            self.assertIn("GameplayPacks.ActiveUnsupported()", (ROOT / "managed/MixedAtmospheres.Oni" / name).read_text(), name)
        controls = (ROOT / "tools/DistributionTests/Program.cs").read_text()
        for needle in ('"DLC2_ID", "DLC3_ID", "DLC4_ID", "DLC5_ID"', "PacksColonyNotice", "PacksLoadRefusal", "refused with Spaced Out!"):
            self.assertIn(needle, controls)

    def test_light_is_kept_vanilla_by_decision_and_disabled_elements_never_exist(self):
        self.assertEqual(self.entries["system/light"]["playtest_disposition"], "kept-vanilla")
        self.assertIn("never absorb light", self.entries["system/light"]["playtest_reason"])
        phases = {entry["fact"]["name"]: entry["fact"]["phase"] for entry in self.inventory["entries"] if entry["kind"] == "material"}
        self.assertTrue(checklist.DISABLED_ELEMENTS <= set(phases))
        for name, phase in phases.items():
            entry = self.entries["material/" + name]
            if name in checklist.DISABLED_ELEMENTS: self.assertEqual(entry["playtest_disposition"], "disabled-element")
            else: self.assertEqual(entry["playtest"], "in", name)
        for name in checklist.SPACED_OUT_FLUIDS:
            self.assertIn(phases[name], checklist.FLUID_PHASES)
            self.assertEqual(self.entries["material/" + name]["playtest_system"], "spaced-out-materials")

    def test_each_spaced_out_system_flips_only_with_all_of_its_scenarios(self):
        for system, (_, scenarios) in checklist.SPACED_OUT_SYSTEMS.items():
            self.assertIn(system, checklist.SCENARIO_SYSTEMS)
            with_all = checklist.scenario_systems(checklist.PASSED_SCENARIOS | set(scenarios))
            self.assertIn(system, with_all)
            for name in scenarios:
                without = checklist.scenario_systems((checklist.PASSED_SCENARIOS | set(scenarios)) - {name})
                self.assertNotIn(system, without, f"{system} needs {name}")

    def test_entries_wait_for_every_system_they_need_and_emitters_need_radiation(self):
        for entry in self.inventory["entries"]:
            if entry.get("playtest") != "in": continue
            needs = [entry["playtest_system"], *entry.get("playtest_requires", [])]
            self.assertEqual(entry["status"] == "implemented", all(name in checklist.SYSTEMS for name in needs), entry["id"])
        spaced_out = {item["id"]: item for item in self.spaced_out["entries"]}
        emitters = [identity for identity, item in spaced_out.items() if item["kind"] == "prefab" and
                    "RadiationEmitter" in {name.rsplit(".", 1)[-1] for name in item["fact"]["components"]}]
        self.assertGreater(len(emitters), 20)
        for identity in emitters:
            entry = self.entries[identity]
            self.assertIn("radiation", [entry["playtest_system"], *entry.get("playtest_requires", [])], identity)
        self.assertIn("rocket-engines", self.entries["system/rockets"]["playtest_requires"])

    def test_scenario_report_names_what_still_needs_a_game_pass(self):
        report = checklist.scenario_requirements()
        for system, item in report.items():
            self.assertEqual(item["missing"] == [], system in checklist.SYSTEMS, system)
        self.assertEqual(report["rocket-cargo"]["missing"], [])


ROOT = checklist.ROOT


if __name__ == "__main__":
    unittest.main()
