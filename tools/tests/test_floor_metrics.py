import json
import sys
import tempfile
import struct
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
              "temperature_storage": "binary64",
              "cells": "per cell: constituent count, (element index, kg) each, temperature K",
              "elements": {str(k): v for k, v in elements.items()}, "placed": placed or {}, "ticks": ticks}
    path = folder / f"floor-{arena}.json"
    path.write_text(json.dumps(record))
    lines = []
    for tick, cell in frames:
        values = [str(tick)]
        for y in range(1, height - 1):
            for x in range(1, width - 1):
                *parts, temperature = cell(x, y)
                pairs = list(zip(parts[::2], parts[1::2]))
                values += [str(len(pairs))] + [str(v) for pair in pairs for v in pair] + [str(temperature)]
        lines.append(" ".join(values))
    path.with_suffix(".txt").write_text("\n".join(lines) + "\n")
    return path


class FloorMetricsTests(unittest.TestCase):
    def test_censor_bound_accounts_for_the_unconfirmed_persistence_tail(self):
        series = [(i * .2, i >= 5) for i in range(10)]
        result = floor_metrics.event(series, lambda value: value)
        self.assertTrue(result["censored"])
        self.assertEqual(result["observed_s"], 1.8)
        self.assertEqual(result["event_lower_bound_s"], .8)
        self.assertEqual(result["unconfirmed_true_tail_samples"], 5)
        result = floor_metrics.event([(t, False) for t, _ in series], lambda value: value)
        self.assertEqual(result["event_lower_bound_s"], 1.8)
        self.assertNotIn("event_lower_bound_s", floor_metrics.event(series[:5], lambda value: value))

    def test_atomic_sampling_reports_refusals_and_checks_booked_inventory(self):
        with tempfile.TemporaryDirectory() as folder:
            path = write(Path(folder), "f1", 3, 3, {"release": 0}, {1: OXYGEN},
                         [(0, lambda x, y: (1, 1.0, 300)), (1, lambda x, y: (1, 2.0, 300)), (2, lambda x, y: (1, 1.75, 300))])
            record = json.loads(path.read_text()); record["simulation"] = "colony owner"
            record["fluid_sampling"] = "single completed owner frame"
            record["owner_sample_audits"] = [dict(tick=t, frame=f, injected_kg=i, extracted_kg=0, reservoir_kg=0, expected_fluid_and_reservoir_kg=1+i+c,
                trace_sink_kg=0, space_sink_kg=0, projection_refused_cells=r)
                for t, f, i, c, r in ((0, 10, 0, 0, []), (1, 10, 1, 0, [5]), (2, 12, 1, -.25, [5, 6]))]
            path.write_text(json.dumps(record)); arena = floor_metrics.load(path)
            result = floor_metrics.sampling(arena)
            self.assertEqual((result["owner_frame_repeats"], result["owner_frame_skips"]), (1, 1))
            self.assertEqual((result["projection_refused_samples"], result["projection_refused_cell_samples"], result["projection_refused_unique_cells"]), (2, 3, 2))
            self.assertEqual(result["maximum_booked_inventory_residual_kg"], 0)
            for audit in arena.record["owner_sample_audits"]:
                audit["reservoir_kg"] = .25
            self.assertEqual(floor_metrics.sampling(arena)["maximum_booked_inventory_residual_kg"], .25)
            legacy = floor_metrics.sampling(arena, legacy_fluid_only_booking=True)
            self.assertEqual(legacy["maximum_booked_inventory_residual_kg"], 0)
            self.assertEqual(legacy["booked_inventory_scope"], "fluid")
            self.assertIn("explicit", legacy["legacy_booking_override"])
            arena.record["booked_inventory_scope"] = "fluid+reservoir"
            with self.assertRaisesRegex(ValueError, "explicit inventory scope"):
                floor_metrics.sampling(arena, legacy_fluid_only_booking=True)
            for audit in arena.record["owner_sample_audits"]:
                audit["expected_fluid_and_reservoir_kg"] += .25
            self.assertEqual(floor_metrics.sampling(arena)["maximum_booked_inventory_residual_kg"], 0)
            arena.record["owner_sample_audits"][1]["tick"] = 99
            with self.assertRaisesRegex(ValueError, "tick"):
                floor_metrics.sampling(arena)
    def test_retained_sampling_rejects_source_clock_gaps_and_distinguishes_accepted_heat(self):
        with tempfile.TemporaryDirectory() as folder:
            path = write(Path(folder), "f1", 3, 3, {"release": 0}, {1: OXYGEN},
                         [(0, lambda x, y: (1, 1.0, 300)), (1, lambda x, y: (1, 1.0, 300))])
            arena = floor_metrics.load(path)
            arena.record.update(simulation="colony owner", owner_sample_history=True, owner_tick_offset=-10)
            arena.record["owner_sample_audits"] = [dict(tick=t, frame=10+t, time_s=.2*t,
                injected_kg=0, extracted_kg=0, reservoir_kg=0, trace_sink_kg=0, space_sink_kg=0,
                projection_refused_cells=[], native_heat_j=0, accepted_external_heat_j=h)
                for t,h in [(0,25.25),(1,24.25)]]
            self.assertEqual(floor_metrics.sampling(arena)["accepted_external_heat_span_j"], 1)
            arena.record["owner_sample_audits"][1]["frame"] = 12
            with self.assertRaisesRegex(ValueError, "source clock"):
                floor_metrics.sampling(arena)
            arena.record["owner_sample_audits"][1]["frame"] = 11
            arena.record["owner_sample_audits"][1]["time_s"] = .4
            with self.assertRaisesRegex(ValueError, "fixed tick"):
                floor_metrics.sampling(arena)
    def test_legacy_short_float_text_and_exact_temperature_export_have_identical_heat(self):
        stored = struct.unpack("<f", struct.pack("<f", 293.15))[0]
        results = []
        with tempfile.TemporaryDirectory() as folder:
            for label, temperature in (("legacy", 293.15), ("exact", stored)):
                target = Path(folder) / label; target.mkdir()
                path = write(target, "f1", 19, 10, {"release": 0}, {1: OXYGEN},
                             [(t, lambda x, y: (1, 1.0, temperature)) for t in range(8)])
                record = json.loads(path.read_text())
                if label == "legacy":
                    record.pop("temperature_storage")
                else:
                    record["temperature_storage"] = "binary32"
                path.write_text(json.dumps(record))
                results.append(floor_metrics.metrics(path))
        self.assertEqual(results[0], results[1])
        self.assertEqual(results[0]["initial"]["elements"]["Oxygen"]["heat_j"], 136 * 1000 * stored)

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

    def test_f3_frozen_origin_starts_from_the_released_placements_not_a_prefill_row(self):
        full = 1000.0460568181818
        stored = lambda value: struct.unpack("<f", struct.pack("<f", value))[0]
        oxygen = {**OXYGEN, "id": "Oxygen"}
        # Tick 10 is exported before the fill reached the owner: displaced
        # background gas only. The frozen origin replaces it at t=0.
        def prefill(x, y):
            return (3, 1e-4, 294) if x >= 7 else (2, 0, 0)
        def levelled(x, y):
            return (1, stored(full) * 40 / 240, 293.15) if y <= 12 else (2, 0, 0)
        with tempfile.TemporaryDirectory() as folder:
            path = write(Path(folder), "f3", 22, 14, {"release": 10}, {1: WATER, 2: VACUUM, 3: oxygen},
                         [(10, prefill)] + [(t, levelled) for t in range(11, 30)],
                         {"Water_full_cell_kg": full, "release_staging": 1})
            record = json.loads(path.read_text())
            record["initial_time_origin"] = "before first completed fluid interval; separate exact admission proofs required"
            path.write_text(json.dumps(record))
            out = floor_metrics.metrics(path)
            arena = floor_metrics.load(path)
            series = arena.series(10)
            # Without the same-frame staging, the exported release row stays t=0.
            record["placed"].pop("release_staging"); path.write_text(json.dumps(record))
            legacy = floor_metrics.load(path).series(10)
        self.assertEqual(legacy[0][1][(7, 1)][0][0][1], 1e-4)
        self.assertEqual(series[0][0], 0.0)
        self.assertEqual(len(series), 20)
        self.assertEqual(out["initial_water_kg"], 40 * stored(full))
        self.assertEqual(out["initial"]["elements"]["Water"]["kg"], 40 * stored(full))
        self.assertNotIn("Oxygen", out["initial"]["elements"])
        # The frozen column cannot satisfy the levelling predicate; the
        # first exported (levelled) row is the event, bracketed from t=0.
        self.assertEqual(out["levelled"]["bracket_s"], [0.0, .2])
        self.assertAlmostEqual(out["final_mean_depth_m"], 2.0 * stored(full) / full)

    def test_f2_final_target_includes_booked_heat_and_released_kinetic_energy(self):
        # The catalogue interprets the game's fixed-cell specific heat as Cv.
        capacity = 130 * OXYGEN["specific_heat_j_per_kg_k"]
        potential = 130 * 9.81 * 5

        def frame(tick):
            return lambda x, y: (2, 0, 0) if x == 9 and y not in (4, 5) else (1, 1, 300 if tick < 2 else 340)

        with tempfile.TemporaryDirectory() as folder:
            path = write(Path(folder), "f2", 19, 10, {"locked": 0, "commanded": 0, "opened": 0, "release": 0},
                         {1: OXYGEN, 2: WALL}, [(tick, frame(tick)) for tick in range(8)])
            record = json.loads(path.read_text())
            record.update(simulation="colony owner", owner_sample_history=True, owner_tick_offset=-10)
            audits = []
            for tick in range(8):
                heat = 7 + (0 if tick < 2 else 30 * capacity)
                audit = dict(tick=tick, session=147, frame=10+tick, time_s=.2*tick,
                    injected_kg=0, extracted_kg=0, reservoir_kg=0, expected_fluid_and_reservoir_kg=130,
                    trace_sink_kg=0, space_sink_kg=0, projection_refused_cells=[], accepted_external_heat_j=heat)
                audit["energy"] = dict(session=147, frame=10+tick, time_s=.2*tick,
                    internal_energy_j=capacity*(300 if tick < 2 else 340), kinetic_energy_j=capacity*(10 if tick < 2 else 0),
                    potential_energy_j=potential, thermal_energy_j=capacity*(300 if tick < 2 else 340),
                    formation_energy_j=0, heat_capacity_j_per_k=capacity, fluid_mass_kg=130,
                    accepted_external_heat_j=heat, trace_sink_energy_j=0)
                audits.append(audit)
            record["owner_sample_audits"] = audits; path.write_text(json.dumps(record))
            out = floor_metrics.metrics(path)
            self.assertEqual(out["equalised"]["bracket_s"], [.2, .4])
            truth = out["analytic_final_pressure_range_pa"]
            expected = floor_metrics.GAS_CONSTANT * 340 / OXYGEN["molar_mass_kg_per_mol"]
            self.assertLess(truth[0], expected); self.assertGreater(truth[1], expected)
            self.assertAlmostEqual(sum(truth) / 2, expected)
            self.assertEqual(out["pressure_truth"]["release_kinetic_energy_j"], 10 * capacity)
            self.assertAlmostEqual(out["pressure_truth"]["accepted_heat_after_release_j"], 30 * capacity)
            # A fixed held reservoir is outside the sealed fluid energy sum.
            for audit in record["owner_sample_audits"]:
                audit["reservoir_kg"] = .75
                audit["expected_fluid_and_reservoir_kg"] += .75
            path.write_text(json.dumps(record))
            held = floor_metrics.metrics(path)
            self.assertEqual(held["analytic_final_pressure_range_pa"], truth)
            self.assertEqual(held["pressure_truth"]["isolated_reservoir_kg"], .75)
            self.assertEqual(held["pressure_truth"]["maximum_fluid_energy_residual_j"], 0)
            record["owner_sample_audits"][3]["energy"]["internal_energy_j"] += 1
            path.write_text(json.dumps(record))
            with self.assertRaisesRegex(ValueError, "fluid energy does not close"):
                floor_metrics.metrics(path)
            record["owner_sample_audits"][3]["energy"]["internal_energy_j"] -= 1
            record["owner_sample_audits"][3]["reservoir_kg"] += .01
            path.write_text(json.dumps(record))
            with self.assertRaisesRegex(ValueError, "mass exchange"):
                floor_metrics.metrics(path)
            record["owner_sample_audits"][3]["reservoir_kg"] -= .01
            record["owner_sample_audits"][3]["energy"]["fluid_mass_kg"] += .01
            path.write_text(json.dumps(record))
            with self.assertRaisesRegex(ValueError, "constant sealed fluid mass"):
                floor_metrics.metrics(path)
            record["owner_sample_audits"][3]["energy"]["fluid_mass_kg"] -= .01
            record["owner_sample_audits"][3]["energy"]["frame"] += 1
            path.write_text(json.dumps(record))
            with self.assertRaisesRegex(ValueError, "session/frame/time"):
                floor_metrics.metrics(path)

    def test_uncertain_final_pressure_is_not_reported_as_a_proven_event(self):
        temperature = 300
        kg = 100 * .032 / (floor_metrics.GAS_CONSTANT * temperature)
        cells = {(1, 1): ([(OXYGEN, kg)], temperature), (2, 1): ([(OXYGEN, kg)], temperature)}
        event = floor_metrics.pressure_event_range([(.2*t, cells) for t in range(8)], [(1, 1)], [(2, 1)], [90, 110])
        self.assertTrue(event["unresolved"])
        self.assertEqual(event["possible"]["bracket_s"], [0, 0])
        self.assertTrue(event["guaranteed"]["censored"])

    def test_pressure_censor_bound_covers_each_targets_unconfirmed_tail(self):
        def cells(value):
            kg = value * .032 / (floor_metrics.GAS_CONSTANT * 300)
            return {(1, 1): ([(OXYGEN, kg)], 300), (2, 1): ([(OXYGEN, kg)], 300)}
        # Five final samples fit target100 but never all targets90..110.
        # The conservative bound must precede that tail, not equal t=1.4.
        series = [(.2 * t, cells(10 if t < 3 else 100)) for t in range(8)]
        result = floor_metrics.pressure_event_range(series, [(1, 1)], [(2, 1)], [90, 110])
        self.assertTrue(result["censored"])
        self.assertEqual(result["event_lower_bound_s"], .4)

    def test_f4_caloric_truth_uses_frozen_placements_before_first_exported_heat(self):
        granite = {"id": "Granite", "phase": "solid", "molar_mass_kg_per_mol": .05,
                   "specific_heat_j_per_kg_k": 800.0}
        placed = {"hot_cell_kg": 2.123456789, "cold_cell_kg": 3.987654321, "granite_tile_kg": 200}
        stored = lambda value: struct.unpack("<f", struct.pack("<f", value))[0]
        hot_mass, cold_mass = stored(placed["hot_cell_kg"]), stored(placed["cold_cell_kg"])

        def cell(x, y):
            # The first sampled row is already at a common 318 K. It cannot
            # establish the prescribed 353.15/283.15/318.15 K input heat.
            return (2, 200, 318) if x == 3 else (1, hot_mass if x < 3 else cold_mass, 318)

        with tempfile.TemporaryDirectory() as folder:
            path = write(Path(folder), "f4", 7, 4, {"release": 0}, {1: OXYGEN, 2: granite},
                         [(t, cell) for t in range(8)], placed)
            out = floor_metrics.metrics(path)
        gas_heat = 4 * 1000 * (hot_mass * stored(353.15) + cold_mass * stored(283.15))
        heat = gas_heat + 400 * 800 * stored(318.15)
        capacity = 4 * (hot_mass + cold_mass) * 1000 + 400 * 800
        self.assertEqual(out["initial_source"], "frozen binary32 placements")
        self.assertAlmostEqual(out["initial_heat_j"], heat)
        self.assertAlmostEqual(out["common_temperature_k"], heat / capacity)
        self.assertAlmostEqual(out["temperature_error_k"], abs(318 - heat / capacity))
        self.assertAlmostEqual(out["release_sample_heat_j"], capacity * 318)
        self.assertAlmostEqual(out["initial"]["elements"]["Oxygen"]["heat_j"], gas_heat)
        self.assertNotEqual(out["initial_heat_j"], out["release_sample_heat_j"])

    def test_f4_energy_rejects_an_unpaired_native_solid_ticket(self):
        with tempfile.TemporaryDirectory() as folder:
            path = write(Path(folder), "f4", 3, 3, {"release": 0}, {1: OXYGEN}, [(0, lambda x, y: (1, 1, 300))])
            arena = floor_metrics.load(path)
            arena.record.update(simulation="colony owner", solid_sampling="retained native heat-input ticket paired with the same owner frame")
            energy = dict(session=7, frame=11, time_s=2.2, internal_energy_j=300000, kinetic_energy_j=0,
                          potential_energy_j=0, thermal_energy_j=300000, formation_energy_j=0,
                          heat_capacity_j_per_k=1000, fluid_mass_kg=1, accepted_external_heat_j=0, trace_sink_energy_j=0)
            audit = dict(session=7, frame=11, time_s=2.2, accepted_external_heat_j=0,
                         native_ticket=509, native_sample_ticket=509, energy=energy)
            arena.record["owner_sample_audits"] = [audit]
            self.assertEqual(floor_metrics.sample_energies(arena), [energy])
            audit["native_sample_ticket"] = 510
            with self.assertRaisesRegex(ValueError, "native solids"):
                floor_metrics.sample_energies(arena)
            audit["native_sample_ticket"] = 509
            arena.record.pop("solid_sampling")
            with self.assertRaisesRegex(ValueError, "native solids"):
                floor_metrics.sample_energies(arena)

    def test_f5_counts_each_liquid_of_a_mixed_cell(self):
        water_full, oil_full = 1000.0, 870.0
        oil = {"id": "CrudeOil", "phase": "liquid", "molar_mass_kg_per_mol": .5, "specific_heat_j_per_kg_k": 1690.0}

        def cell(x, y):
            # Water in rows 1-3, a mixed row 4 (half of each), oil in rows 5-6.
            if y <= 3:
                return (1, water_full, 293.15)
            if y == 4:
                return (1, water_full / 2, 3, oil_full / 2, 293.15)
            if y <= 6:
                return (3, oil_full, 293.15)
            return (2, 0, 0)

        with tempfile.TemporaryDirectory() as folder:
            path = write(Path(folder), "f5", 10, 14, {"release": 0}, {1: WATER, 2: VACUUM, 3: oil},
                         [(t, cell) for t in range(0, 12)], {"Water_full_cell_kg": water_full, "CrudeOil_full_cell_kg": oil_full})
            out = floor_metrics.metrics(path)
        self.assertAlmostEqual(out["final_mean_depth_m"]["Water"], 3.5)
        self.assertAlmostEqual(out["final_mean_depth_m"]["CrudeOil"], 2.5)
        self.assertEqual(out["final_layer_order_bottom_to_top"], ["Water", "CrudeOil"])
        self.assertGreater(out["interface_overlap_m"], 0)


if __name__ == "__main__":
    unittest.main()
