"""Acceptance must inspect main without changing checkouts, saves or locks."""
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).parents[1]))
import acceptance_suite as suite

REVISION = "a" * 40


class MainCheckoutTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.local = self.root / "local"
        self.local.mkdir()
        (self.local / "sentinel").write_bytes(b"preserve saves and locks")
        self.root_patch = mock.patch.object(suite, "ROOT", self.root)
        self.root_patch.start()
        self.addCleanup(self.root_patch.stop)
        self.layout = suite.Layout(self.root / "artifacts", "test")

    def inspect(self, branch="main", revision=REVISION, dirty="", source=None):
        def git(*arguments, **kwargs):
            if arguments == ("branch", "--show-current"):
                return branch
            if arguments == ("rev-parse", "HEAD"):
                return revision
            if arguments == ("status", "--porcelain"):
                return dirty
            raise AssertionError(arguments)
        with mock.patch.object(suite, "common_root", return_value=self.root), \
                mock.patch.object(suite, "git", side_effect=git), \
                mock.patch.object(suite.subprocess, "run") as mutate:
            try:
                suite.prepare_source(source or self.layout, REVISION)
            finally:
                mutate.assert_not_called()
                self.assertFalse(self.local.is_symlink())
                self.assertEqual((self.local / "sentinel").read_bytes(), b"preserve saves and locks")

    def test_clean_main_is_used_without_worktree_or_local_mutation(self):
        self.assertEqual(self.layout.source, self.root)
        self.inspect()
        self.assertFalse(self.layout.base.exists())

    def test_wrong_branch_revision_or_dirty_main_refuses_without_mutation(self):
        for values in [
            {"branch": "feature"}, {"branch": ""},
            {"revision": "b" * 40}, {"dirty": " M native/src/mac_game_kernel.cpp"},
        ]:
            with self.subTest(values=values), self.assertRaises(suite.Refused):
                self.inspect(**values)

    def test_other_checkout_is_refused_even_with_same_revision_and_branch(self):
        other = self.root / "other-checkout"
        other.mkdir()
        with self.assertRaises(suite.Refused):
            self.inspect(source=SimpleNamespace(source=other))


if __name__ == "__main__":
    unittest.main()
