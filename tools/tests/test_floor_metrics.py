import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import floor_metrics  # noqa: E402

OXYGEN = {"id": "Oxygen", "phase": "gas", "molar_mass_kg_per_mol": .032, "specific_heat_j_per_kg_k": 1000.0}
VACUUM = {"id": "Vacuum", "phase": "vacuum", "molar_mass_kg_per_mol": 0, "specific_heat_j_per_kg_k": 0}
WALL = {"id": "Unobtanium", "phase": "solid", "molar_mass_kg_per_mol": .05, "specific_heat_j_per_kg_k": 0}
WATER = {"id": "Water", "phase": "liquid", "molar_mass_kg_per_mol": .018, "specific_heat_j_per_kg_k": 4179.0}


def write(folder: Path, arena: str, width: int, height: int, ticks: dict, elements: dict, frames, placed=None) -> Path:
    record = {"arena": arena, "width": width, "height": height, "tick_seconds": .2, "simulation": "synthetic",
              "elements": {str(k): v for k, v in elements.items()}, "placed": placed or {}, "ticks": ticks}
    path = folder / f"floor-{arena}.json"
    path.write_text(json.dumps(record))
    lines = []
    for tick, cell in frames:
        values = [str(tick)]
        for y in range(1, height - 1):
            for x in range(1, width - 1):
                values += [str(v) for v in cell(x, y)]
        lines.append(" ".join(values))
    path.with_suffix(".txt").write_text("\n".join(lines) + "\n")
    return path


class FloorMetricsTests(unittest.TestCase):
    def test_f1_equalises_after_its_persistent_bracket_and_splits_by_volume(self):
        left_kg = 101325 * .032 / (8.314462618 * 293.15)
        uniform = left_kg * 64 / 136

        def frame(tick):
            # Divider gone at tick 10; gas uniform from tick 13 on; a blip back at 12 so 11 does not count.
            def cell(x, y):
                if tick <= 10:
                    return (1, left_kg, 293.15) if x < 9 else (2, 0, 0) if x > 9 else (3, 20000, 293.15)
                if tick >= 13 or tick == 11:
                    return (1, uniform, 293.15)
                return (1, left_kg, 293.15) if x < 9 else (1, 1e-6, 293.15)
            return cell

        with tempfile.TemporaryDirectory() as folder:
            path = write(Path(folder), "f1", 19, 10, {"release": 10}, {1: OXYGEN, 2: VACUUM, 3: WALL},
                         [(t, frame(t)) for t in range(8, 40)])
            out = floor_metrics.metrics(path)
        self.assertEqual(out["equalised"]["bracket_s"], [.4, .6000000000000001])
        self.assertAlmostEqual(out["final_mass_fraction"]["left"], 64 / 136, places=9)
        self.assertAlmostEqual(out["final_mass_fraction"]["divider"], 8 / 136, places=9)
        self.assertAlmostEqual(out["analytic_final_pressure_pa"], 101325 * 64 / 136, delta=1e-6)

    def test_f3_censors_an_unlevelled_basin_and_measures_depth(self):
        full = 1000.05
        with tempfile.TemporaryDirectory() as folder:
            path = write(Path(folder), "f3", 22, 14, {"release": 0}, {1: WATER, 2: VACUUM},
                         [(t, lambda x, y: (1, full, 293.15) if x <= 5 and y <= 8 else (2, 0, 0)) for t in range(0, 20)],
                         {"Water_full_cell_kg": full})
            out = floor_metrics.metrics(path)
        self.assertTrue(out["levelled"]["censored"])
        self.assertAlmostEqual(out["final_mean_depth_m"], 2.0)


if __name__ == "__main__":
    unittest.main()
