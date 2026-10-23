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


if __name__ == "__main__":
    unittest.main()
