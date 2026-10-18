"""Guards for the manual visual-scene request, including the explicit long run."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

SPEC = importlib.util.spec_from_file_location("configure_visual_demo", Path(__file__).parents[1] / "configure_visual_demo.py")
demo = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(demo)


class VisualDemoConfigurationTests(unittest.TestCase):
    def test_short_scene_keeps_its_original_request(self):
        self.assertEqual(demo.configuration("air"), {
            "Mode": "native-domain", "AutoStartNewColony": False, "QuitWhenComplete": False, "SaveOnComplete": False,
            "CompletedFrames": 120, "TimeoutSeconds": 7200, "Seed": "147147", "ColonyName": "Mixed Atmospheres visual demo"})
        self.assertEqual(demo.configuration("water", frames=10000)["CompletedFrames"], 10000)

    def test_long_run_requests_ten_cycles_a_48_hour_limit_and_telemetry(self):
        value = demo.configuration("mixed", long_run=True)
        self.assertEqual((value["Mode"], value["CompletedFrames"], value["TimeoutSeconds"], value["LongRun"], value["TelemetryIntervalFrames"]),
                         ("native-mixed-domain", 30000, 172800, True, 300))
        self.assertEqual(demo.configuration("air", long_run=True, frames=100000)["CompletedFrames"], 100000)
        for long_run, frames in ((False, 0), (False, 10001), (True, 100001), (True, 0), (True, True)):
            with self.subTest(long_run=long_run, frames=frames), self.assertRaises(ValueError):
                demo.configuration("air", long_run=long_run, frames=frames)

    def _main(self, package: Path, *extra: str) -> tuple[int, str, str]:
        output, error = io.StringIO(), io.StringIO()
        with mock.patch.object(sys, "argv", ["configure_visual_demo", "--package", str(package), *extra]), \
                contextlib.redirect_stdout(output), contextlib.redirect_stderr(error):
            try:
                demo.main()
                code = 0
            except SystemExit as exit:
                code = exit.code
        return code, output.getvalue(), error.getvalue()

    def test_main_writes_one_fresh_request_and_rejects_widened_frames_without_opt_in(self):
        with tempfile.TemporaryDirectory() as directory:
            package = Path(directory)
            (package / "MixedAtmospheres.Oni.dll").write_bytes(b"unit")
            (package / "native-backend-observation.json").write_text(json.dumps({"SchemaVersion": 1, "Enabled": True}))
            code, _, error = self._main(package, "--frames", "30000")
            self.assertEqual(code, 2)
            self.assertIn("requires --long-run", error)
            self.assertFalse((package / "colony-validation.json").exists())
            code, output, _ = self._main(package, "--scene", "water", "--long-run")
            self.assertEqual(code, 0)
            self.assertIn("pauses after 30000 completed frames", output)
            self.assertIn("development-telemetry.jsonl", output)
            written = json.loads((package / "colony-validation.json").read_text())
            self.assertEqual((written["Mode"], written["LongRun"], written["TimeoutSeconds"]), ("native-liquid-domain", True, 172800))
            code, _, error = self._main(package)
            self.assertEqual(code, 2)
            self.assertIn("already exists", error)


if __name__ == "__main__":
    unittest.main()
