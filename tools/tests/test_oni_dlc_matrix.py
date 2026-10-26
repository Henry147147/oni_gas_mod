"""Compatibility orchestration must reject wrong content, hosts and no-op runs."""
import copy
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).parents[1]))
import oni_dlc as dlc
import oni_dlc_matrix as matrix


def result(mask, target="linux", loading=False, menu=False):
    ids = dlc.ids_for_mask(mask)
    return dict(status="passed-colony-owner-load" if loading else "passed-colony-owner", exit_code=0,
                platform="native Linux" if target == "linux" else None,
                target_platform="WindowsPlayer" if target != "linux" else "LinuxPlayer",
                host_platform="windows" if target == "windows" else "linux", actual_windows_os=target == "windows",
                real_unity_player=True, integration_probe=dict(Platform="LinuxPlayer" if target == "linux" else "WindowsPlayer",
                                                               **{flag: True for flag in matrix.PROBE_FLAGS}),
                colony_report=dict(RequestedDlcMask=mask, RuntimeDlcIds=ids, ActiveDlcIds=ids,
                                   DlcSelectionChecked=True, DlcSelectionMatched=True, Status="passed", GameStarted=True,
                                   WorldCount=9 if mask & 1 else 1, LoadContentCompatibilityChecked=loading,
                                   LoadContentCompatible=loading, RequiredLoadDlcIds=ids if loading else [], UnavailableLoadDlcIds=[]),
                colony_owner=dict(State=1, Faulted=False, Error="", OwnedCells=200, CompletedFrames=30, Publications=30,
                                  MaximumRelativeMassError=1e-15, MaximumRelativeEnergyError=1e-15,
                                  PipeSave=dict(Active=True, AuditClosed=True), RestoredOwner=dict(Reencoded=loading),
                                  OwnerMenuReload=dict(Requested=menu, Verified=menu, ShutdownObserved=menu, MainMenuObserved=menu)))


class CompatibilityEvidenceTests(unittest.TestCase):
    def test_plan_has_all_64_unique_os_content_rows_and_claims_no_execution(self):
        plan = matrix.plan()
        self.assertFalse(plan["passed"])
        rows = plan["cases"]
        self.assertEqual({(row["target"], row["mask"]) for row in rows},
                         {(target, mask) for target in ("linux", "windows") for mask in range(32)})
        self.assertEqual(len(rows), 64)
        self.assertTrue(all(row["status"] == "not-run" for row in rows))
        self.assertTrue(all(row["checks"] == ["startup", "world-generation", "save", "cold-load", "menu-load"] for row in rows))

    def test_each_actual_host_can_accept_complete_content_and_solver_evidence(self):
        for target in matrix.TARGETS:
            for mask in range(32):
                matrix.verify_case(result(mask, target, True, True), target, mask, True, True)

    def test_inactive_owner_wrong_content_and_incomplete_load_evidence_cannot_pass(self):
        data = result(31, loading=True, menu=True)
        changes = [("status", "passed-colony-owner-inactive"), ("exit_code", 2),
                   ("integration_probe.ManagedFrameIntercepted", False),
                   ("integration_probe.Platform", "WindowsPlayer"), ("colony_report.GameStarted", False),
                   ("colony_report.WorldCount", 1), ("colony_report.RuntimeDlcIds", ["EXPANSION1_ID"]),
                   ("colony_report.ActiveDlcIds", []), ("colony_report.LoadContentCompatible", False),
                   ("colony_report.RequiredLoadDlcIds", []), ("colony_report.UnavailableLoadDlcIds", ["DLC5_ID"]),
                   ("colony_owner.State", 0), ("colony_owner.Faulted", True), ("colony_owner.OwnedCells", 0),
                   ("colony_owner.CompletedFrames", 0), ("colony_owner.Publications", 0),
                   ("colony_owner.MaximumRelativeMassError", float("nan")),
                   ("colony_owner.MaximumRelativeEnergyError", 1e-8), ("colony_owner.MaximumRelativeEnergyError", -1),
                   ("colony_owner.PipeSave.Active", False), ("colony_owner.PipeSave.AuditClosed", False),
                   ("colony_owner.RestoredOwner.Reencoded", False), ("colony_owner.OwnerMenuReload.Verified", False),
                   ("colony_owner.OwnerMenuReload.ShutdownObserved", False), ("colony_owner.OwnerMenuReload.MainMenuObserved", False)]
        for field, value in changes:
            changed = copy.deepcopy(data)
            location = changed
            keys = field.split(".")
            for key in keys[:-1]:
                location = location[key]
            location[keys[-1]] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                matrix.verify_case(changed, "linux", 31, True, True)

    def test_proton_cannot_fill_the_windows_os_row_and_host_metadata_is_required(self):
        with self.assertRaises(ValueError):
            matrix.verify_case(result(0, "proton"), "windows", 0, False, False)
        for key, value in (("actual_windows_os", False), ("host_platform", "linux"), ("real_unity_player", False)):
            changed = result(0, "windows")
            changed[key] = value
            with self.assertRaises(ValueError):
                matrix.verify_case(changed, "windows", 0, False, False)


@unittest.skipUnless(os.name != "nt", "Mocked Linux execution uses the Linux host")
class CompatibilityRunnerTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.runner = self.root / "runner"
        (self.runner / "tools").mkdir(parents=True)
        for name in ("oni_headless.py", "oni_dlc.py", "oni_dlc_matrix.py", "oni_native_domain_evidence.py",
                     "oni_tile_hover.py", "oni_native_matrix.py", "oni_proton_headless.py"):
            (self.runner / "tools" / name).write_text("frozen runner")
        self.package = self.root / "package"
        self.package.mkdir()
        (self.package / "probe.dll").write_bytes(b"frozen package")
        self.output = self.root / "output"
        self.game_slot = self.root / "game-slot.sh"
        self.game_slot.write_text("frozen game slot")
        slot = mock.patch.object(matrix, "DEFAULT_GAME_SLOT", self.game_slot)
        slot.start()
        self.addCleanup(slot.stop)
        self.commands = []

    def game(self, command, log, timeout):
        self.commands.append(command)
        run = Path(command[command.index("--output") + 1])
        run.mkdir()
        mask = int(command[command.index("--dlc-mask") + 1])
        loading = "--load-save" in command
        target = "proton" if "--proton" in command else "linux"
        data = result(mask, target, loading, "--reload-in-process" in command)
        if not loading:
            source = run / "colony.sav"
            source.write_bytes(b"completed colony")
            data["colony_report"]["ColonySavePath"] = str(source)
            data.update(host_saved_file=str(source), host_saved_file_sha256=matrix.digest(source))
        (run / "result.json").write_text(json.dumps(data))
        return 0

    def test_partial_selection_runs_fresh_sources_and_both_reload_paths_without_claiming_the_full_matrix(self):
        with mock.patch.object(matrix, "invoke", side_effect=self.game):
            report = matrix.run_matrix(self.runner, self.package, self.output, target="linux", masks=[0, 20])
        self.assertTrue(report["passed"])
        self.assertFalse(report["full_target_coverage_passed"])
        self.assertFalse(report["byte_exact_active_continuation_verified"])
        self.assertFalse(report["actual_windows_os"])
        self.assertEqual(len(self.commands), 6)
        self.assertTrue(all(command[:2] == [str(self.game_slot), "high"] for command in self.commands))
        self.assertTrue(all("--dlc-mask" in command for command in self.commands))
        self.assertIn("--save-colony", self.commands[0])
        self.assertNotIn("--reload-in-process", self.commands[1])
        self.assertIn("--reload-in-process", self.commands[2])
        self.assertNotEqual(self.commands[1][self.commands[1].index("--load-save") + 1],
                            self.commands[4][self.commands[4].index("--load-save") + 1])

    def test_proton_full_coverage_still_cannot_become_windows_os_evidence(self):
        with mock.patch.object(matrix, "invoke", side_effect=self.game):
            report = matrix.run_matrix(self.runner, self.package, self.output, target="proton", proton=self.root / "proton")
        self.assertTrue(report["full_target_coverage_passed"])
        self.assertFalse(report["actual_windows_os"])
        self.assertEqual(len(self.commands), 96)

    def test_first_failure_retains_all_unrun_rows_and_does_not_start_a_load(self):
        def rejected(*args):
            self.game(*args)
            return 2
        with mock.patch.object(matrix, "invoke", side_effect=rejected), self.assertRaises(ValueError):
            matrix.run_matrix(self.runner, self.package, self.output, target="linux", masks=[1, 2, 3])
        self.assertEqual(len(self.commands), 1)
        report = json.loads((self.output / "matrix.json").read_text())
        self.assertFalse(report["passed"])
        self.assertEqual([row["status"] for row in report["cases"]], ["failed", "not-run", "not-run"])

    def test_no_result_and_runner_timeout_leave_failed_evidence(self):
        import subprocess
        for failure in ("missing", "timeout"):
            output = self.root / failure
            with mock.patch.object(matrix, "invoke", return_value=2,
                                   side_effect=subprocess.TimeoutExpired("game", 30) if failure == "timeout" else None), \
                    self.assertRaises((ValueError, subprocess.TimeoutExpired)):
                matrix.run_matrix(self.runner, self.package, output, target="linux", masks=[0])
            report = json.loads((output / "matrix.json").read_text())
            self.assertEqual(report["cases"][0]["status"], "failed")
            self.assertFalse(report["passed"])

    def test_mutations_to_the_frozen_runner_package_or_source_save_refuse(self):
        for change in ("runner", "package", "save"):
            output = self.root / ("evidence-" + change)
            def changed(command, *args):
                self.game(command, *args)
                if change == "runner":
                    (self.runner / "tools/oni_dlc.py").write_text("changed " + change)
                elif change == "package":
                    (self.package / "probe.dll").write_text("changed " + change)
                elif "--load-save" in command:
                    Path(command[command.index("--load-save") + 1]).write_bytes(b"changed")
                return 0
            with mock.patch.object(matrix, "invoke", side_effect=changed), self.assertRaises(ValueError):
                matrix.run_matrix(self.runner, self.package, output, target="linux", masks=[0])
            self.assertFalse(json.loads((output / "matrix.json").read_text())["passed"])

    def test_invalid_requests_and_existing_evidence_are_refused_before_launch(self):
        self.output.mkdir()
        sentinel = self.output / "retained"
        sentinel.write_text("first evidence")
        with mock.patch.object(matrix, "invoke") as invoke, self.assertRaises(FileExistsError):
            matrix.run_matrix(self.runner, self.package, self.output, target="linux", masks=[0])
        invoke.assert_not_called()
        self.assertEqual(sentinel.read_text(), "first evidence")
        for options in (dict(target="windows"), dict(target="linux", masks=[]), dict(target="linux", masks=[0, 0]),
                        dict(target="linux", masks=[32]), dict(target="linux", masks=[True]),
                        dict(target="proton"), dict(target="linux", frames=True), dict(target="linux", timeout=29)):
            with mock.patch.object(matrix, "invoke") as invoke, self.assertRaises(ValueError):
                matrix.run_matrix(self.runner, self.package, self.root / "invalid", **options)
            invoke.assert_not_called()

    def test_a_missing_or_changed_game_slot_refuses_and_queue_time_is_not_a_game_timeout(self):
        with mock.patch.object(matrix, "invoke") as invoke, self.assertRaises(ValueError):
            matrix.run_matrix(self.runner, self.package, self.output, target="linux", masks=[0], game_slot=self.root / "missing")
        invoke.assert_not_called()
        def changed(command, log, timeout):
            self.assertIsNone(timeout)
            self.game(command, log, timeout)
            self.game_slot.write_text("changed wrapper")
            return 0
        with mock.patch.object(matrix, "invoke", side_effect=changed), self.assertRaisesRegex(ValueError, "Frozen"):
            matrix.run_matrix(self.runner, self.package, self.output, target="linux", masks=[0])
        self.assertFalse(json.loads((self.output / "matrix.json").read_text())["passed"])


if __name__ == "__main__":
    unittest.main()
