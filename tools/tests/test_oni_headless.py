"""Acceptance guards for private Linux playtest evidence and package isolation."""
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

SPEC = importlib.util.spec_from_file_location("oni_headless", Path(__file__).parents[1] / "oni_headless.py")
runner = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(runner)


class EvidenceTests(unittest.TestCase):
    def setUp(self):
        self.probe = {flag: True for flag in runner.PROBE_FLAGS}
        self.probe["Platform"] = "LinuxPlayer"
        self.log = ("[17:30:01.000] [1] [INFO] -- MAIN MENU -- \n"
                    "[Mixed Atmospheres] Native contract captured without native initialization: /x/game-contract-local\n")

    def test_successful_startup_is_not_successful_colony_validation(self):
        self.assertEqual(runner.classify("menu", self.log, self.probe, None, False)[1], 0)
        self.assertNotEqual(runner.classify("presentation", self.log, self.probe, None, False)[1], 0)

    def test_linux_and_all_scoped_checks_are_required(self):
        self.probe["Platform"] = "WindowsPlayer"
        self.assertNotEqual(runner.classify("menu", self.log, self.probe, None, False)[1], 0)
        self.probe["Platform"] = "LinuxPlayer"
        self.probe[runner.PROBE_FLAGS[-1]] = "true"
        self.assertNotEqual(runner.classify("menu", self.log, self.probe, None, False)[1], 0)

    def test_successful_probe_before_main_menu_does_not_pass(self):
        self.assertNotEqual(runner.classify("menu", "", self.probe, None, False)[1], 0)
        status, code = runner.classify("menu", "Steam not initialized in time", self.probe, None, False)
        self.assertEqual((status, code), ("blocked-steam-authentication", 3))

    def test_crashes_timeout_and_late_failures_override_completed_report(self):
        report = {"Status": "passed", "GameStarted": True}
        for timed_out, player_exit, extra in ((True, 0, ""), (False, 139, ""),
                (False, 0, "[Mixed Atmospheres] Colony validation failed: late error")):
            self.assertNotEqual(runner.classify("presentation", self.log + extra, self.probe,
                                               report, timed_out, player_exit)[1], 0)

    def test_menu_export_failure_with_null_reference_does_not_pass(self):
        # The verified false pass: marker, then the mod's export error and a
        # NullReferenceException through the patched MainMenu.OnSpawn.
        log = ("[17:30:01.000] [1] [INFO] -- MAIN MENU -- \n"
               "[17:30:01.050] [1] [ERROR] [Mixed Atmospheres] Native contract export failed: "
               "System.NullReferenceException: Object reference not set to an instance of an object\n"
               "  at MixedAtmospheres.Oni.NativeContractCapture.Export () [0x00012] in <0a1b>:0 \n"
               "  at MixedAtmospheres.Oni.MainMenuContractExportPatch.Postfix () [0x00000] in <0a1b>:0 \n"
               "NullReferenceException: Object reference not set to an instance of an object\n"
               "  at (wrapper dynamic-method) MainMenu.DMD<MainMenu::OnSpawn>(MainMenu)\n"
               "  at KMonoBehaviour.Spawn () [0x00068] in <b4ac>:0 \n")
        status, code = runner.classify("menu", log, self.probe, None, False, None, ("MA-Linux-x",))
        self.assertEqual((status, code), ("failed-mod-log-errors", 2))
        lines = runner.mod_failure_lines(log)
        self.assertTrue(any("Native contract export failed" in line for line in lines))
        self.assertIn("NullReferenceException: Object reference not set to an instance of an object", lines)
        self.assertEqual(runner.contract_export(log), "failed")
        warning = ("[17:30:01.000] [1] [INFO] -- MAIN MENU -- \n[Mixed Atmospheres] Native contract diagnostics "
                   "capture failed; gameplay is unaffected: System.NullReferenceException: x\n")
        self.assertEqual(runner.contract_export(warning), "failed")
        self.assertEqual(runner.classify("menu", warning, self.probe, None, False), ("failed-mod-log-errors", 2))

    def test_clean_menu_pass_ignores_game_noise(self):
        log = ("kGfxThreadingModeClientWorkerNativeJobs is not supported on OpenGL Core. "
               "Reverting to kGfxThreadingModeClientWorkerJobs instead.\n"
               "Could not fetch DPI for display (screen): Couldn't get DPI\n"
               "[17:01:43.045] [1] [WARNING] MixedAtmospheres.Core, Version=1.0.0.0, Culture=neutral, "
               "PublicKeyToken=null at /x/mods/Dev/MA-Linux-x has no classes inheriting from UserMod, creating one...\n"
               "[Mixed Atmospheres] Integration probe completed. Full simulation ownership is NOT certified; "
               "this package does not change colony physics. Report: /x/mods/Dev/MA-Linux-x/integration-probe.json\n"
               "[17:01:47.536] [1] [WARNING] Missing Anim: [0x31A5D250]. You may have to run Collect Anim on the Assets prefab\n"
               "[17:30:01.000] [1] [INFO] -- MAIN MENU -- \n"
               "[Mixed Atmospheres] Runtime discovery captured 900 prefabs and 300 recipes; "
               "ownership and coverage completeness remain unverified.\n"
               "[Mixed Atmospheres] Native contract captured without native initialization: /x/mods/Dev/MA-Linux-x/game-contract-local\n"
               "[17:30:09.585] [7] [ERROR] Fatal parse error in /x/game/OxygenNotIncluded_Data/StreamingAssets/"
               "templates/events/strangeasteroid/marsh_frag_table_rotated.yaml\n"
               "(Line: 358, Col: 3, Idx: 7486) - (Line: 358, Col: 3, Idx: 7486): Exception during deserialization\n"
               "Thread was being aborted.\n")
        self.assertEqual(runner.mod_failure_lines(log, ("MA-Linux-x",)), [])
        self.assertEqual(runner.classify("menu", log, self.probe, None, False, None, ("MA-Linux-x",)),
                         ("passed-scoped-main-menu", 0))

    def test_menu_requires_contract_export_completion(self):
        log = "[17:30:01.000] [1] [INFO] -- MAIN MENU -- \n"
        self.assertEqual(runner.contract_export(log), None)
        self.assertEqual(runner.classify("menu", log, self.probe, None, False),
                         ("failed-main-menu-contract-export", 2))
        # An export line from before the menu marker does not count.
        early = runner.EXPORT_DONE_MARKER + " /x\n" + log
        self.assertNotEqual(runner.classify("menu", early, self.probe, None, False)[1], 0)

    def test_mod_load_and_harmony_failures_are_detected(self):
        for line in ("[18:31:49.529] [1] [ERROR] null Exception while loading mod MixedAtmospheres.IntegrationProbe at /x.",
                     "[18:31:49.530] [1] [INFO] Failed to load mod Mixed Atmospheres - Integration Probe...disabling",
                     "HarmonyLib.HarmonyException: Patching exception in method System.Void SimMessages::Dig()",
                     "[Mixed Atmospheres] Private input observation failed: InvalidOperationException: x"):
            self.assertEqual(runner.classify("menu", self.log + line + "\n", self.probe, None, False),
                             ("failed-mod-log-errors", 2), line)
        game_only = self.log + ("[17:30:02.000] [1] [ERROR] Something in the game failed\n"
                                "NullReferenceException: Object reference not set to an instance of an object\n"
                                "  at Klei.Foo.Bar () [0x0] in <a>:0 \n")
        self.assertEqual(runner.classify("menu", game_only, self.probe, None, False)[1], 0)

    def test_mod_failure_is_not_hidden_by_steam_block(self):
        blocked = "[17:01:40.292] [1] [ERROR] Steam not initialized in time.\n"
        self.assertEqual(runner.classify("menu", blocked, self.probe, None, False, 2),
                         ("blocked-steam-authentication", 3))
        failing = blocked + "[Mixed Atmospheres] Native contract export failed: System.Exception: x\n"
        self.assertEqual(runner.classify("menu", failing, self.probe, None, False, 2)[1], 2)
        bad_probe = dict(self.probe, NativeMixtureRoundtripPassed=False)
        self.assertEqual(runner.classify("menu", blocked, bad_probe, None, False, 2),
                         ("failed-integration-probe", 2))

    def test_failed_or_unstarted_colony_cannot_pass(self):
        for report in ({"Status": "failed", "GameStarted": True}, {"Status": "passed", "GameStarted": False}):
            self.assertNotEqual(runner.classify("presentation", self.log, self.probe, report, False)[1], 0)
        self.assertEqual(runner.classify("presentation", self.log, self.probe,
                                       {"Status": "passed", "GameStarted": True}, False)[1], 0)


class SandboxTests(unittest.TestCase):
    def test_game_shares_host_pid_namespace_and_keeps_filesystem_sandbox(self):
        root = Path("/srv/state")
        command = runner.sandbox_command(root / "runs/r/home", root, root / "runs/r", Path("/games/oni"),
                                         root / "runs/r/game", keep_open=False)
        self.assertNotIn("--unshare-pid", command)
        self.assertNotIn("--unshare-all", command)
        self.assertNotIn("--proc", command)
        pairs = list(zip(command, command[1:], command[2:]))
        for expected in (("--ro-bind", "/", "/"), ("--bind", "/proc", "/proc"), ("--dev-bind", "/dev", "/dev"),
                         ("--bind", "/tmp", "/tmp"), ("--ro-bind", str(root), str(root)),
                         ("--bind", str(root / "runs/r/home"), str(Path.home())),
                         ("--ro-bind", "/games/oni", "/games/oni")):
            self.assertIn(expected, pairs)
        self.assertEqual(command[:2], ["bwrap", "--die-with-parent"])
        self.assertNotIn("--die-with-parent", runner.sandbox_command(
            root / "h", root, root / "o", Path("/g"), root / "t", keep_open=True))

    def test_load_rejection_is_rejected_on_linux(self):
        self.assertIn("Windows", runner.unsupported_mode("load-rejection", "linux"))
        self.assertIsNone(runner.unsupported_mode("menu", "linux"))
        with tempfile.TemporaryDirectory() as state, mock.patch.object(sys, "platform", "linux"), \
                mock.patch.object(sys, "argv", ["oni_headless", "--state", state, "run", "--package", state,
                                                "--mode", "load-rejection"]), \
                contextlib.redirect_stderr(io.StringIO()) as error:
            self.assertEqual(runner.main(), 2)
        self.assertIn("load-rejection cannot run on Linux", error.getvalue())


class ProcessTests(unittest.TestCase):
    def test_stop_process_tolerates_already_exited_process(self):
        with tempfile.TemporaryDirectory() as directory:
            record = runner.spawn(["true"], Path(directory) / "log", dict(os.environ))
            runner.CHILDREN[record["pid"]].wait()
            runner.stop_process(record)
            # Simulate exit between the identity check and the signal.
            with mock.patch.object(runner, "alive", return_value=True), \
                    mock.patch.object(runner.os, "getpgid", side_effect=ProcessLookupError):
                runner.stop_process(record)

    def test_session_commands_share_one_lock(self):
        with tempfile.TemporaryDirectory() as state:
            (Path(state) / "session.json").write_text("{}")
            with runner.locked(Path(state) / runner.LOCK_NAME):
                for argv in (["stop-game"], ["stop"], ["start"], ["run", "--package", state]):
                    with mock.patch.object(sys, "argv", ["oni_headless", "--state", state, *argv]):
                        with self.assertRaisesRegex(RuntimeError, "Another runner operation is active"):
                            runner.main()
            self.assertEqual(set(runner.LOCKED_COMMANDS), {"setup", "start", "stop", "stop-game", "run"})


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        paths = ["MixedAtmospheres.Oni.dll", "MixedAtmospheres.Core.dll", "native/linux-x64/libmixed_atmospheres.so"]
        for name in paths:
            file = self.root / name
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_bytes(b"test fixture")
        self.manifest = {"kind": "integration-probe", "release_ready": False,
                         "source_revision": "0123456789abcdef0123456789abcdef01234567", "source_dirty": True,
                         "files": {name: runner.digest(self.root / name) for name in paths}}
        self.write_manifest()

    def write_manifest(self):
        (self.root / "package-manifest.json").write_text(json.dumps(self.manifest))

    def test_clean_package_passes_but_tampering_does_not(self):
        runner.verify_package(self.root)
        (self.root / "MixedAtmospheres.Oni.dll").write_bytes(b"changed")
        with self.assertRaises(ValueError):
            runner.verify_package(self.root)

    def test_manifest_must_record_source_revision(self):
        for key, value in (("source_revision", None), ("source_revision", "HEAD"), ("source_dirty", "yes")):
            manifest = dict(self.manifest)
            if value is None:
                del manifest[key]
            else:
                manifest[key] = value
            (self.root / "package-manifest.json").write_text(json.dumps(manifest))
            with self.assertRaisesRegex(ValueError, "source_revision"):
                runner.verify_package(self.root)

    def test_result_revision_comes_from_package_not_checkout(self):
        fields = runner.revision_fields(runner.verify_package(self.root))
        self.assertEqual(fields["source_revision"], self.manifest["source_revision"])
        self.assertIs(fields["source_dirty"], True)
        head = subprocess.run(["git", "rev-parse", "HEAD"], cwd=runner.ROOT, text=True,
                              capture_output=True).stdout.strip() or None
        self.assertEqual(fields["runner_git_revision"], head)
        self.assertNotEqual(fields["source_revision"], fields["runner_git_revision"])

    def test_unlisted_dll_is_rejected(self):
        (self.root / "StaleMod.dll").write_bytes(b"unlisted")
        with self.assertRaises(ValueError):
            runner.verify_package(self.root)

    def test_manifest_cannot_include_outside_files(self):
        self.manifest["files"]["../outside"] = "unused"
        self.write_manifest()
        with self.assertRaises(ValueError):
            runner.verify_package(self.root)

    def test_symlinks_are_rejected_even_with_matching_hash(self):
        file = self.root / "MixedAtmospheres.Core.dll"
        target = self.root / "MixedAtmospheres.Oni.dll"
        file.unlink()
        file.symlink_to(target.name)
        with self.assertRaises(ValueError):
            runner.verify_package(self.root)


if __name__ == "__main__":
    unittest.main()
