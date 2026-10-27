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


if __name__ == "__main__":
    unittest.main()
