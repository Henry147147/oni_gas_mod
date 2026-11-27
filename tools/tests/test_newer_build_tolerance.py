"""A game newer than the build a package was checked against, with real game
copies (docs/game-update-resilience.md#tolerated-newer-builds).

The current mod source is compiled against build 763796 only, as if 763796
were the newest supported build, and its contract is written against that
game. Checked as the package root checks a newer game:

- 764025 changed nothing the mod uses apart from the build number: accepted,
  with exactly the seven build-number entries forgiven;
- 764961 changed SelectToolHoverTextCard.UpdateHoverElements: refused, naming it.

Each control is skipped with a plain message when a game copy is absent.
Building the adapter and the contracts takes a minute or two.
"""
from __future__ import annotations

import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SHARE = Path("/mnt/data/share/oni")
# Each kept build in its -<build> folders; the plain folders hold the newest build (764961 now).
COPIES = {
    763796: {"linux-x64": SHARE / "OxygenNotIncluded-linux-763796", "windows-x64": SHARE / "OxygenNotIncluded-763796"},
    764025: {"linux-x64": SHARE / "OxygenNotIncluded-linux-764025", "windows-x64": SHARE / "OxygenNotIncluded-764025"},
}
FORGIVEN = [
    "static LoadScreen::DoLoad(System.String)System.Void",
    "named Game::Save",
    "named KleiVersion::ChangeList",
    "named LoadScreen::DoLoad",
    "patch target Game::Save(System.IO.BinaryWriter)System.Void",
    "patch target static LoadScreen::DoLoad(System.String)System.Void",
    "patch target SaveGame::GetSaveHeader(System.Boolean,System.Boolean,SaveGame/Header&)System.Byte[]",
]


def build_of(root: Path) -> int | None:
    """0 for an installation folder, None when it is absent (the build is read by the contract tool)."""
    return None if not (root / "OxygenNotIncluded_Data/Managed/Assembly-CSharp.dll").is_file() else 0


def contract_tool(*arguments: str) -> subprocess.CompletedProcess:
    return subprocess.run(["dotnet", "run", "--project", str(ROOT / "tools/GameContract"), "-c", "Release", "-p:UseSharedCompilation=false", "--", *arguments],
                          cwd=ROOT, capture_output=True, text=True)


@unittest.skipUnless(os.environ.get("MA_SKIP_GAME_COPIES") != "1", "MA_SKIP_GAME_COPIES=1")
class NewerBuildTolerance(unittest.TestCase):
    folder: tempfile.TemporaryDirectory
    mod: Path

    @classmethod
    def setUpClass(cls):
        reference = COPIES[763796]["linux-x64"]
        if build_of(reference) is None:
            raise unittest.SkipTest(f"the 763796 Linux game copy is not on this machine ({reference})")
        cls.folder = tempfile.TemporaryDirectory(prefix="ma-newer-build-")
        artifacts = Path(cls.folder.name) / "managed"
        env = dict(os.environ, MSBUILDDISABLENODEREUSE="1", DOTNET_CLI_TELEMETRY_OPTOUT="1")
        subprocess.run(["dotnet", "build", str(ROOT / "managed/MixedAtmospheres.Oni"), "-c", "Release", "--nologo", "-v:q", "-p:UseSharedCompilation=false",
                        "-p:DebugType=None", f"-p:OniGameDir={reference}", "--artifacts-path", str(artifacts)], check=True, env=env, capture_output=True)
        cls.mod = artifacts / "bin/MixedAtmospheres.Oni/release"
        games = [f"{platform}={root}" for platform, root in COPIES[763796].items() if build_of(root) is not None]
        result = contract_tool("generate", "--mod", str(cls.mod), *sum((["--game", g] for g in games), []), "--output", str(cls.mod / "game-contract.json"))
        if result.returncode != 0:
            raise AssertionError("contract generation failed:\n" + result.stdout + result.stderr)

    @classmethod
    def tearDownClass(cls):
        cls.folder.cleanup()

    def verify(self, platform: str, build: int, roots: list[Path]) -> list[str]:
        """The contract tool's lines for the first copy of `build` among `roots`."""
        for root in roots:
            if build_of(root) is None:
                continue
            lines = self.run_verify(platform, root)
            if any(line.startswith(f"{platform}: build {build},") for line in lines):
                return lines
        self.skipTest(f"no {platform} copy of build {build} on this machine (looked in {', '.join(map(str, roots))})")

    def run_verify(self, platform: str, root: Path) -> list[str]:
        contract = self.mod / "game-contract.json"
        if f'"{platform}"' not in contract.read_text():
            self.skipTest(f"no {platform} copy of 763796 to write its contract")
        result = contract_tool("verify", "--mod", str(self.mod), "--game", f"{platform}={root}", "--contract", str(contract))
        self.assertIn(result.returncode, (0, 1), result.stdout + result.stderr)
        return result.stdout.splitlines()

    def check_accepted(self, platform: str, build: int, roots: list[Path]):
        lines = self.verify(platform, build, roots)
        self.assertIn(f"{platform}: accepted as a newer build", lines, "\n".join(lines))
        self.assertEqual([line.strip()[len("forgiven "):] for line in lines if line.strip().startswith("forgiven ")], FORGIVEN)
        self.assertTrue(any(line.endswith(", 0 differences") for line in lines), "\n".join(lines))

    def check_refused(self, platform: str, build: int, roots: list[Path]):
        lines = self.verify(platform, build, roots)
        self.assertIn(f"{platform}: refused, an update of the mod is needed: SelectToolHoverTextCard.UpdateHoverElements", lines, "\n".join(lines))
        self.assertIn("changed patch target SelectToolHoverTextCard::UpdateHoverElements(System.Collections.Generic.List`1<KSelectable>)System.Void",
                      [line.strip() for line in lines])
        self.assertEqual(len([line for line in lines if line.strip().startswith("forgiven ")]), len(FORGIVEN))

    def test_764025_linux_is_accepted(self):
        self.check_accepted("linux-x64", 764025, [COPIES[764025]["linux-x64"]])

    def test_764025_windows_is_accepted(self):
        self.check_accepted("windows-x64", 764025, [COPIES[764025]["windows-x64"]])

    def test_764961_linux_is_refused_naming_the_change(self):
        self.check_refused("linux-x64", 764961, candidates("linux-x64", 764961))

    def test_764961_windows_is_refused_naming_the_change(self):
        self.check_refused("windows-x64", 764961, candidates("windows-x64", 764961))


def candidates(platform: str, build: int) -> list[Path]:
    """Where a build's copy is kept: its own -<build> folder, or the plain folder while it is the newest."""
    plain = SHARE / ("OxygenNotIncluded-linux" if platform == "linux-x64" else "OxygenNotIncluded")
    return [plain.with_name(f"{plain.name}-{build}"), plain]


if __name__ == "__main__":
    unittest.main()
