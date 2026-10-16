"""Native hook profiles reject drift instead of selecting approximate addresses."""
import copy
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import oni_hook_profiles as profiles


class NativeProfileTests(unittest.TestCase):
    def setUp(self):
        self.value = json.loads(profiles.PROFILE.read_text())

    def test_pinned_profiles_and_platform_specific_step(self):
        profiles.validate(self.value)
        win, linux = self.value["profiles"]
        self.assertEqual(len(win["hooks"]), 22)
        self.assertEqual(len(linux["hooks"]), 25)
        self.assertNotIn("gas-interaction", [h["role"] for h in win["hooks"]])
        self.assertEqual(next(h["signature"] for h in linux["hooks"] if h["role"] == "gas-interaction"), "bool(data*,events*,int)")
        self.assertNotIn("step", [h["role"] for h in win["hooks"]])
        self.assertNotEqual(win["assembly_sha256"], linux["assembly_sha256"])
        self.assertNotEqual(win["native_sha256"], linux["native_sha256"])

    def test_missing_duplicate_or_misidentified_hooks_rejected(self):
        for mutate in (lambda p: p["hooks"].pop(),
                       lambda p: p["hooks"].append(p["hooks"][0]),
                       lambda p: p["hooks"][0].update(role="other"),
                       lambda p: p["hooks"][0].update(signature="int()"),
                       lambda p: p["hooks"][0].update(symbol="unknown")):
            value = copy.deepcopy(self.value)
            mutate(value["profiles"][1])
            with self.assertRaises(ValueError): profiles.validate(value)

    def test_unknown_build_abi_or_fingerprint_rejected(self):
        for key in ("oni_build", "schema_version"):
            value = copy.deepcopy(self.value); value[key] += 1
            with self.assertRaises(ValueError): profiles.validate(value)
        for update in ({"rva": 0}, {"fingerprint_bytes": 8}, {"fingerprint_sha256": "0" * 63}):
            value = copy.deepcopy(self.value); value["profiles"][1]["hooks"][0].update(update)
            with self.assertRaises(ValueError): profiles.validate(value)

    def test_codegen_contains_hashes_without_game_code(self):
        text = profiles.header(self.value)
        for value in self.value["profiles"]:
            self.assertIn(value["native_sha256"], text)
            self.assertIn(value["assembly_sha256"], text)
        self.assertNotIn("0x48,", text)


if __name__ == "__main__": unittest.main()
