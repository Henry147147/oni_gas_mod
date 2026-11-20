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

ROOT = Path(__file__).resolve().parents[1]
STATIC_ID = "MixedAtmospheres"
TITLE = "Mixed Atmospheres"
DEV_FOLDER = "MixedAtmospheres.IntegrationProbe"
# The game build the managed contract and the packages are written against,
# which is also the only build a package accepts: the game compiles its build
# number into the save and load methods the mod patches, so the contract of
# one build never verifies on another. Retargeting: docs/game-update-resilience.md.
INSPECTED_ONI_BUILD = 764025
MINIMUM_SUPPORTED_BUILD = INSPECTED_ONI_BUILD
# Builds whose SimDLL images have a verified native profile. The Linux image
# of 763796 and 764025 is byte for byte that of 744825, so its profile reports
# 744825.
SUPPORTED_NATIVE_BUILDS = (744825, 763796, 764025)
API_VERSION = 2
PROJECT = ROOT / "managed/MixedAtmospheres.Oni/MixedAtmospheres.Oni.csproj"


def version(project: Path = PROJECT) -> str:
    """The one version: the managed project's <Version>, which the DLL carries."""
    text = project.read_text(encoding="utf-8")
    found = re.findall(r"<Version>([^<]*)</Version>", text)
    if len(found) != 1 or not re.fullmatch(r"\d+\.\d+\.\d+", found[0]):
        raise ValueError(f"{project} must declare exactly one <Version>MAJOR.MINOR.PATCH</Version>")
    informational = re.findall(r"<InformationalVersion>([^<]*)</InformationalVersion>", text)
    if informational != ["$(Version)"]:
        raise ValueError(f"{project} must set <InformationalVersion>$(Version)</InformationalVersion>")
    return found[0]


def mod_info(version_text: str) -> str:
    return (f"minimumSupportedBuild: {MINIMUM_SUPPORTED_BUILD}\n"
            f'version: "{version_text}"\nAPIVersion: {API_VERSION}\n')


def mod_yaml(description: str) -> str:
    if '"' in description or "\n" in description:
        raise ValueError("Keep the mod.yaml description on one line without double quotes")
    return f'title: "{TITLE}"\ndescription: "{description}"\nstaticID: "{STATIC_ID}"\n'
