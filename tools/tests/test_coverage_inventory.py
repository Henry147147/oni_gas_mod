"""Regression checks for inventory omissions and false acceptance claims."""
import copy
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import coverage_inventory as coverage


class CoverageTests(unittest.TestCase):
    def setUp(self):
        self.inventory = json.loads(coverage.INVENTORY.read_text())
        self.policy = json.loads(coverage.POLICY.read_text())

    def proof(self, entry=None, config="linux-x64/dlc-00", **changes):
        value = {"configuration": config, "kind": "in-game", "run_id": "test-fixture-only",
                 "result": "passed", "artifact_sha256": "a" * 64,
                 "assembly_sha256": self.inventory["assembly_hashes"][config.split("/")[0]]}
        if entry:
            value.update(entry=entry["id"], acceptance=entry["acceptance"])
        value.update(changes)
        return value

    def test_checked_inventory_valid_but_never_release_ready(self):
        coverage.validate(self.inventory)
        report = coverage.blockers(self.inventory, self.policy, release=True)
        self.assertFalse(report["gate_passed"])
        self.assertEqual(report["missing_runtime_registry_configurations"], 64)
        self.assertEqual(report["missing_in_game_acceptances"], len(self.inventory["entries"]) * 64)
        self.assertGreater(report["missing_release_acceptances"], 0)

    def test_all_pack_masks_are_independent_per_platform(self):
        configs = coverage.configurations()
        for platform in coverage.PLATFORMS:
            self.assertEqual({item["dlc_mask"] for item in configs if item["platform"] == platform}, set(range(32)))
        self.inventory["configurations"][0]["packs"] = ["Aquatic"]
        with self.assertRaises(ValueError): coverage.validate(self.inventory)

    def test_missing_discovered_interface_rejected(self):
        self.inventory["entries"] = [entry for entry in self.inventory["entries"] if entry["id"] != "message/MassEmission"]
        with self.assertRaisesRegex(ValueError, "Missing discovered"): coverage.validate(self.inventory)

    def test_native_hooks_cannot_disappear_from_coverage(self):
        self.inventory["entries"] = [entry for entry in self.inventory["entries"] if entry["id"] != "native-hook/pressure"]
        with self.assertRaisesRegex(ValueError, "Missing discovered"): coverage.validate(self.inventory)

    def test_forwarding_observer_cannot_satisfy_native_takeover(self):
        entry = next(entry for entry in self.inventory["entries"] if entry["kind"] == "native-hook")
        self.inventory["evidence"] = [self.proof(entry)]
        with self.assertRaisesRegex(ValueError, "forwarding observation is insufficient"): coverage.validate(self.inventory)

    def test_material_deletion_or_identity_edit_rejected(self):
        self.inventory["entries"] = [entry for entry in self.inventory["entries"] if entry["id"] != "material/Oxygen"]
        with self.assertRaisesRegex(ValueError, "Material identity list changed"): coverage.validate(self.inventory)

    def test_duplicates_missing_owner_or_undefined_acceptance_rejected(self):
        for mutation in (lambda inv: inv["entries"].append(inv["entries"][0]),
                         lambda inv: inv["entries"][0].update(owner=""),
                         lambda inv: inv["entries"][0].update(acceptance="no-such-test")):
            inventory = copy.deepcopy(self.inventory); mutation(inventory)
            with self.assertRaises(ValueError): coverage.validate(inventory)

    def test_stale_contract_or_wrong_assembly_rejected(self):
        self.inventory["source_hashes"]["interop_contract"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "Stale inventory"): coverage.validate(self.inventory)

    def test_implemented_entry_needs_real_local_sources(self):
        entry = self.inventory["entries"][0]; entry["status"] = "implemented"
        for paths in ([], ["/etc/passwd"], ["managed/nonexistent.cs"]):
            entry["implementation"] = paths
            with self.assertRaises(ValueError): coverage.validate(self.inventory)

    def test_standalone_or_wrong_binary_evidence_cannot_pass(self):
        entry = self.inventory["entries"][0]
        for change in ({"kind": "standalone"}, {"assembly_sha256": "b" * 64}, {"artifact_sha256": "z" * 64},
                       {"run_id": ""}, {"acceptance": "not-this-test"}):
            self.inventory["evidence"] = [self.proof(entry, **change)]
            with self.assertRaises(ValueError): coverage.validate(self.inventory)

    def test_evidence_duplicates_and_untriaged_runtime_identities_rejected(self):
        proof = self.proof(self.inventory["entries"][0]); self.inventory["evidence"] = [proof, proof]
        with self.assertRaises(ValueError): coverage.validate(self.inventory)
        self.inventory["evidence"] = []
        for extra in ({"entries": ["unseen-prefab"], "unresolved": []}, {"entries": [], "unresolved": ["reflection"]}):
            self.inventory["registry_captures"] = [self.proof(**extra)]
            with self.assertRaises(ValueError): coverage.validate(self.inventory)

    def test_declared_native_interfaces_cannot_be_waived_as_absent(self):
        entry = next(e for e in self.inventory["entries"] if e["kind"] == "native-message")
        self.inventory["evidence"] = [self.proof(entry, result="absent")]
        with self.assertRaisesRegex(ValueError, "cannot be waived"): coverage.validate(self.inventory)

    def test_absence_requires_runtime_capture_and_cannot_hide_a_present_material(self):
        entry = next(e for e in self.inventory["entries"] if e["id"] == "material/Oxygen")
        small = copy.deepcopy(self.inventory)
        small["entries"] = [entry]; small["configurations"] = [small["configurations"][0]]
        config = small["configurations"][0]["id"]
        small["evidence"] = [self.proof(entry, config, result="absent")]
        self.assertFalse(coverage.blockers(small, self.policy)["gate_passed"])
        small["registry_captures"] = [self.proof(config=config, entries=[entry["id"]], unresolved=[])]
        self.assertFalse(coverage.blockers(small, self.policy)["gate_passed"])
        small["registry_captures"][0]["entries"] = []
        self.assertTrue(coverage.blockers(small, self.policy)["gate_passed"])

    def test_passing_ownership_evidence_does_not_pass_release(self):
        small = copy.deepcopy(self.inventory)
        entry = small["entries"][0]; entry["status"] = "implemented"
        small["entries"] = [entry]; small["configurations"] = [small["configurations"][0]]
        config = small["configurations"][0]["id"]
        small["evidence"] = [self.proof(entry, config)]
        small["registry_captures"] = [self.proof(config=config, entries=[entry["id"]], unresolved=[])]
        self.assertTrue(coverage.blockers(small, self.policy)["gate_passed"])
        self.assertFalse(coverage.blockers(small, self.policy, release=True)["gate_passed"])


if __name__ == "__main__":
    unittest.main()
