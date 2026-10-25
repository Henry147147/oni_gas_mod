#!/usr/bin/env python3
"""Wrap an immutable Windows mod package and its installer in a private ZIP."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, required=True, help="Output of package_probe.py with both Windows DLLs")
    parser.add_argument("--output", type=Path, required=True, help="Fresh .zip filename")
    args = parser.parse_args()
    package = args.package.resolve()
    manifest = json.loads((package / "package-manifest.json").read_text())
    files = manifest["files"]
    required = {"MixedAtmospheres.Oni.dll", "MixedAtmospheres.Core.dll",
                "native/win-x64/mixed_atmospheres.dll", "native/win-x64/mixed_atmospheres_oni.dll"}
    if not required.issubset(files) or manifest.get("native_observer_enabled") is not True:
        parser.error("Supply a Windows package with the native backend")
    payload = {}
    for name, digest in files.items():
        path = PurePosixPath(name)
        if path.is_absolute() or ".." in path.parts or "\\" in name:
            parser.error("Unsafe manifest path")
        source = package / name
        if source.is_symlink() or not source.resolve().is_relative_to(package):
            parser.error("Package file escapes its source directory")
        data = source.read_bytes()
        if hashlib.sha256(data).hexdigest() != digest:
            parser.error("Package hash mismatch: " + name)
        payload["MixedAtmospheres/" + name] = data
    if {str(path.relative_to(package)) for path in package.rglob("*") if path.is_file()} != set(files) | {"package-manifest.json"}:
        parser.error("Package has files outside its immutable manifest")
    payload["MixedAtmospheres/package-manifest.json"] = (package / "package-manifest.json").read_bytes()
    payload["install_windows.ps1"] = (ROOT / "tools/install_windows.ps1").read_bytes()
    payload["INSTALL.txt"] = (
        "Mixed Atmospheres: private Windows x64 playtest\n\n"
        "1. Close Oxygen Not Included and extract the entire ZIP.\n"
        "2. In the extracted folder run:\n"
        "   powershell -NoProfile -ExecutionPolicy Bypass -File .\\install_windows.ps1\n"
        "3. Start ONI, enable Mixed Atmospheres in Mods, and restart when asked.\n"
        "4. In the main menu, turn OFF Spaced Out! and any other gameplay packs (ONI restarts).\n"
        "5. Optional: Mods -> Mixed Atmospheres -> Settings. Default is recommended.\n"
        "   You can create, save and load your own presets; reinstalls retain them.\n"
        "6. Start a NEW base-game colony. No environment variables are needed.\n\n"
        "The installer preserves user configuration and saves, and backs up old mod copies.\n"
        "A custom user-data folder can be selected with -OniUserRoot.\n"
        "Mixture saves need this mod to load; existing vanilla saves keep their saved simulation.\n"
        "After playing, send Player.log from:\n"
        "%USERPROFILE%\\AppData\\LocalLow\\Klei\\Oxygen Not Included\\Player.log\n"
    ).encode("utf-8")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output, "x", zipfile.ZIP_DEFLATED) as archive:
        for name, data in sorted(payload.items()):
            info = zipfile.ZipInfo(name, (2026, 10, 25, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, data)
    with zipfile.ZipFile(args.output) as archive:
        if archive.testzip() is not None or any(archive.read(name) != data for name, data in payload.items()):
            raise ValueError("ZIP verification failed")
    print(hashlib.sha256(args.output.read_bytes()).hexdigest(), args.output)


if __name__ == "__main__":
    main()
