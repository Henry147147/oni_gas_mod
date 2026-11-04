"""The full-colony benchmark tool's parsing and metadata, from synthetic run folders; no game runs."""
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).parents[1]))
import full_colony_benchmark as tool

WINDOW = ("[Mixed Atmospheres] perf frames 151-300 session 1 partial 0: owner_ms mean 35.3 p95 39.9 max 60.3 stages_ms edits 0.2 heat 4.0 "
          "transfers 4.6 flow 26.5 space 0.0 ledger_projection 1.9 interval_ms mean 78.0 p95 184.9 max 293.4 submit_to_publication_ms mean 80.3 "
          "p95 167.5 max 261.5 unmatched_publications 0 publication_samples 148 dropped_publication_samples 0 dropped_submission_samples 0 "
          "owner_seconds 30.000000 game_seconds 29.999542 wall_seconds 11.550835 owner_per_real 2.597 game_per_real 2.597 pause_observed 0 "
          "capacity_estimate 5.88 scale 3.00 speed_button 3 fps 37 slowest_frame_ms 255 held 205 samples 148 owner_frames 150")
RUN = ("[Mixed Atmospheres] perf run session 1 frames 0-422 reason session_end owner_seconds 84.400001 game_seconds 84.200161 "
       "wall_seconds 36.139327 owner_per_real 2.335 game_per_real 2.330 pause_observed 1 unity_frames 1280 slowest_frame_ms 1925")


class FullColonyBenchmarkTests(unittest.TestCase):
    def test_windows_parse_owner_and_interval_times_and_speed(self):
        rows = tool.windows("noise\n" + WINDOW + "\n" + RUN + "\n")
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["frames"], [151, 300])
        self.assertEqual(rows[0]["owner_ms"], {"mean": 35.3, "p95": 39.9, "max": 60.3})
        self.assertEqual(rows[0]["interval_ms"]["mean"], 78.0)
        self.assertAlmostEqual(rows[0]["game_per_wall_second"], 2.597)

    def test_summary_and_publish_write_the_metadata_beside_the_save(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); run = root / "run"; validation = run / tool.VALIDATION
            validation.mkdir(parents=True)
            (run / "Player.log").write_text(WINDOW + "\n" + RUN + "\n")
            (run / "result.json").write_text(json.dumps({"source_revision": "r" * 40, "source_dirty": False}))
            (validation / "colony-owner.json").write_text(json.dumps({"State": 1, "Faulted": False, "CompletedFrames": 9, "OwnedCells": 5}))
            census = {"GameCycle": 4, "GameSeconds": 1800.0, "Frame": 9, "Duplicants": 18, "GasCells": 7, "LiquidCells": 3, "Buildings": 2,
                      "GasPipeCells": 1, "LiquidPipeCells": 1, "WireCells": 1, "OwnedPipeCellsWithContents": 1, "OwnedCells": 5, "GasKg": 1.5, "LiquidKg": 2.5}
            (validation / "colony-census.json").write_text(json.dumps(census))
            save = validation / "colony-owner.sav"; save.write_bytes(b"save")
            (validation / "colony-validation.json").write_text(json.dumps({"ColonySavePath": str(save)}))
            (run / "auto_save").mkdir(); (run / "auto_save" / "colony Cycle 2.sav").write_bytes(b"auto")
            data = tool.summary(run)
            self.assertEqual(data["natural_autosaves"], ["colony Cycle 2.sav"])
            self.assertEqual(data["revision"], "r" * 40)
            self.assertEqual(data["run_total"]["game_per_wall_second"], 2.33)
            self.assertEqual(data["census"]["Duplicants"], 18)
            self.assertEqual(data["windows"][0]["owner_ms"]["p95"], 39.9)
            self.assertEqual(len(tool.sha256(save)), 64)


if __name__ == "__main__":
    unittest.main()
