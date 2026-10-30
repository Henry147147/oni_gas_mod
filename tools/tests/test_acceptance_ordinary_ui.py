"""No-game regressions for delayed ordinary start and actual manual saves."""
import sys
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).parents[1]))
import acceptance_suite as suite

READONLY = "Settings window opened (Advanced, colony settings read-only)"


class OrdinaryColony:
    def __init__(self, begin_after=3, manual_save=True):
        # Historical messages must not qualify the next colony's readiness.
        self.messages = ["-- MAIN MENU --", "Colony owner started:",
                         "Saved to previous-colony.sav", READONLY]
        self.begin_after = begin_after
        self.manual_save = manual_save
        self.embarked = False
        self.begin_clicks = 0
        self.settings_open = False
        self.running = False
        self.saved_manually = False
        self.quit = False
        self.shots = {}

    def log(self):
        return "\n".join(self.messages)

    def click(self, x, y):
        if (x, y) == (903, 624):
            self.embarked = True
        elif (x, y) == (637, 449) and self.embarked:
            self.begin_clicks += 1
            if self.begin_after is not None and self.begin_clicks >= self.begin_after:
                if not self.settings_open and not self.running:
                    self.settings_open = True
                    self.messages.append(READONLY)
        elif (x, y) == (934, 128) and self.settings_open:
            self.settings_open = False
            self.running = True
            self.messages.append("Saved to initial.sav")
        elif (x, y) == (641, 389) and self.running and self.manual_save:
            self.saved_manually = True
            self.messages.append("Saved to manual.sav")
        elif (x, y) == (641, 406):
            self.quit = True

    def key(self, key):
        pass

    def shot(self, name):
        self.shots[name] = (self.running, self.saved_manually)

    @staticmethod
    def wait(predicate, timeout=60):
        for _ in range(12):
            if predicate():
                return
        raise TimeoutError("readiness or save never observed")


class OrdinaryUiTests(unittest.TestCase):
    def run_ui(self, colony):
        with mock.patch.object(suite.time, "sleep"):
            suite.ordinary_clicks(colony, colony.log, colony.wait)

    def test_delayed_begin_requires_new_initial_save_and_manual_save(self):
        colony = OrdinaryColony()
        self.run_ui(colony)
        self.assertTrue(colony.running)
        self.assertTrue(colony.saved_manually)
        self.assertTrue(colony.quit)
        self.assertEqual(colony.shots["ordinary-colony-owner-active"], (True, False))
        self.assertEqual(colony.shots["ordinary-saved"], (True, True))

    def test_begin_timeout_refuses_before_menu_save_or_quit(self):
        colony = OrdinaryColony(begin_after=None)
        with self.assertRaises(TimeoutError):
            self.run_ui(colony)
        self.assertFalse(colony.saved_manually)
        self.assertFalse(colony.quit)
        self.assertNotIn("ordinary-colony-owner-active", colony.shots)

    def test_manual_save_timeout_cannot_be_reported_as_saved(self):
        colony = OrdinaryColony(begin_after=1, manual_save=False)
        with self.assertRaises(TimeoutError):
            self.run_ui(colony)
        self.assertTrue(colony.running)
        self.assertFalse(colony.saved_manually)
        self.assertFalse(colony.quit)
        self.assertNotIn("ordinary-saved", colony.shots)


if __name__ == "__main__":
    unittest.main()
