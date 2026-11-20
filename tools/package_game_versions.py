"""Build the contents ONI selects from archived_versions for older installations."""
from pathlib import Path
import json
import shutil
import subprocess
import tempfile

import game_contract
import mod_identity

MANAGED = ("MixedAtmospheres.Oni.dll", "MixedAtmospheres.Core.dll")


def compile_adapter(game: Path, artifacts: Path) -> Path:
    # Isolate reference assemblies and intermediate outputs even when two
    # packagers run concurrently. Omit debug paths from the distributable DLL.
    subprocess.run(["dotnet", "build", str(mod_identity.PROJECT), "-c", "Release", "--nologo", "-v:q",
                    "-p:DebugType=None", "-p:OniGameDir=" + str(game),
                    "--artifacts-path", str(artifacts)], check=True)
    return artifacts / "bin/MixedAtmospheres.Oni/release"


def write_contract(managed: Path, games: dict[str, Path], destination: Path, build: int) -> None:
    for platform, game in games.items():
        if mod_identity.installed_build(game, platform) != build:
            raise ValueError(f"Expected {platform} build {build} at {game}")
    contract = destination / game_contract.FILE_NAME
    game_contract.generate(managed, games, contract)
    platforms = json.loads(contract.read_text())["Platforms"]
    if set(platforms) != set(games) or any(p["GameBuild"] != build for p in platforms.values()):
        raise ValueError(f"Generated contract does not describe build {build} on every requested platform")


def write_archives(output: Path, games: dict[str, Path], version: str, compare_managed=None) -> None:
    """Each archive is a complete loadable mod content folder. Notices are shared at the root."""
    for build in sorted(mod_identity.GAME_BUILDS):
        if build == mod_identity.INSPECTED_ONI_BUILD:
            continue
        archive = output / "archived_versions" / str(build)
        archive.mkdir(parents=True)
        roots = {platform: mod_identity.reference_installation(root, build) for platform, root in games.items()}
        with tempfile.TemporaryDirectory(prefix="ma-oni-managed-") as temporary:
            managed = {platform: compile_adapter(game, Path(temporary) / platform) for platform, game in roots.items()}
            if compare_managed is not None:
                compare_managed(managed["windows-x64"], managed["linux-x64"])
            adapter = managed.get("linux-x64") or managed["windows-x64"]
            for name in MANAGED:
                shutil.copyfile(adapter / name, archive / name)
            write_contract(archive, roots, archive, build)
        shutil.copytree(output / "native", archive / "native")
        for name in ("development.json", "native-backend-observation.json"):
            if (output / name).is_file():
                shutil.copyfile(output / name, archive / name)
        if (output / "private-validation").is_dir():
            shutil.copytree(output / "private-validation", archive / "private-validation")
        (archive / "mod_info.yaml").write_text(mod_identity.mod_info(version, build), encoding="utf-8")
