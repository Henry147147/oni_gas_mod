"""Mixed Atmospheres' identity, shared by every package (docs/distribution.md).

ONI matches saves and mods.json entries by label (folder name or Workshop ID
plus platform), never by staticID; settings and presets live in
mods/config/MixedAtmospheres. The final staticID therefore changes no player
configuration, preset or save. The private installer and the harnesses keep
their Dev folder name, so an existing mods.json entry stays enabled.
"""
from __future__ import annotations

from pathlib import Path
import re
import hashlib
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
STATIC_ID = "MixedAtmospheres"
TITLE = "Mixed Atmospheres"
DEV_FOLDER = "MixedAtmospheres.IntegrationProbe"
# The Steam Workshop item (docs/workshop-upload.md). Klei's uploader, a Steam tool of its own,
# created it as one ZIP in that tool's Steam Cloud; ONI is the app that consumes it.
ONI_APP = 457140
WORKSHOP_ITEM = 3842047795
WORKSHOP_UPLOADER_APP = 636750
WORKSHOP_FILE = "mod_publish_data_file.zip"
# Shared with MSBuild so the packaged builds and their API variants agree.
GAME_BUILDS = {int(node.attrib["Include"]): node.attrib
               for node in ET.parse(ROOT / "managed/OniBuilds.targets").findall("./ItemGroup/OniRelease")}
INSPECTED_ONI_BUILD = max(GAME_BUILDS)
MINIMUM_SUPPORTED_BUILD = min(GAME_BUILDS)
SUPPORTED_NATIVE_BUILDS = tuple(sorted(GAME_BUILDS))


def reference_installation(current: Path, build: int) -> Path:
    return current if build == INSPECTED_ONI_BUILD else current.with_name(f"{current.name}-{build}")


def installed_build(game: Path, platform: str) -> int:
    managed = game / "OxygenNotIncluded_Data/Managed/Assembly-CSharp.dll"
    key = {"linux-x64": "LinuxManaged", "windows-x64": "WindowsManaged"}[platform]
    digest = hashlib.sha256(managed.read_bytes()).hexdigest()
    for build, files in GAME_BUILDS.items():
        if files[key] == digest:
            return build
    raise ValueError(f"Unregistered {platform} game assembly in {game}")


API_VERSION = 2
PROJECT = ROOT / "managed/MixedAtmospheres.Oni/MixedAtmospheres.Oni.csproj"


def version(project: Path = PROJECT) -> str:
    """The one version: the managed project's <Version>, which the DLL carries."""
    text = project.read_text(encoding="utf-8")
    found = re.findall(r"<Version>([^<]*)</Version>", text)
    if len(found) != 1 or not re.fullmatch(r"\d+\.\d+\.\d+(?:\.\d+)?", found[0]):
        raise ValueError(f"{project} must declare one numeric MAJOR.MINOR.PATCH version with an optional hotfix revision")
    informational = re.findall(r"<InformationalVersion>([^<]*)</InformationalVersion>", text)
    if informational != ["$(Version)"]:
        raise ValueError(f"{project} must set <InformationalVersion>$(Version)</InformationalVersion>")
    return found[0]


def mod_info(version_text: str, build: int = INSPECTED_ONI_BUILD) -> str:
    return (f"minimumSupportedBuild: {build}\n"
            f'version: "{version_text}"\nAPIVersion: {API_VERSION}\n')


def mod_yaml(description: str) -> str:
    if '"' in description or "\n" in description:
        raise ValueError("Keep the mod.yaml description on one line without double quotes")
    return f'title: "{TITLE}"\ndescription: "{description}"\nstaticID: "{STATIC_ID}"\n'
