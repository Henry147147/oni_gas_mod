"""Dry-run checks of the release acceptance plan, commands, guards and criteria; no game runs."""
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).parents[1]))
import acceptance_suite as suite

REVISION = "a" * 40


def names(steps):
    return [step.name for step in steps]


def write(path: Path, value) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value) if not isinstance(value, str) else value)
    return path


def save_bytes(header: dict, build: int = suite.mod_identity.INSPECTED_ONI_BUILD) -> bytes:
    info = json.dumps(header).encode()
    return struct.pack("<IiIi", build, len(info), 1, 0) + info + b"\0" * 32


class PlanTests(unittest.TestCase):
    def test_patch_uses_original_gameplay_batches_and_a_separate_real_save_reload(self):
        steps = suite.plan("patch")
        self.assertEqual(names(steps), ["linux-offline", "linux-build", "windows-build", "linux-b1",
                                        "linux-b13", "windows-meteor-reload"])
        batches = [s.options["scenario"] for s in steps if s.kind == "batch"]
        self.assertEqual(batches, [suite.LINUX_BATCHES[0], suite.LINUX_BATCHES[12]])
        self.assertEqual(names(suite.select(steps, "windows-meteor-reload")), ["windows-build", "windows-meteor-reload"])
        command = suite.describe(steps[-1])["command"]
        self.assertEqual(command[:2], [str(suite.GAME_SLOT), "low"])
        self.assertIn("--load-save", command)
        self.assertIn("--reload-in-process", command)
        self.assertTrue(all(cmd[:2] == [str(suite.RUN_LOCKED), "high"] for cmd in suite.describe(steps[0])["commands"]))

    def test_affected_scenario_adds_its_whole_batch_once_on_each_applicable_player(self):
        steps = suite.plan("patch", scenarios=("airflow", "airflow", "falling"))
        added = [s for s in steps if s.name not in names(suite.plan("patch"))]
        self.assertEqual(names(added), ["linux-b2", "windows-b2"])
        self.assertTrue(all(s.options["scenario"] == suite.LINUX_BATCHES[1] for s in added))
        with self.assertRaisesRegex(ValueError, "No gameplay batch"):
            suite.plan("patch", scenarios=("typo",))

    def test_quick_tier_runs_every_linux_batch_asteroid_and_one_cycle(self):
        steps = suite.plan("quick")
        batches = [s for s in steps if s.kind == "batch"]
        self.assertEqual([s.options["scenario"] for s in batches], list(suite.LINUX_BATCHES))
        self.assertEqual(len(batches), 15)
        asteroids = [s for s in steps if s.kind == "asteroid"]
        self.assertEqual([s.options["cluster"] for s in asteroids], list(suite.ASTEROIDS))
        self.assertEqual({s.options["frames"] for s in asteroids}, {3000})
        cycles = [s for s in steps if s.kind == "cycle"]
        self.assertEqual([(s.name, s.options["frames"]) for s in cycles], [("linux-one-cycle", 3000)])
        self.assertIn("windows-build", names(steps))
        windows = [s.options["scenario"] for s in steps if s.kind == "proton-batch"]
        self.assertEqual(windows, [suite.LINUX_BATCHES[i - 1] for i in (2, 3, 4, 6, 7, 10, 11, 12)])
        for name in ("windows-contact", "windows-base-source", "windows-prepared-cycle", "windows-autosave-cold",
                     "windows-settings-source", "windows-settings-load", "windows-spacedout-source",
                     "windows-spacedout-cold", "windows-spacedout-menu", "windows-spacedout-ui",
                     "windows-spacedout-ui-load"):
            self.assertIn(name, names(steps))
        self.assertEqual(len(set(names(steps))), len(steps))

    def test_release_adds_the_remaining_scenarios_and_the_ten_cycle_run(self):
        quick, release = names(suite.plan("quick")), suite.plan("release")
        extra = [s for s in release if s.name not in quick]
        self.assertEqual([s.name for s in extra],
                         [f"linux-x{i}" for i in range(1, len(suite.LINUX_EXTRA_BATCHES) + 1)] +
                         ["linux-resume-refused", "linux-menu-rephysicalized", "linux-ten-cycle"])
        self.assertEqual(extra[-1].options["frames"], 30000)
        # Another mod loaded first is no refusal: the main menu with Rephysicalized first, the release folder judged by its log.
        menu = suite.describe(extra[-2])["command"]
        self.assertEqual((extra[-2].kind, extra[-2].deps), ("menu-other-mod", ("linux-build",)))
        self.assertEqual(menu[menu.index("--mode") + 1], "menu")
        self.assertEqual(menu[menu.index("--extra-mod-first") + 1], str(suite.ROOT / "local/other-mods/src/Rephysicalized"))
        # The guard of a refused mod resumes the autosave that the one-cycle run wrote on the running build.
        self.assertEqual((extra[-3].kind, extra[-3].deps), ("resume-refused", ("linux-one-cycle",)))
        command = suite.describe(extra[-3])["command"]
        self.assertEqual(command[command.index("--mode") + 1], "menu")
        self.assertEqual(command[command.index("--resume-save") + 1], "<runs>/linux-one-cycle/auto_save/<autosave>")
        self.assertEqual(command[command.index("--extra-mod-first") + 1], "<runs>/linux-resume-refused-other-mod")
        self.assertEqual(set(quick) - set(names(release)), set())
        self.assertEqual(names(suite.plan("release", ("windows",))), names(suite.plan("quick", ("windows",))))

    def test_every_in_game_scenario_runs_in_the_release_tier(self):
        import oni_headless
        ran = [name for s in suite.plan("release") if s.kind == "batch" for name in s.options["scenario"].split(",")]
        self.assertEqual(set(oni_headless.SCENARIOS) - set(ran), set(suite.UNTIERED_SCENARIOS))
        # The three rocket chains each build their rocket first; the four every-building parts run with the base
        # game's buildings and with Spaced Out!'s.
        self.assertEqual(len(ran), len(set(ran)) + 3 + 4)
        self.assertEqual(set(ran) - set(oni_headless.SCENARIOS), set())
        first = {name for batch in suite.LINUX_BATCHES for name in batch.split(",")}
        self.assertEqual({name for batch, *_ in suite.LINUX_EXTRA_BATCHES for name in batch.split(",")} & first, set())

    def test_remaining_scenarios_carry_their_content_arguments_and_environment(self):
        extra = [s for s in suite.plan("release") if s.name.startswith("linux-x")]
        steps = {s.options["scenario"]: s for s in extra}
        package, out = Path("/p"), Path("/o")
        # Batches that need Spaced Out! content besides those named for it: uranium ore, rockets, its critters.
        with_spaced_out = {"full-colony-spacedout", "message-edges,germ-cleaner", "every-rocket-building",
                           "every-living-1", "every-living-2", "every-living-3"}
        building_parts = {}
        for step in extra:
            scenario = step.options["scenario"]
            command = suite.linux_command(step, package, out, CommandTests.infra)
            mask = command[command.index("--dlc-mask") + 1]
            self.assertEqual(command[command.index("--scenario") + 1], scenario)
            if scenario.startswith("every-building-"):
                building_parts.setdefault(scenario, set()).add(mask)
                self.assertEqual(command[command.index("--scenario") + 2:], ["--speed", "3"])
                continue
            spaced_out = scenario.startswith("spaced-out-") or scenario in with_spaced_out
            self.assertEqual(mask, "1" if spaced_out else "0", scenario)
        self.assertEqual(building_parts, {f"every-building-{part}": {"0", "1"} for part in (1, 2, 3, 4)})
        lifecycle = suite.linux_command(steps["activity-lifecycle"], package, out, CommandTests.infra)
        self.assertEqual(lifecycle[lifecycle.index("--scenario") + 2:], ["--save-colony", "--frames", "450", "--speed", "3"])
        self.assertEqual(steps["germ-oracle"].options["env"], {"MA_COLONY_GERM_ORACLE": "1"})
        self.assertEqual(steps["mixed-pipe-save-guard"].options["env"], {"MA_COLONY_PIPE_SAVES": "0"})
        self.assertEqual(steps["playtest-ui"].options["env"], {})
        # A chain's scenarios stay in the order that builds on each other.
        abandon = next(s for s in steps if s.endswith("spaced-out-rocket-abandon")).split(",")
        self.assertEqual(abandon, ["spaced-out-rocket", "spaced-out-rocket-ports", "spaced-out-rocket-flight", "spaced-out-rocket-abandon"])

    def test_patch_scenario_selection_reaches_the_remaining_scenarios(self):
        steps = suite.plan("patch", scenarios=("spaced-out-hep",))
        added = [s for s in steps if s.name not in names(suite.plan("patch"))]
        self.assertEqual(len(added), 1)
        self.assertEqual((added[0].platform, added[0].options["dlc_mask"]), ("linux", 1))
        self.assertIn("spaced-out-hep", added[0].options["scenario"].split(","))

    def test_platform_selection_and_unknown_values(self):
        self.assertTrue(all(s.platform == "linux" for s in suite.plan("quick", ("linux",))))
        self.assertTrue(all(s.platform == "windows" for s in suite.plan("quick", ("windows",))))
        with self.assertRaises(ValueError):
            suite.plan("weekly")
        with self.assertRaises(ValueError):
            suite.plan("quick", ("macos",))

    def test_only_pulls_in_dependencies_and_rejects_unknown_steps(self):
        steps = suite.plan("quick")
        chosen = names(suite.select(steps, "windows-autosave-cold,linux-b2"))
        self.assertEqual(chosen, ["linux-build", "windows-build", "linux-b2", "windows-base-source",
                                  "windows-prepared-cycle", "windows-autosave-cold"])
        with self.assertRaises(ValueError):
            suite.select(steps, "linux-b16")

    def test_dependencies_precede_their_steps(self):
        steps = suite.plan("release")
        seen = set()
        for step in steps:
            self.assertTrue(set(step.deps) <= seen, step.name)
            seen.add(step.name)


class CommandTests(unittest.TestCase):
    infra = suite.Infra(Path("/linux"), Path("/windows"), Path("/proton"), Path("/mingw"), Path("/wine"), ":99")

    def test_every_game_takes_the_low_game_slot_with_the_gpu_display(self):
        for step in suite.plan("release"):
            if step.kind == "build":
                continue
            command = suite.describe(step)["command"]
            self.assertEqual(command[:2], [str(suite.GAME_SLOT), "low"], step.name)
            self.assertEqual(command[command.index("--gpu-display") + 1], ":99", step.name)
            self.assertIn("--output", command)

    def test_linux_commands_keep_the_driver_arguments(self):
        steps = {s.name: s for s in suite.plan("release")}
        package, out = Path("/p"), Path("/o")
        batch = suite.linux_command(steps["linux-b2"], package, out, self.infra)
        self.assertEqual(batch[batch.index("--scenario") + 1], suite.LINUX_BATCHES[1])
        self.assertEqual(batch[batch.index("--timeout") + 1], "6800")
        self.assertEqual(batch[batch.index("--mode") + 1], "colony-owner")
        asteroid = suite.linux_command(steps["linux-Oasis"], package, out, self.infra)
        self.assertEqual(asteroid[asteroid.index("--cluster") + 1:asteroid.index("--cluster") + 4], ["Oasis", "--frames", "3000"])
        cycle = suite.linux_command(steps["linux-ten-cycle"], package, out, self.infra, load=Path("/copy.sav"))
        for flag, value in (("--mode", "colony-owner-load"), ("--load-save", "/copy.sav"), ("--frames", "30000"),
                            ("--timeout", "172800"), ("--dlc-mask", "0")):
            self.assertEqual(cycle[cycle.index(flag) + 1], value)
        self.assertIn("--long-run", cycle)

    def test_frozen_save_must_be_a_base_game_colony(self):
        for header in ({}, {"dlcIds": [""]}, {"dlcId": None, "dlcIds": []}):
            suite.require_base_game_save(header)
        for header in ({"dlcIds": ["EXPANSION1_ID"]}, {"dlcId": "EXPANSION1_ID", "dlcIds": [""]}):
            with self.assertRaises(suite.Refused):
                suite.require_base_game_save(header)

    def test_windows_wrappers_and_arguments(self):
        steps = {s.name: s for s in suite.plan("quick")}
        source, out, load = Path("/src"), Path("/o"), Path("/in.sav")
        prepared = suite.windows_command(steps["windows-prepared-cycle"], Path("/w"), out, self.infra, source, load)
        self.assertEqual(prepared[2:5], ["_proton-longrun", "--source", "/src"])
        self.assertEqual(prepared[prepared.index("--frames") + 1], "3132")
        self.assertIn("--quit-from-menu", prepared)
        ui = suite.windows_command(steps["windows-spacedout-ui"], Path("/w"), out, self.infra, source)
        self.assertEqual(ui[2:4], ["_ui-stage", "ordinary-ui"])
        self.assertNotIn("--dlc-mask", ui)
        own, runner = suite.split_runner(ui[2:])
        self.assertEqual(runner[runner.index("--mode") + 1], "menu")
        self.assertEqual(suite.parser().parse_args(own).stage, "ordinary-ui")
        cold = suite.windows_command(steps["windows-spacedout-menu"], Path("/w"), out, self.infra, source, load)
        self.assertEqual(cold[1], "tools/oni_proton_headless.py")
        self.assertEqual(cold[cold.index("--dlc-mask") + 1], "1")
        self.assertIn("--reload-in-process", cold)
        settings = suite.windows_command(steps["windows-settings-source"], Path("/w"), out, self.infra, source, Path("/preset.json"))
        self.assertEqual(settings[settings.index("--settings-preset") + 1], "/preset.json")
        self.assertEqual(settings[settings.index("--inspection-seconds") + 1], "120")
        self.assertNotIn("--quit-from-menu", settings)
        batch = suite.windows_command(steps["windows-b11"], Path("/w"), out, self.infra, source)
        self.assertEqual(batch[batch.index("--scenario") + 1], suite.LINUX_BATCHES[10])
        self.assertEqual(batch[batch.index("--timeout") + 1], "1800")

    def test_mod_overrides_are_never_inherited(self):
        with mock.patch.dict(suite.os.environ, {"MA_COLONY_SPACED_OUT_CONTROL": "1", "KEEP": "1"}):
            env = suite.clean_env({"MA_COLONY_FAULT_DUMP": "/x"})
        self.assertNotIn("MA_COLONY_SPACED_OUT_CONTROL", env)
        self.assertEqual((env["KEEP"], env["MA_COLONY_FAULT_DUMP"]), ("1", "/x"))

    def test_settings_ui_pins_game_and_desktop_resolution(self):
        runner = SimpleNamespace(
            start_display=lambda state, out, gpu: (None, gpu, {}),
            sandbox_command=lambda home, state, out, game, proton, tail: tail)
        suite.pin_ui_resolution(runner)
        with mock.patch.object(suite.subprocess, "run") as run:
            self.assertEqual(runner.start_display(None, None, ":99"), (None, ":99", {}))
        run.assert_called_once_with(["xrandr", "--display", ":99", "--fb", "1920x1080"], check=True)
        game = runner.sandbox_command(None, None, None, None, None,
                                      ["run", "game", "-screen-width", "1280", "-screen-height", "720"])
        self.assertEqual(game[-4:], ["-screen-width", "1920", "-screen-height", "1080"])
        self.assertEqual(runner.sandbox_command(None, None, None, None, None, ["run", "wineboot", "-u"]),
                         ["run", "wineboot", "-u"])
        with self.assertRaises(RuntimeError):
            runner.sandbox_command(None, None, None, None, None, ["run", "game", "-screen-width", "1280"])

    def test_settings_ui_stage_uses_the_resolution_wrapper(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "settings"
            output.mkdir()
            process = mock.Mock()
            process.wait.return_value = 0
            with mock.patch.object(suite.subprocess, "Popen", return_value=process) as popen, \
                 mock.patch.object(suite, "Desktop"), mock.patch.object(suite, "settings_clicks"):
                self.assertEqual(suite.ui_stage("settings", Path(directory), output, ":99", ["--mode", "colony-owner"]), 0)
        self.assertEqual(popen.call_args.args[0][2:5], ["_proton-settings-ui", "--source", directory])


class SettingsClickTests(unittest.TestCase):
    class Desktop:
        bin = Path("/bin")

        def __init__(self, quit_after: int | None = None, geometry: str = "1920 1080"):
            self.quit_after, self.geometry = quit_after, geometry
            self.clicks, self.shots = [], []
            self.quit_clicks = 0

        def run(self, *command):
            if "getdisplaygeometry" in command:
                return self.geometry
            if "search" in command:
                return "12345"
            return ""

        def click(self, x, y):
            self.clicks.append((x, y))
            if (x, y) == (641, 406):
                self.quit_clicks += 1

        def key(self, key):
            pass

        def shot(self, name):
            self.shots.append(name)

        def shot_settings_tab(self, name, selected):
            self.shot(name)

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.output = Path(self.directory.name)
        report = self.output / suite.PROTON_PROFILE / suite.VALIDATION / "colony-validation.json"
        write(report, {"Status": "passed"})

    def tearDown(self):
        self.directory.cleanup()

    def run_clicks(self, desktop, resolution="1920x1080"):
        def log():
            value = (f"Applying resolution {resolution}\nSettings window opened (Advanced)\n"
                     "Settings window opened (Advanced, this colony's settings)\n")
            if desktop.quit_after is not None and desktop.quit_clicks >= desktop.quit_after:
                value += "Game.OnApplicationQuit()\n"
            return value

        def wait(predicate, limit=900, stop_on_exit=True):
            if not predicate():
                raise TimeoutError("expected evidence was absent")

        with mock.patch.object(suite.time, "sleep"):
            suite.settings_clicks(desktop, self.output, log, wait)

    def test_settings_shot_requires_the_expected_tab_to_be_highlighted(self):
        desktop = object.__new__(suite.Desktop)
        desktop.output = self.output
        # The stale clicks in the compact UI left Solver selected in all screenshots.
        neutral, highlight = bytes((87, 87, 102)), bytes((140, 89, 118))
        for selected, offset in enumerate((0, 86, 173)):
            pixels = bytearray(neutral * 174)
            pixels[3 * offset:3 * offset + 3] = highlight
            with self.subTest(selected=selected), mock.patch.object(desktop, "shot"), mock.patch.object(desktop, "park"), \
                    mock.patch.object(suite.subprocess, "check_output", return_value=bytes(pixels)):
                desktop.shot_settings_tab(suite.SETTINGS_SHOTS[selected], selected)
                with self.assertRaisesRegex(RuntimeError, "requested Settings tab"):
                    desktop.shot_settings_tab("wrong-tab", (selected + 1) % 3)

    def test_settings_shot_parks_the_pointer_first(self):
        # The pointer rests where the previous game's last click left it (the Performance
        # tab after a settings run); its hover fill made the first screenshot of the
        # next run fail (1.13.0 and 1.13.3 release runs). The check itself stays strict.
        desktop = object.__new__(suite.Desktop)
        desktop.output, desktop.bin, desktop.env = self.output, Path("/bin"), {}
        calls = []
        neutral, highlight, hover = bytes((87, 87, 102)), bytes((140, 89, 118)), bytes((111, 110, 128))
        clean = bytearray(neutral * 174); clean[0:3] = highlight
        hovered = bytearray(clean); hovered[3 * 173:3 * 173 + 3] = hover

        def run(*command):
            calls.append("geometry" if "getdisplaygeometry" in command else "move " + " ".join(command[-2:]) if "mousemove" in command else "other")
            return "1920 1080"

        with mock.patch.object(desktop, "run", side_effect=run), mock.patch.object(desktop, "shot", side_effect=lambda name: calls.append("shot")), \
                mock.patch.object(suite.time, "sleep"), mock.patch.object(suite.subprocess, "check_output", return_value=bytes(clean)):
            desktop.shot_settings_tab("solver-readonly", 0)
        self.assertEqual(calls, ["geometry", "move 900 328", "shot"])
        with mock.patch.object(desktop, "park"), mock.patch.object(desktop, "shot"), \
                mock.patch.object(suite.subprocess, "check_output", return_value=bytes(hovered)):
            with self.assertRaisesRegex(RuntimeError, "requested Settings tab"):
                desktop.shot_settings_tab("solver-readonly", 0)

    def test_intro_is_seen_by_its_begin_button(self):
        desktop = object.__new__(suite.Desktop)
        desktop.output = self.output
        dark = bytearray(bytes((0x3e, 0x43, 0x57)) * 510)
        button = bytearray(bytes((0x87, 0x45, 0x66)) * 510)
        one_place = bytearray(dark); one_place[3 * 50:3 * 50 + 3] = bytes((0x87, 0x45, 0x66))
        for pixels, expected in ((button, True), (dark, False), (one_place, False)):
            with self.subTest(expected=expected, first=bytes(pixels[:3])), mock.patch.object(desktop, "shot"), \
                    mock.patch.object(suite.subprocess, "check_output", return_value=bytes(pixels)):
                self.assertEqual(desktop.intro_showing("check"), expected)
        with mock.patch.object(desktop, "shot"), mock.patch.object(suite.subprocess, "check_output", return_value=b""):
            with self.assertRaisesRegex(RuntimeError, "could not be sampled"):
                desktop.intro_showing("check")

    def test_wrong_resolution_aborts_before_clicking_game(self):
        for resolution, geometry in (("640x480", "1920 1080"),
                                     ("1920x1080\nApplying resolution 640x480", "1920 1080"),
                                     ("1920x1080", "640 480")):
            with self.subTest(resolution=resolution, geometry=geometry):
                desktop = self.Desktop(geometry=geometry)
                with self.assertRaises(RuntimeError):
                    self.run_clicks(desktop, resolution)
                self.assertEqual(desktop.clicks, [])

    def test_quit_is_observed_and_missed_first_attempt_is_retried(self):
        desktop = self.Desktop(quit_after=2)
        self.run_clicks(desktop)
        self.assertEqual(desktop.quit_clicks, 2)
        self.assertIn("settings-quit-attempt-1", desktop.shots)
        self.assertNotIn("settings-quit-attempt-2", desktop.shots)

    def test_missing_quit_fails_with_diagnostic_screenshots(self):
        desktop = self.Desktop()
        with self.assertRaisesRegex(TimeoutError, "Settings Quit to Desktop"):
            self.run_clicks(desktop)
        self.assertEqual(desktop.quit_clicks, 2)
        self.assertEqual(desktop.shots[-2:], ["settings-quit-attempt-1", "settings-quit-attempt-2"])


class GuardTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.root = Path(self.directory.name)

    def tearDown(self):
        self.directory.cleanup()

    def package(self):
        package = self.root / "package"
        (package / "native").mkdir(parents=True)
        (package / "a.dll").write_bytes(b"a")
        (package / "native/b.so").write_bytes(b"b")
        files = {n: hashlib.sha256((package / n).read_bytes()).hexdigest() for n in ("a.dll", "native/b.so")}
        write(package / "package-manifest.json", {"source_revision": REVISION, "source_dirty": False, "files": files})
        return package

    def test_package_must_match_revision_hashes_and_inventory(self):
        package = self.package()
        self.assertEqual(set(suite.verify_package(package, REVISION)["files"]), {"a.dll", "native/b.so"})
        with self.assertRaises(suite.Refused):
            suite.verify_package(package, "b" * 40)
        (package / "extra.txt").write_text("x")
        with self.assertRaises(suite.Refused):
            suite.verify_package(package, REVISION)
        (package / "extra.txt").unlink()
        (package / "a.dll").write_bytes(b"changed")
        with self.assertRaises(suite.Refused):
            suite.verify_package(package, REVISION)

    def test_frozen_save_is_copied_unchanged_and_never_reused(self):
        source = self.root / "base.sav"
        source.write_bytes(b"frozen")
        expected = hashlib.sha256(b"frozen").hexdigest()
        copy = suite.stage_frozen_save(source, self.root / "copy.sav", expected)
        self.assertEqual(copy.read_bytes(), b"frozen")
        with self.assertRaises(suite.Refused):
            suite.stage_frozen_save(source, copy, expected)
        with self.assertRaises(suite.Refused):
            suite.stage_frozen_save(source, self.root / "other.sav", "0" * 64)
        self.assertEqual(source.read_bytes(), b"frozen")

    def test_fresh_base_source_accepts_current_activity_flags_and_refuses_legacy_flags(self):
        output = self.root / "fresh"
        save = output / "colony.sav"
        save.parent.mkdir()
        save.write_bytes(save_bytes({"numberOfDuplicants": 3, "sandboxEnabled": False}))
        result = {"status": "passed-colony-owner", "exit_code": 0, "source_revision": REVISION,
                  "source_dirty": False, "owner_environment_overrides": False,
                  "colony_report": {"Duplicants": 3}, "host_saved_file": str(save),
                  "host_saved_file_sha256": suite.sha256(save),
                  "colony_owner": {"OwnerConfigFlags": 29, "PipeSave": {"AuditClosed": True},
                                   "Scenarios": [{"Passed": True}]}}
        write(output / "result.json", result)
        self.assertEqual(suite.base_source_save(output, REVISION), save)
        result["colony_owner"]["OwnerConfigFlags"] = 13
        write(output / "result.json", result)
        with self.assertRaises(suite.Refused):
            suite.base_source_save(output, REVISION)

    def test_linux_game_commands_carry_the_player_choice(self):
        infra = suite.Infra(Path("/linux"), Path("/windows"), Path("/proton"), Path("/mingw"), Path("/wine"), ":99")
        moono = suite.Infra(Path("/linux"), Path("/windows"), Path("/proton"), Path("/mingw"), Path("/wine"), ":99", Path("/moono/player"))
        step = next(s for s in suite.plan("release") if s.kind == "batch")
        self.assertNotIn("--moono", suite.linux_command(step, Path("/p"), Path("/o"), infra))
        for command in (suite.linux_command(step, Path("/p"), Path("/o"), moono),
                        suite.menu_other_mod_command(Path("/p"), Path("/o"), moono, Path("/mod")),
                        suite.resume_refused_command(Path("/p"), Path("/o"), moono, Path("/s.sav"), Path("/mod"))):
            self.assertEqual(command[command.index("--moono") + 1], "/moono/player")

    def test_a_scenario_with_another_mod_loads_it_first_or_says_how_to_get_it(self):
        step = next(s for s in suite.plan("release") if s.options.get("scenario") == "rephysicalized")
        command = suite.linux_command(step, Path("/p"), Path("/o"), CommandTests.infra)
        self.assertEqual(command[command.index("--extra-mod-first") + 1], str(suite.OTHER_MODS / "Rephysicalized"))
        self.assertEqual(command[command.index("--dlc-mask") + 1], "0")
        with mock.patch.object(suite, "OTHER_MODS", self.root / "absent"):
            with self.assertRaisesRegex(suite.Refused, "tools/workshop.py fetch-mod 3609531831"):
                suite.other_mod_folder("Rephysicalized")
        present = self.root / "mods" / "Rephysicalized"
        write(present / "mod.yaml", "title: 'Rephysicalized'\n")
        with mock.patch.object(suite, "OTHER_MODS", self.root / "mods"):
            self.assertEqual(suite.other_mod_folder("Rephysicalized"), present)

    def test_the_menu_with_another_mod_needs_the_mod_active_and_its_patches_named(self):
        output = self.root / "menu"
        write(output / "result.json", {"status": "passed-menu-active", "other_mod_patches": ["Db.Initialize <- Rephysicalized (Harmony: Rephysicalized)"]})
        checks, details = suite.menu_other_mod_checks(0, output)
        self.assertTrue(all(checks.values()), checks)
        self.assertEqual(len(details["other_mod_patches"]), 1)
        write(output / "result.json", {"status": "failed-menu-not-active", "other_mod_patches": []})
        checks, _ = suite.menu_other_mod_checks(2, output)
        self.assertEqual(checks, {"harness_pass": False, "mod_active": False, "other_mod_patches_named": False})

    def test_resume_refused_needs_the_changed_code_refusal_and_its_guard(self):
        # The stand-in removed its patches (MA_OTHER_MOD_REMOVES_PATCHES=1): another mod's patch alone is
        # accepted, so the step must see the "game's code differs" refusal, the guard and the refused save.
        output = self.root / "resume"
        save = output / "autosave.sav"
        save.parent.mkdir()
        save.write_bytes(save_bytes({"saveMajorVersion": 7, "saveMinorVersion": 39}, 764025))
        write(output / "result.json", {"status": "passed-resume-refused"})
        log = ("[Mixed Atmospheres] Not active on game build 764025 (the game's code differs from the supported build); nothing is patched. Details:\n"
               "[Mixed Atmospheres] Only the verified save-load guard is installed.\n"
               "[12:00:00.000] [1] [WARNING] [Mixed Atmospheres] Colony save refused before loading: This colony uses Mixed Atmospheres, "
               "which is not active because the game's code differs from the supported build 764025, so it was not loaded.\n")
        write(output / "Player.log", log)
        checks, details = suite.resume_refused_checks(0, output, save, suite.sha256(save))
        self.assertTrue(all(checks.values()), checks)
        self.assertEqual(details["input_save"]["save_version"], "7.39")
        # A mod that is active (another mod's patches are tolerated) never installs the guard.
        write(output / "Player.log", "[Mixed Atmospheres] Game build 764025 verified: 3010 managed dependencies match the game contract.\n")
        checks, _ = suite.resume_refused_checks(0, output, save, suite.sha256(save))
        self.assertFalse(checks["mod_off_game_code_changed"] or checks["guard_installed"] or checks["refusal_names_cause"])

    def test_prepared_autosave_requires_exactly_one_rollover_save(self):
        output = self.root / "prepared"
        write(output / "result.json", {"status": "passed-colony-owner-load", "exit_code": 0, "source_revision": REVISION,
                                        "source_dirty": False, "owner_environment_overrides": False,
                                        "game_build": suite.mod_identity.INSPECTED_ONI_BUILD,
                                        "colony_report": {"LongRun": True, "Duplicants": 3}})
        (output / "input-colony.sav").write_bytes(save_bytes({"isAutoSave": False, "numberOfDuplicants": 3}))
        with self.assertRaises(suite.Refused):
            suite.prepared_autosave(output, REVISION)
        auto = output / "pfx/auto_save/a.sav"
        auto.parent.mkdir(parents=True)
        auto.write_bytes(save_bytes({"isAutoSave": True, "numberOfCycles": 1, "numberOfDuplicants": 3}))
        self.assertEqual(suite.prepared_autosave(output, REVISION), auto.resolve())
        (output / "pfx/auto_save/b.sav").write_bytes(save_bytes({"isAutoSave": True, "numberOfCycles": 1, "numberOfDuplicants": 3}))
        with self.assertRaises(suite.Refused):
            suite.prepared_autosave(output, REVISION)

    def test_a_fresh_passing_build_reports_only_passing_checks(self):
        layout = suite.Layout(self.root, "quick-v1")
        infra = suite.Infra(Path("/linux"), Path("/windows"), Path("/proton"), Path("/mingw"), Path("/wine"), ":99")
        with mock.patch.object(suite, "run_build", return_value=True), mock.patch.object(suite, "finish_package"):
            checks, record = suite.build_linux(layout, REVISION, infra)
        self.assertTrue(checks and all(checks.values()), checks)
        self.assertIs(record["reused_verified_package"], False)
        with mock.patch.object(suite, "run_build", return_value=False):
            checks, _ = suite.build_linux(suite.Layout(self.root / "other", "quick-v1"), REVISION, infra)
        self.assertFalse(all(checks.values()))

    def test_existing_run_output_is_refused(self):
        layout = suite.Layout(self.root, "quick-v1")
        layout.runs.mkdir(parents=True)
        (layout.runs / "linux-b1").mkdir()
        run = suite.Suite.__new__(suite.Suite)
        run.layout, run.revision = layout, REVISION
        step = next(s for s in suite.plan("quick") if s.name == "linux-b1")
        with mock.patch.object(suite, "verify_source"), mock.patch.object(suite, "verify_package", return_value={"files": {}}):
            with self.assertRaises(suite.Refused):
                run.execute(step)


class CriteriaTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.root = Path(self.directory.name)

    def tearDown(self):
        self.directory.cleanup()

    def cycle_run(self, frames=3000, owner=None, games=None):
        self.runs = getattr(self, "runs", 0) + 1
        output = self.root / f"run{self.runs}"
        validation = output / suite.LINUX_PROFILE / suite.VALIDATION
        owner_report = {"State": 1, "Faulted": False, "Error": "", "RejectedAttempts": 56, "SkippedRelaxations": 0,
                        "MaximumRelativeMassError": 1e-15, "MaximumRelativeEnergyError": 1e-14,
                        "CompletedFrames": 33 + frames, "OwnerConfigFlags": 29,
                        "RejectionCriteria": {"Available": True, "PressureSolveFailures": 0, "UnconvergedPressure": 0,
                                              "NegativeOrNonFinitePressure": 0, "SweptVolumeRefinements": 56},
                        "RestoredOwner": {"PhysicalVersion": 32, "LiquidLaw": 1, "PressureIterationCap": 400,
                                          "HeatIterationCap": 800, "GasHeatMixing": 8.0}}
        owner_report.update(owner or {})
        write(validation / "colony-owner.json", owner_report)
        write(validation / "colony-validation.json", {"Duplicants": 3, "RestoredFrame": 33})
        games = games or [{"Started": True, "Cycle": 1, "Duplicants": 3, "DebugInvincible": False},
                          {"Started": True, "Cycle": 1 + frames // 3000, "Duplicants": 3, "DebugInvincible": False}]
        (validation / "development-telemetry.jsonl").write_text(
            "".join(json.dumps({"Sequence": i, "Game": g}) + "\n" for i, g in enumerate(games, 1)))
        autosave = output / "home/auto_save/x.sav"
        autosave.parent.mkdir(parents=True)
        autosave.write_bytes(b"save")
        windows = "".join(f"perf frames {a}-{a + 149} session 1 partial 0: x\n" for a in range(34, 34 + frames, 150))
        (output / "Player.log").write_text(f"Saved to [{autosave}]\n" + windows)
        package = self.root / "package"
        package.mkdir(exist_ok=True)
        (package / "a.dll").write_bytes(b"a")
        write(package / "package-manifest.json", {"files": {"a.dll": hashlib.sha256(b"a").hexdigest()}})
        manifest = json.loads((package / "package-manifest.json").read_text())
        source, copy = self.root / "source.sav", self.root / "copy.sav"
        source.write_bytes(b"frozen")
        copy.write_bytes(b"frozen")
        expected = hashlib.sha256(b"frozen").hexdigest()
        hashes = suite.package_hashes(package, manifest)
        return suite.cycle_checks(0, output, frames, hashes, package, manifest,
                                  suite.sha256(package / "package-manifest.json"), source, copy, expected)

    def test_complete_prepared_run_passes_every_driver_check(self):
        checks, details = self.cycle_run()
        self.assertTrue(all(checks.values()), [k for k, v in checks.items() if not v])
        self.assertEqual(details["game_cycle_delta"], 1)
        self.assertEqual(len(checks), 19)

    def test_ten_cycles_need_ten_game_cycles_and_thirty_thousand_frames(self):
        checks, _ = self.cycle_run(frames=30000)
        self.assertTrue(all(checks.values()))
        checks, _ = self.cycle_run(frames=30000, owner={"CompletedFrames": 33 + 29999})
        self.assertFalse(checks["new_frames"])

    def test_any_rejected_attempt_invincibility_or_death_fails(self):
        # Swept-volume refinements are recorded, not a check; any failure criterion fails (delegated decision 2026-11-03).
        checks, details = self.cycle_run()
        self.assertTrue(checks["zero_failed_pressure_attempts"])
        self.assertEqual(details["rejection_criteria"]["SweptVolumeRefinements"], 56)
        self.assertNotIn("zero_rejected_attempts", checks)
        for name in ("PressureSolveFailures", "UnconvergedPressure", "NegativeOrNonFinitePressure"):
            criteria = {"Available": True, "PressureSolveFailures": 0, "UnconvergedPressure": 0,
                        "NegativeOrNonFinitePressure": 0, "SweptVolumeRefinements": 3, name: 1}
            self.assertFalse(self.cycle_run(owner={"RejectionCriteria": criteria})[0]["zero_failed_pressure_attempts"], name)
        # A package that cannot report the criteria cannot prove the bound.
        self.assertFalse(self.cycle_run(owner={"RejectionCriteria": {"Available": False}})[0]["zero_failed_pressure_attempts"])
        self.assertFalse(self.cycle_run(owner={"RejectionCriteria": None})[0]["zero_failed_pressure_attempts"])
        self.assertFalse(self.cycle_run(owner={"MaximumRelativeEnergyError": 2e-6})[0]["ledgers_close_at_existing_native_bounds"])
        games = [{"Started": True, "Cycle": 1, "Duplicants": 3, "DebugInvincible": True},
                 {"Started": True, "Cycle": 2, "Duplicants": 2, "DebugInvincible": False}]
        checks, _ = self.cycle_run(games=games)
        self.assertFalse(checks["no_invincibility"])
        self.assertFalse(checks["all_samples_three_alive"])
        games = [{"Started": True, "Cycle": 1, "Duplicants": 3, "DebugInvincible": False}] * 2
        self.assertFalse(self.cycle_run(games=games)[0]["actual_game_cycles"])

    def test_linux_gate_is_the_harness_classification(self):
        output = self.root / "batch"
        write(output / "result.json", {"status": "failed-runtime-validation"})
        write(output / suite.LINUX_PROFILE / suite.VALIDATION / "colony-owner.json",
              {"Scenarios": [{"Name": "building-melt", "Passed": False,
                              "Checks": [{"Name": "metal", "Passed": False, "Detail": "92.96 kg"}]}]})
        checks, details = suite.linux_checks(2, output)
        self.assertEqual(checks, {"harness_pass": False})
        self.assertEqual(details["scenarios"][0]["failed_checks"], [{"Name": "metal", "Detail": "92.96 kg"}])
        self.assertEqual(suite.linux_checks(0, output)[0], {"harness_pass": True})

    def proton_result(self, **values):
        files = {"a.dll": "0" * 64}
        owner = {"State": 1, "Faulted": False, "Error": "", "RejectedAttempts": 12, "SkippedRelaxations": 0,
                 "MaximumRelativeMassError": 0.0, "MaximumRelativeEnergyError": 0.0, "OwnerConfigFlags": 29,
                 "RejectionCriteria": {"Available": True, "PressureSolveFailures": 0, "UnconvergedPressure": 0,
                                       "NegativeOrNonFinitePressure": 0, "SweptVolumeRefinements": 12},
                 # windows-b2's scenarios, in its order (six since natural-sublimation joined it).
                 "Scenarios": [{"Name": name, "Finished": True, "Passed": True} for name in suite.LINUX_BATCHES[1].split(",")]}
        result = {"exit_code": 0, "observed_player_exit_code": 0, "source_revision": REVISION, "source_dirty": False,
                  "runner_git_revision": REVISION, "runner_git_status": "", "mod_failure_lines": [], "timed_out": False,
                  "actual_windows_os": False, "target_platform": "WindowsPlayer", "package_manifest": {"files": files},
                  "status": "passed-colony-owner", "owner_environment_overrides": False, "colony_owner": owner}
        result.update(values)
        output = self.root / "proton"
        write(output / "result.json", result)
        return output, files

    def settings_result(self, stage="load", **overrides):
        step = next(s for s in suite.plan("quick") if s.name == "windows-settings-" + stage)
        values = dict(zip(suite.SETTINGS_KEYS, suite.SETTINGS_EXPECTED))
        owner = {"State": 1, "Faulted": False, "Error": "", "ColonySettings": {"Values": values}}
        result = {"status": "passed-colony-owner" if stage == "source" else "passed-colony-owner-load",
                  "colony_owner": owner, "inspection_seconds_requested": suite.SETTINGS_INSPECTION_SECONDS,
                  "inspection_seconds_held": 9.626, "stopped_early_for_mod_failure": False,
                  "observed_player_exit_code": 0}
        result.update(overrides)
        output, files = self.proton_result(**result)
        for name in suite.SETTINGS_SHOTS:
            (output / (name + ".png")).write_bytes(b"png")
        return step, output, files

    def test_windows_batch_checks_follow_the_summary_driver(self):
        step = next(s for s in suite.plan("quick") if s.name == "windows-b2")
        output, files = self.proton_result()
        checks, _ = suite.proton_checks(step, output, REVISION, files)
        self.assertTrue(all(checks.values()), checks)
        # A batch that skipped or reordered a scenario is refused.
        result = suite.read_json(output / "result.json")
        result["colony_owner"]["Scenarios"].pop()
        (output / "result.json").write_text(suite.json.dumps(result))
        self.assertFalse(suite.proton_checks(step, output, REVISION, files)[0]["requested_scenarios_ran"])
        output, files = self.proton_result(owner_environment_overrides=True, runner_git_status=" M x")
        checks, _ = suite.proton_checks(step, output, REVISION, files)
        self.assertFalse(checks["no_ma_environment_overrides"])
        self.assertFalse(checks["clean_exact_runner"])
        output, files = self.proton_result(package_manifest={"files": {}})
        self.assertFalse(suite.proton_checks(step, output, REVISION, files)[0]["package_is_bundle"])

    def test_windows_owner_bound_applies_to_failure_criteria_only(self):
        output, files = self.proton_result()
        result = suite.read_json(output / "result.json")
        checks = suite.proton_owner(result, "passed-colony-owner", REVISION, files)
        self.assertTrue(checks["zero_failed_pressure_attempts"])
        self.assertNotIn("zero_rejected_attempts", checks)
        self.assertEqual(suite.owner_details(result)["rejection_criteria"]["SweptVolumeRefinements"], 12)
        result["colony_owner"]["RejectionCriteria"]["UnconvergedPressure"] = 1
        self.assertFalse(suite.proton_owner(result, "passed-colony-owner", REVISION, files)["zero_failed_pressure_attempts"])

    def test_current_proton_owner_rejects_the_legacy_owner_flags(self):
        output, files = self.proton_result()
        result = suite.read_json(output / "result.json")
        self.assertTrue(suite.proton_owner(result, "passed-colony-owner", REVISION, files)["default_config_flags"])
        for flags in (13, 0, 31, None):
            with self.subTest(flags=flags):
                result["colony_owner"]["OwnerConfigFlags"] = flags
                self.assertFalse(suite.proton_owner(result, "passed-colony-owner", REVISION, files)["default_config_flags"])

    def test_settings_must_restore_the_explicit_preset(self):
        step, output, files = self.settings_result()
        self.assertTrue(all(suite.proton_checks(step, output, REVISION, files)[0].values()))
        result = suite.read_json(output / "result.json")
        result["colony_owner"]["ColonySettings"]["Values"]["solver.heatIterationCap"] = 800
        write(output / "result.json", result)
        self.assertFalse(suite.proton_checks(step, output, REVISION, files)[0]["settings_restored"])

    def test_settings_inspection_requires_clean_desktop_exit(self):
        failures = (("inspection_seconds_requested", 60, "inspection_requested"),
                    ("timed_out", True, "not_timed_out"),
                    ("stopped_early_for_mod_failure", True, "no_early_mod_failure"),
                    ("exit_code", 2, "controlled_runner_exit"),
                    ("observed_player_exit_code", 5, "normal_desktop_exit"))
        for stage in ("source", "load"):
            with self.subTest(stage=stage):
                step, output, files = self.settings_result(stage)
                baseline = suite.read_json(output / "result.json")
                checks, details = suite.proton_checks(step, output, REVISION, files)
                self.assertTrue(all(checks.values()), checks)
                self.assertEqual(details["inspection_seconds_held"], 9.626)
                for field, value, failed_check in failures:
                    with self.subTest(stage=stage, field=field):
                        modified = dict(baseline, **{field: value})
                        write(output / "result.json", modified)
                        self.assertFalse(suite.proton_checks(step, output, REVISION, files)[0][failed_check])
                write(output / "result.json", dict(baseline, inspection_seconds_held=120.134,
                                                    observed_player_exit_code=None))
                self.assertFalse(suite.proton_checks(step, output, REVISION, files)[0]["normal_desktop_exit"])
                write(output / "result.json", baseline)
                (output / "performance.png").unlink()
                self.assertFalse(suite.proton_checks(step, output, REVISION, files)[0]["screenshots"])

    def test_missing_result_never_passes(self):
        step = next(s for s in suite.plan("quick") if s.name == "windows-contact")
        checks, _ = suite.proton_checks(step, self.root / "absent", REVISION, {})
        self.assertFalse(all(checks.values()))


if __name__ == "__main__":
    unittest.main()
