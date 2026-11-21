"""Actual named settings views must be selected before capturing them."""
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock
sys.path.insert(0, str(Path(__file__).parents[1]))
import acceptance_suite as suite


class SettingsUiTests(unittest.TestCase):
    def test_release_capture_visits_each_required_settings_view(self):
        selected = "solver"
        views = {}
        keys = []
        clicks = []
        def click(x, y):
            nonlocal selected
            clicks.append((x, y))
            selected = {(550, 356): "breathing", (638, 356): "performance"}.get((x, y), selected)
        def key(value):
            keys.append(value)
        def shot(name):
            views[name] = selected
        def shot_settings_tab(name, index):
            self.assertEqual(selected, ("solver", "breathing", "performance")[index])
            shot(name)
        def wait(predicate, *args):
            self.assertTrue(predicate())
        def run(*command):
            return "1920 1080" if "getdisplaygeometry" in command else "7"
        desktop = SimpleNamespace(bin=Path("/desktop/bin"), run=mock.Mock(side_effect=run),
                                  click=click, key=key, shot=shot, shot_settings_tab=shot_settings_tab)
        def log():
            events = "Applying resolution 1920x1080\nSettings window opened (Advanced)\nSettings window opened (Advanced, this colony's settings)"
            return events + ("\nGame.OnApplicationQuit()" if (641, 406) in clicks else "")
        with mock.patch.object(suite.time, "sleep"), mock.patch.object(suite, "read_json", return_value={"Status": "passed"}):
            suite.settings_clicks(desktop, Path("/run"), log, wait)
        self.assertEqual({name: views.get(name) for name in suite.SETTINGS_SHOTS}, {
            "solver-readonly": "solver", "breathing-readonly": "breathing", "performance": "performance"})
        self.assertEqual(keys, ["Escape"])
        self.assertEqual(clicks[-3:], [(1255, 40), (641, 487), (641, 406)])
        self.assertNotIn("settings-quit-attempt-1", views)


if __name__ == "__main__":
    unittest.main()
