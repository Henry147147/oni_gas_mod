import copy
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import runtime_inventory as runtime


class RuntimeInventoryTests(unittest.TestCase):
    def setUp(self):
        self.contract = json.loads((runtime.ROOT / "docs/interop-contract.json").read_text())
        self.capture = {
            "SchemaVersion": 1, "Kind": "runtime-registry-discovery", "OniBuild": "744825", "Platform": "WindowsPlayer",
            "AssemblySha256": self.contract["scope"]["assemblySha256"], "LoadedDlcIds": ["EXPANSION1_ID"],
            "PrefabRegistryInitialized": True, "RecipeRegistryInitialized": True,
            "CoverageComplete": False, "AuthoritativeColonySimulationReady": False, "Unresolved": ["fixture only"],
            "Prefabs": [{"Id": "FixtureMachine", "Components": ["Storage", "ElementConverter"], "MissingComponents": 0,
                         "Converters": [{"ComponentOrdinal": 1, "Inputs": [{"Tag": "Water", "MassKgPerSecond": 1.0}],
                                         "Outputs": [{"Element": "Oxygen", "MassKgPerSecond": 0.8}]}]}],
            "Recipes": [{"Id": "FixtureRecipe", "Fabricators": ["FixtureMachine"],
                         "Inputs": [{"Material": "Water", "PossibleMaterials": ["Water"], "PossibleAmounts": [10]}],
                         "Outputs": [{"Material": "Oxygen", "PossibleMaterials": [], "PossibleAmounts": [8]}]}]
        }

    def extract(self):
        return runtime.identities(self.capture, "a" * 64, self.contract)

    def test_discovery_keeps_identities_without_copying_property_values_or_claiming_coverage(self):
        value = self.extract()
        self.assertEqual(len(value["entries"]), 3)
        text = json.dumps(value)
        self.assertNotIn("MassKgPerSecond", text); self.assertNotIn("PossibleAmounts", text)
        self.assertFalse(value["coverage_complete"]); self.assertFalse(value["save_dlc_configuration_known"])
        self.assertIn("Water", text); self.assertIn("ElementConverter", text)

    def test_duplicate_registration_labels_preserved_and_duplicate_component_ordinals_rejected(self):
        original = copy.deepcopy(self.capture)
        for kind in ("Prefabs", "Recipes", "Converters"):
            self.capture = copy.deepcopy(original)
            array = self.capture["Prefabs"][0][kind] if kind == "Converters" else self.capture[kind]
            array.append(copy.deepcopy(array[0]))
            if kind == "Converters":
                with self.assertRaisesRegex(ValueError, "Duplicate runtime"): self.extract()
            else:
                value = self.extract()
                observed_kind = "prefab" if kind == "Prefabs" else "recipe"
                entries = [entry for entry in value["entries"] if entry["kind"] == observed_kind]
                self.assertEqual(len(entries), 2)
                self.assertNotEqual(entries[0]["id"], entries[1]["id"])

    def test_uninitialized_registry_cannot_be_interpreted_as_absence(self):
        self.capture["PrefabRegistryInitialized"] = False
        with self.assertRaises(ValueError): self.extract()
        self.capture["PrefabRegistryInitialized"] = True; self.capture["Prefabs"] = []
        with self.assertRaises(ValueError): self.extract()

    def test_wrong_binary_or_claimed_ownership_rejected(self):
        original = copy.deepcopy(self.capture)
        for changes in ({"AssemblySha256": "b" * 64}, {"CoverageComplete": True}, {"AuthoritativeColonySimulationReady": True},
                        {"Platform": "OtherPlatform"}, {"OniBuild": "future"}):
            self.capture = copy.deepcopy(original); self.capture.update(changes)
            with self.assertRaises(ValueError): self.extract()

    def test_extraction_is_deterministic_and_retains_missing_component_evidence(self):
        self.capture["Prefabs"][0]["MissingComponents"] = 1
        first = self.extract()
        self.assertEqual(first, self.extract())
        # Ordinals identify converters, so component order is significant.
        self.capture["Prefabs"][0]["Components"].reverse()
        self.assertNotEqual(first, self.extract())
        prefab = next(entry for entry in first["entries"] if entry["kind"] == "prefab")
        self.assertEqual(prefab["fact"]["missing_components"], 1)


if __name__ == "__main__":
    unittest.main()
