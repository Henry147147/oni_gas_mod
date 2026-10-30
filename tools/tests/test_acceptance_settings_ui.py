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
        def click(x, y):
            nonlocal selected
            selected = {(507, 280): "breathing", (637, 280): "performance"}.get((x, y), selected)
        def key(value):
            keys.append(value)
        def shot(name):
            views[name] = selected
        def wait(predicate, *args):
            self.assertTrue(predicate())
        desktop = SimpleNamespace(bin=Path("/desktop/bin"), run=mock.Mock(return_value="7"),
                                  click=click, key=key, shot=shot)
        log = lambda: "Settings window opened (Advanced)\nSettings window opened (Advanced, colony settings read-only)"
        with mock.patch.object(suite.time, "sleep"), mock.patch.object(suite, "read_json", return_value={"Status": "passed"}):
            suite.settings_clicks(desktop, Path("/run"), log, wait)
        self.assertEqual({name: views.get(name) for name in suite.SETTINGS_SHOTS}, {
            "solver-readonly": "solver", "breathing-readonly": "breathing", "performance": "performance"})
        self.assertEqual(keys, ["Escape"])


if __name__ == "__main__":
    unittest.main()
