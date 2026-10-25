import math
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import floor_timing


class FloorTimingTests(unittest.TestCase):
    def compare(self, game, vanilla, truth):
        return floor_timing.compare({"bracket_s": game}, {"bracket_s": vanilla}, truth,
                                    "Synthetic controlled truth range")

    def test_early_gas_reference_decides_comparison_across_whole_range(self):
        self.assertEqual(self.compare([1.4, 1.6], [56.4, 56.6], [.4, .6])["status"], "pass")
        self.assertEqual(self.compare([56.4, 56.6], [1.4, 1.6], [.4, .6])["status"], "fail")

    def test_liquid_resolution_uncertainty_does_not_become_a_pass(self):
        result = self.compare([5.8, 6], [68.8, 69], [25.8, 48.6])
        self.assertEqual(result["status"], "unresolved")
        self.assertLess(result["game_minus_vanilla_absolute_error_range_s"][0], 0)
        self.assertGreater(result["game_minus_vanilla_absolute_error_range_s"][1], 0)

    def test_sampling_uncertainty_is_preserved_even_with_matching_midpoints(self):
        self.assertEqual(self.compare([1, 3], [2, 2], [2, 2])["status"], "unresolved")
        self.assertEqual(self.compare([3, 3], [2, 2], [2, 2])["status"], "fail")
        self.assertEqual(self.compare([1, 3], [1, 3], [2, 2])["status"], "unresolved")
        self.assertEqual(self.compare([2, 2], [2, 2], [2, 2])["status"], "pass")

    def test_censored_vanilla_is_unresolved_even_when_game_event_is_reached(self):
        result = floor_timing.compare({"bracket_s": [0, .2]}, {"censored": True, "observed_s": 120},
                                      [0, 1], "Synthetic truth")
        self.assertEqual(result["status"], "unresolved")

    def test_invalid_or_undocumented_ranges_are_refused(self):
        for invalid in ([math.nan, 1], [0, math.inf], [1, 0], [-1, 0], [0]):
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                self.compare([0, 1], [2, 3], invalid)
        with self.assertRaises(ValueError):
            floor_timing.compare({"bracket_s": [0, 1]}, {"bracket_s": [2, 3]}, [0, 1], " ")


if __name__ == "__main__":
    unittest.main()
