"""Isolation and evidence regressions for the licensed Windows Unity runner."""
import copy
import importlib.util
import json
import os
import signal
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

if not sys.platform.startswith("linux"):
    raise unittest.SkipTest("The Proton runner requires a Linux host")

SPEC = importlib.util.spec_from_file_location("oni_proton_headless", Path(__file__).parents[1] / "oni_proton_headless.py")
runner = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(runner)
common = runner.common


class ProtonIsolationTests(unittest.TestCase):
    def test_base_game_profile_uses_the_normal_persisted_dlc_preferences(self):
        with tempfile.TemporaryDirectory() as directory:
            profile = Path(directory)
            runner.base_game_preferences(profile)
            text = (profile / "kplayerprefs.yaml").read_text()
            self.assertIn("EXPANSION1_ID.ENABLED: 0", text)
            self.assertNotIn("MA_", text)

    def test_owner_timing_excludes_missing_frames_and_session_changes(self):
        samples = [dict(session=1, frame=1, wall=1, owner_ms=10),
                   dict(session=1, frame=2, wall=1.2, owner_ms=20),
                   dict(session=1, frame=5, wall=2, owner_ms=30),
                   dict(session=2, frame=6, wall=10, owner_ms=40)]
        timing = runner.owner_timing(samples)
        self.assertEqual(timing["nonadjacent_intervals"], 2)
        self.assertEqual(timing["owner_milliseconds"]["mean"], 25)
        self.assertEqual(timing["observed_milliseconds"]["samples"], 1)
        self.assertAlmostEqual(timing["observed_milliseconds"]["mean"], 200)
        self.assertIsNone(runner.owner_timing([])["observed_milliseconds"])

    def test_owner_evidence_requires_physical_progress_conservation_and_scenario_success(self):
        owner = dict(State=1, Faulted=False, Error="", CompletedFrames=32, OwnedCells=100,
                     Publications=32, MaximumRelativeMassError=1e-15, MaximumRelativeEnergyError=1e-15,
                     Scenarios=[dict(Passed=True)])
        self.assertTrue(runner.colony_owner_passed(owner))
        self.assertFalse(runner.colony_owner_passed(None))
        for key, value in (("State", 2), ("Faulted", True), ("CompletedFrames", 0), ("OwnedCells", 0),
                           ("MaximumRelativeMassError", float("nan")), ("MaximumRelativeEnergyError", 1e-8),
                           ("Error", "fault"), ("Scenarios", [dict(Passed=False)]), ("RendererError", "failed")):
            with self.subTest(key=key):
                self.assertFalse(runner.colony_owner_passed(dict(owner, **{key: value})))

    def test_owner_modes_admit_batches_and_fresh_process_loads(self):
        args = SimpleNamespace(mode="colony-owner", frames=32, load_save=None, save_colony=True,
                               scenario="base,mixed-pipe", cluster="SandstoneDefault",
                               resave_before_resume=False, reload_in_process=False, verify_save_io_recovery=False)
        runner.validate_arguments(args)
        with tempfile.NamedTemporaryFile(suffix=".sav") as save:
            args.mode = "colony-owner-load"
            args.load_save = Path(save.name)
            args.scenario = args.cluster = None
            runner.validate_arguments(args)
            args.reload_in_process = True
            runner.validate_arguments(args)
            args.load_save = None
            with self.assertRaises(ValueError):
                runner.validate_arguments(args)

    def test_inspection_rejects_unbounded_or_menu_holds_before_game_start(self):
        for seconds in (-1, 301, 1.5, True):
            with self.subTest(seconds=seconds), self.assertRaisesRegex(ValueError, "Completed inspection"):
                runner.validate_arguments(SimpleNamespace(inspection_seconds=seconds, mode="native-domain"))
        with self.assertRaisesRegex(ValueError, "Completed inspection"):
            runner.validate_arguments(SimpleNamespace(inspection_seconds=1, mode="menu"))

    def test_unsupported_graphics_selection_rejects_before_lifecycle_arguments_are_used(self):
        for graphics in ('vulkan', '', None, 1):
            with self.subTest(graphics=graphics), self.assertRaisesRegex(ValueError,'Graphics API'):
                runner.validate_arguments(SimpleNamespace(graphics_api=graphics))

    def test_explicit_renderer_selection_is_applied_and_reported_with_default_preserved(self):
        for requested,expected in ((None,'1'),('0','0'),('1','1')):
            with mock.patch.dict(os.environ,{},clear=True):
                if requested is not None:os.environ['PROTON_USE_WINED3D']=requested
                env=runner.game_env(Path('/srv/state'),Path('/srv/run'),Path('/srv/game'),':123')
            self.assertEqual(env['PROTON_USE_WINED3D'],expected)
            self.assertEqual(runner.compatibility_settings(env)['PROTON_USE_WINED3D'],expected)

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def test_windows_save_mapping_requires_own_real_file_and_rejects_escape(self):
        output = self.root / "run"
        save = output / "compatdata/pfx/drive_c/users/steamuser/Documents/colony.sav"
        save.parent.mkdir(parents=True)
        save.write_bytes(b"private fixture")
        self.assertEqual(runner.host_save_path(r"C:\users\steamuser\Documents\colony.sav", output), save)
        self.assertEqual(runner.host_save_path(runner.windows_path(save), output), save)
        outside = self.root / "outside.sav"
        outside.write_bytes(b"must not be read")
        for value in (r"C:\users\..\outside.sav", r"colony.sav", r"D:\colony.sav",
                      r"\\server\share\colony.sav", runner.windows_path(outside)):
            with self.subTest(value=value), self.assertRaises(ValueError):
                runner.host_save_path(value, output)
        link = save.parent / "escape.sav"
        link.symlink_to(outside)
        with self.assertRaises(ValueError):
            runner.host_save_path(r"C:\users\steamuser\Documents\escape.sav", output)

    def test_output_cannot_overlap_persistent_or_source_paths_in_either_direction(self):
        protected = self.root / "steam"
        for output in (protected, protected / "run", self.root):
            with self.assertRaises(ValueError):
                runner.private_output(output, (protected,))
        runner.private_output(self.root / "fresh", (protected,))

    def test_sandbox_keeps_game_steam_and_root_read_only_and_masks_user_home(self):
        home, state, output, game, proton = [self.root / name for name in ("home", "steam", "run", "game", "proton")]
        command = runner.sandbox_command(home, state, output, game, proton, ["run", "wineboot", "-u"])
        mounts = {command[index + 1]: (value, command[index + 2]) for index, value in enumerate(command)
                  if value in ("--bind", "--ro-bind", "--dev-bind")}
        for source in (state, game):
            self.assertEqual(mounts[str(source)], ("--ro-bind", str(source)))
        self.assertEqual(mounts["/"], ("--ro-bind", "/"))
        self.assertEqual(mounts[str(home)], ("--bind", str(Path.home())))
        self.assertEqual(mounts[str(output)], ("--bind", str(output)))
        self.assertIn("--die-with-parent", command)

    def test_proton_does_not_inherit_desktop_or_native_test_wine_paths(self):
        with mock.patch.dict(os.environ, {"LD_LIBRARY_PATH": "/private/desktop", "WINEPREFIX": "/other/prefix",
                                         "WINESERVER": "/other/server", "WAYLAND_DISPLAY": "wayland-0"}):
            env = runner.game_env(self.root / "steam", self.root / "run", self.root / "game", ":123")
        for key in ("LD_LIBRARY_PATH", "WINEPREFIX", "WINESERVER", "WAYLAND_DISPLAY"):
            self.assertNotIn(key, env)
        self.assertEqual(env["STEAM_COMPAT_DATA_PATH"], str(self.root / "run/compatdata"))
        self.assertEqual(env["DISPLAY"], ":123")

    def test_compatibility_receipt_records_controlled_overrides_without_inherited_secrets(self):
        env = dict(MONOMOD_RunningOnWine='false', MONOMOD_DMDType='methodbuilder', PROTON_USE_WINED3D='1',
                   WINEDEBUG='-all', STEAM_SESSION_TOKEN='must-not-export', XAUTHORITY='/private/cookie-file')
        self.assertEqual(runner.compatibility_settings(env),
            dict(MONOMOD_RunningOnWine='false',MONOMOD_DMDType='methodbuilder',PROTON_USE_WINED3D='1',WINEDEBUG='-all'))
        self.assertEqual(runner.compatibility_settings({}), {})

    def test_failed_wine_cleanup_still_stops_only_owned_game_and_display(self):
        game, display = {"pid": 111}, {"pid": 222}
        (self.root / "run/compatdata/pfx").mkdir(parents=True)
        with mock.patch.object(common, "stop_process") as stop, mock.patch.object(
                runner.subprocess, "run", side_effect=subprocess.TimeoutExpired("wineserver", 30)) as run:
            with self.assertRaisesRegex(RuntimeError, "private Wine prefix"):
                runner.cleanup_run(game, display, True, self.root / "proton", self.root / "run")
        self.assertEqual(stop.call_args_list, [mock.call(game), mock.call(display)])
        self.assertEqual(run.call_args.kwargs["env"]["WINEPREFIX"], str(self.root / "run/compatdata/pfx"))
        self.assertEqual(run.call_args.args[0], [str(self.root / "proton/files/bin/wineserver"), "-k"])

    def test_prefix_parent_exists_before_proton_takes_its_lock(self):
        output = self.root / "run"
        output.mkdir()
        def launch(command, log, env):
            self.assertTrue((output / "compatdata").is_dir())
            raise OSError("injected launch failure")
        with mock.patch.object(common, "spawn", side_effect=launch), self.assertRaisesRegex(OSError, "injected"):
            runner.initialize_prefix(["proton", "run", "wineboot", "-u"], output, {})

    def test_no_wine_server_cleanup_for_a_prefix_that_never_existed(self):
        with mock.patch.object(common, "stop_process") as stop, mock.patch.object(runner.subprocess, "run") as run:
            runner.cleanup_run(None, {"pid": 222}, True, self.root / "proton", self.root / "run")
        run.assert_not_called()
        self.assertEqual(stop.call_args_list, [mock.call(None), mock.call({"pid": 222})])

    def test_already_exited_server_requires_a_successful_bounded_wait(self):
        (self.root / "run/compatdata/pfx").mkdir(parents=True)
        server=str(self.root / "proton/files/bin/wineserver")
        for wait_error in (False,True):
            responses=[subprocess.CompletedProcess([server,"-k"],1,stderr=b""),
                       subprocess.TimeoutExpired(server,30) if wait_error else subprocess.CompletedProcess([server,"-w"],0)]
            with mock.patch.object(common,"stop_process") as stop, mock.patch.object(runner.subprocess,"run",side_effect=responses) as run:
                if wait_error:
                    with self.assertRaisesRegex(RuntimeError,"private Wine prefix"):
                        runner.cleanup_run(None,{"pid":222},True,self.root/"proton",self.root/"run")
                else:runner.cleanup_run(None,{"pid":222},True,self.root/"proton",self.root/"run")
                self.assertEqual([call.args[0] for call in run.call_args_list],[[server,"-k"],[server,"-w"]])
                self.assertEqual(run.call_args.kwargs["timeout"],30)
                self.assertEqual(stop.call_args_list[-1],mock.call({"pid":222}))

    def test_proton_and_native_linux_use_the_same_whole_run_lock(self):
        with common.locked(self.root / common.LOCK_NAME), mock.patch.object(
                sys, "argv", ["oni_proton_headless", "--state", str(self.root), "--proton", "/tool", "--package", "/package"]), \
                mock.patch.object(runner, "playtest") as play:
            with self.assertRaisesRegex(RuntimeError, "Another runner operation"):
                runner.main()
            play.assert_not_called()

    def test_termination_unwinds_run_cleanup_releases_lock_and_restores_the_callers_handler(self):
        previous=signal.getsignal(signal.SIGTERM);cleaned=[]
        def stop_during_run(*args):
            try:signal.getsignal(signal.SIGTERM)(signal.SIGTERM,None)
            finally:cleaned.append(True)
        with mock.patch.object(sys,"argv",["oni_proton_headless","--state",str(self.root),"--proton","/tool","--package","/package"]), \
                mock.patch.object(runner,"playtest",side_effect=stop_during_run):
            with self.assertRaises(SystemExit) as stopped:runner.main()
        self.assertEqual(stopped.exception.code,128+signal.SIGTERM)
        self.assertEqual(cleaned,[True]);self.assertEqual(signal.getsignal(signal.SIGTERM),previous)
        with common.locked(self.root/common.LOCK_NAME):pass


class WindowsEvidenceTests(unittest.TestCase):
    def test_windows_probe_requires_explicit_target_and_keeps_failure_precedence(self):
        probe = {"Platform": "WindowsPlayer", **{flag: True for flag in common.PROBE_FLAGS}}
        log = common.MAIN_MENU_MARKER + "\n" + common.EXPORT_DONE_MARKER
        report = {"Status": "passed", "GameStarted": True}
        self.assertEqual(common.classify("native-domain", log, probe, report, False)[0], "failed-integration-probe")
        self.assertEqual(common.classify("native-domain", log, probe, report, False, expected_platform=runner.PLATFORM),
                         ("passed-native-domain", 0))
        self.assertEqual(common.classify("native-domain", log + "\n[Mixed Atmospheres] fatal error", probe, report,
                                        False, expected_platform=runner.PLATFORM)[0], "failed-mod-log-errors")

    def test_platform_specific_package_cannot_satisfy_the_other_native_target(self):
        with tempfile.TemporaryDirectory() as directory:
            package = Path(directory)
            names = ["MixedAtmospheres.Core.dll", "MixedAtmospheres.Oni.dll", "native/win-x64/mixed_atmospheres.dll"]
            for name in names:
                file = package / name
                file.parent.mkdir(parents=True, exist_ok=True)
                file.write_bytes(b"fixture")
            manifest = {"kind": "integration-probe", "release_ready": False, "source_dirty": False,
                        "source_revision": "a" * 40, "files": {name: common.digest(package / name) for name in names}}
            common.write_json(package / "package-manifest.json", manifest)
            self.assertEqual(common.verify_package(package, "win-x64"), manifest)
            with self.assertRaisesRegex(ValueError, "Linux native"):
                common.verify_package(package)
            (package / names[-1]).write_bytes(b"tampered")
            with self.assertRaisesRegex(ValueError, "hash or path mismatch"):
                common.verify_package(package, "win-x64")

    def test_load_controls_cannot_be_accepted_on_an_initial_scene(self):
        args = SimpleNamespace(mode="native-domain", frames=120, load_save=None, save_colony=False,
                               resave_before_resume=False, reload_in_process=False, verify_save_io_recovery=False)
        runner.validate_arguments(args)
        for key in ("resave_before_resume", "reload_in_process", "verify_save_io_recovery"):
            invalid = copy.copy(args)
            setattr(invalid, key, True)
            with self.subTest(key=key), self.assertRaises(ValueError):
                runner.validate_arguments(invalid)
        options = runner.domain_options(args)
        self.assertEqual(options["expected_platform"], "WindowsPlayer")
        self.assertFalse(options["restored"])
        self.assertFalse(options["configured"])
        for mode,liquid in (("native-configured-domain",False),("native-configured-water-domain",True)):
            configured=copy.copy(args);configured.mode=mode;configured.save_colony=True
            runner.validate_arguments(configured);options=runner.domain_options(configured)
            self.assertIn(mode,runner.MODES)
            self.assertTrue(options["configured"] and options["saved"]);self.assertIs(options["liquid"],liquid)
        args.mode="menu"
        runner.validate_arguments(args)
        args.save_colony=True
        with self.assertRaisesRegex(ValueError,"initial domain"):
            runner.validate_arguments(args)


if __name__ == "__main__":
    unittest.main()
