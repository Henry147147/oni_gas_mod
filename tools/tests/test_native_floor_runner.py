"""Keep the vanilla floor probe separate from mod-owner measurements."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location("floor_native_runner", Path(__file__).resolve().parents[1] / "NativeShadowProbe/run.py")
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class NativeFloorRunnerTests(unittest.TestCase):
    def test_vanilla_floor_refuses_missing_input_or_observer_before_launch(self):
        for extra in ([], ["--observer", "/private/owner.so", "--game", "/private/game"]):
            with mock.patch.object(sys, "argv", ["probe", "--native", "/private/native.so", "--contract", "/private/tables",
                    "--output", "/private/output", "--mode", "floor-f4", *extra]), \
                    mock.patch.object(runner.subprocess, "run") as launch, contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                runner.main()
            launch.assert_not_called()

    def test_vanilla_floor_pins_private_request_and_uses_its_explicit_process_bound(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ("native.so", "probe.dll", "contract.json", "elements.bin", "diseases.bin", "request.bin"):
                (root / name).touch()
            seen = []

            def launch(command, **options):
                seen.append((command, options))
                Path(command[-1]).write_text(json.dumps({"passed": True}))
                return mock.Mock(returncode=0)

            with mock.patch.object(sys, "argv", ["probe", "--native", str(root / "native.so"), "--contract", str(root),
                    "--probe", str(root / "probe.dll"), "--output", str(root / "output"), "--mode", "floor-f4",
                    "--floor-request", str(root / "request.bin")]), mock.patch.object(runner.subprocess, "run", side_effect=launch):
                self.assertEqual(runner.main(), 0)
            command, options = seen[0]
            self.assertEqual(options["timeout"], 240)
            self.assertEqual(options["env"]["MA_NATIVE_FLOOR_REQUEST"], str(root / "request.bin"))
            self.assertEqual(command[-2], "floor-f4")
            self.assertFalse(json.loads((root / "output/suite.json").read_text())["authoritativeColonySimulationReady"])
