#!/usr/bin/env python3
"""Fetch a Moono release for the game harness (tools/ONI_HEADLESS.md#moono).

    python3 tools/moono.py fetch [--tag 2026.11.26.02] [--windows] [--dest DIR]

Downloads the release's linux-x64 archive (and with --windows the windows-x64 one) and its
SHA256SUMS from github.com/tigin-backwards/oni-moono's releases into local/moono/release of the
main checkout, checks each archive against the digest GitHub lists for it, unpacks the Linux
player into DIR/linux (refusing any entry outside its oni-moono folder, links, devices) and checks
every unpacked file against SHA256SUMS: none missing, none extra, every hash equal. A Windows
archive is checked the same way member by member and left packed. Prints the release, its commit
and the player folder. Requests carry a generic User-Agent and nothing about the user.
Moono is "All rights reserved": the release stays in local/, never in git.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
REPOSITORY = "tigin-backwards/oni-moono"
API = "https://api.github.com/repos/" + REPOSITORY + "/releases"
USER_AGENT = "oni-mod-test-harness"
FOLDER = "oni-moono"
PLATFORM_ARCHIVES = {"linux-x64": ".tar.gz", "windows-x64": ".zip"}


def main_checkout() -> Path:
    try:
        common = subprocess.check_output(["git", "rev-parse", "--path-format=absolute", "--git-common-dir"], cwd=ROOT,
                                         text=True, stderr=subprocess.DEVNULL).strip()
        return Path(common).parent
    except (OSError, subprocess.CalledProcessError):
        return ROOT


DEFAULT_DEST = main_checkout() / "local/moono/release"


def request(url: str, accept: str = "application/octet-stream") -> bytes:
    with urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": USER_AGENT, "Accept": accept}), timeout=120) as response:
        return response.read()


def release_info(tag: str | None) -> dict:
    """The release's API record: the latest, or the one whose tag is TAG (with or without its v)."""
    if tag is None:
        return json.loads(request(API + "/latest", "application/vnd.github+json"))
    if not re.fullmatch(r"v?[0-9][0-9A-Za-z.\-]*", tag):
        raise ValueError("unsafe release tag " + repr(tag))
    return json.loads(request(API + "/tags/" + (tag if tag.startswith("v") else "v" + tag), "application/vnd.github+json"))


def version_of(release: dict) -> str:
    tag = release.get("tag_name") or ""
    if not re.fullmatch(r"v?[0-9][0-9A-Za-z.\-]*", tag):
        raise ValueError("unexpected release tag " + repr(tag))
    return tag[1:] if tag.startswith("v") else tag


def assets(release: dict, platform: str) -> tuple[dict, dict]:
    """The archive and SHA256SUMS assets of one platform."""
    version = version_of(release)
    by_name = {asset.get("name"): asset for asset in release.get("assets") or []}
    archive = f"oni-moono-{version}-{platform}{PLATFORM_ARCHIVES[platform]}"
    sums = f"oni-moono-{version}-{platform}.SHA256SUMS"
    missing = [name for name in (archive, sums) if name not in by_name]
    if missing:
        raise ValueError(f"release {release.get('tag_name')} has no {', '.join(missing)}")
    return by_name[archive], by_name[sums]


def digest(asset: dict) -> str | None:
    """The SHA-256 GitHub lists for an asset ("sha256:<hex>"), or None for a release that predates digests."""
    value = asset.get("digest") or ""
    return value[len("sha256:"):].lower() if re.fullmatch(r"sha256:[0-9a-fA-F]{64}", value) else None


def download(asset: dict, destination: Path) -> Path:
    """The asset's file in `destination`; reused when it is there with the listed digest."""
    name = asset["name"]
    if not re.fullmatch(r"[A-Za-z0-9._-]+", name):
        raise ValueError("unsafe asset name " + repr(name))
    path = destination / name
    expected = digest(asset)
    if path.is_file() and expected and hashlib.sha256(path.read_bytes()).hexdigest() == expected:
        return path
    url = asset.get("browser_download_url") or ""
    if not url.startswith(f"https://github.com/{REPOSITORY}/releases/download/"):
        raise ValueError("unexpected download URL " + repr(url))
    data = request(url)
    if expected and hashlib.sha256(data).hexdigest() != expected:
        raise ValueError(f"{name} differs from the digest GitHub lists for it")
    if asset.get("size") is not None and len(data) != asset["size"]:
        raise ValueError(f"{name} has {len(data)} bytes, the release lists {asset['size']}")
    partial = path.with_name(path.name + ".part")
    partial.write_bytes(data)
    partial.replace(path)
    return path


def parse_sums(text: str) -> dict[str, str]:
    """SHA256SUMS: '<hex>  <path>' per file, paths relative to the archive's oni-moono folder."""
    sums = {}
    for number, line in enumerate(text.splitlines(), 1):
        if not line.strip():
            continue
        match = re.fullmatch(r"([0-9a-f]{64})  (\S.*)", line)
        if not match:
            raise ValueError(f"SHA256SUMS line {number} is not '<sha256>  <path>'")
        sums[safe_member(match.group(2), prefixed=False)] = match.group(1)
    if not sums:
        raise ValueError("SHA256SUMS lists no files")
    return sums


def safe_member(name: str, prefixed: bool = True) -> str:
    """An archive member's path below the oni-moono folder ('' for the folder itself), or ValueError."""
    path = PurePosixPath(name.replace("\\", "/"))
    if path.is_absolute() or any(part in ("..", "") for part in path.parts) or ":" in name or name.startswith("/"):
        raise ValueError("archive entry escapes its folder: " + repr(name))
    parts = path.parts
    if prefixed:
        if not parts or parts[0] != FOLDER:
            raise ValueError(f"archive entry outside {FOLDER}/: {name!r}")
        parts = parts[1:]
    return "/".join(parts)


def unpack_linux(archive: Path, sums: dict[str, str], target: Path) -> Path:
    """Unpacks the Linux archive to `target`/oni-moono, checked against `sums`; replaces an earlier copy."""
    target.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=".unpack-", dir=target))
    try:
        seen: dict[str, str] = {}
        with tarfile.open(archive, "r:gz") as tar:
            for member in tar:
                relative = safe_member(member.name)
                if member.isdir():
                    continue
                if not member.isfile():
                    raise ValueError(f"archive entry is not a plain file: {member.name!r}")
                if relative in seen:
                    raise ValueError("archive lists a file twice: " + relative)
                source = tar.extractfile(member)
                data = source.read() if source else b""
                seen[relative] = hashlib.sha256(data).hexdigest()
                path = staging / FOLDER / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
                os.chmod(path, 0o755 if member.mode & 0o111 else 0o644)
        check_sums(seen, sums)
        final = target / FOLDER
        old = target / (".old-" + FOLDER)
        if old.exists():
            shutil.rmtree(old)
        if final.exists():
            final.rename(old)
        (staging / FOLDER).rename(final)
        if old.exists():
            shutil.rmtree(old)
        return final
    finally:
        shutil.rmtree(staging, ignore_errors=True)


def check_zip(archive: Path, sums: dict[str, str]) -> None:
    seen = {}
    with zipfile.ZipFile(archive) as packed:
        for info in packed.infolist():
            relative = safe_member(info.filename)
            if info.is_dir():
                continue
            if relative in seen:
                raise ValueError("archive lists a file twice: " + relative)
            seen[relative] = hashlib.sha256(packed.read(info)).hexdigest()
    check_sums(seen, sums)


def check_sums(seen: dict[str, str], sums: dict[str, str]) -> None:
    missing = sorted(set(sums) - set(seen))
    extra = sorted(set(seen) - set(sums))
    wrong = sorted(name for name in set(seen) & set(sums) if seen[name] != sums[name])
    if missing or extra or wrong:
        raise ValueError("the archive differs from its SHA256SUMS: " + "; ".join(
            part for part in (f"missing {missing[:5]}" if missing else "", f"extra {extra[:5]}" if extra else "",
                              f"changed {wrong[:5]}" if wrong else "") if part))


def player_release(player: Path) -> str | None:
    sys.path.insert(0, str(ROOT / "tools"))
    import oni_headless
    return oni_headless.moono_release(player)


def fetch(tag: str | None, windows: bool, dest: Path) -> dict:
    release = release_info(tag)
    version = version_of(release)
    dest.mkdir(parents=True, exist_ok=True)
    (dest / f"release-{version}.json").write_text(json.dumps(release, indent=2) + "\n")
    facts = {"release": release.get("tag_name"), "commit": release.get("target_commitish"), "published": release.get("published_at")}
    for platform in ("linux-x64", *(("windows-x64",) if windows else ())):
        archive_asset, sums_asset = assets(release, platform)
        unlisted = [asset["name"] for asset in (archive_asset, sums_asset) if digest(asset) is None]
        archive, sums_file = download(archive_asset, dest), download(sums_asset, dest)
        sums = parse_sums(sums_file.read_text())
        if platform == "linux-x64":
            player = unpack_linux(archive, sums, dest / "linux") / "player"
            facts["player"] = str(player)
            facts["player_release"] = player_release(player)
        else:
            check_zip(archive, sums)
            facts["windows_archive"] = str(archive)
        facts.setdefault("unlisted_digests", []).extend(unlisted)
    return facts


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    get = sub.add_parser("fetch", help="download, verify and unpack a Moono release")
    get.add_argument("--tag", help="release tag such as 2026.11.26.02 (default: the latest release)")
    get.add_argument("--windows", action="store_true", help="also fetch and verify the windows-x64 archive (left packed)")
    get.add_argument("--dest", type=Path, default=DEFAULT_DEST, help=f"release folder (default {DEFAULT_DEST})")
    args = parser.parse_args(argv)
    try:
        facts = fetch(args.tag, args.windows, args.dest.resolve())
    except (ValueError, OSError, tarfile.TarError, zipfile.BadZipFile) as error:
        print("moono fetch: " + str(error), file=sys.stderr)
        return 1
    for name in facts.get("unlisted_digests", []):
        print(f"note: GitHub lists no digest for {name}; its contents were checked against SHA256SUMS only", file=sys.stderr)
    print(f"Moono {facts['release']} (commit {facts['commit']}, published {facts['published']})")
    print(f"player: {facts['player']} (its OniMoono.dll says {facts['player_release']})")
    if "windows_archive" in facts:
        print(f"windows archive: {facts['windows_archive']} (verified, left packed)")
    if facts["player_release"] and facts["release"] and facts["player_release"].lstrip("v") != facts["release"].lstrip("v"):
        print("warning: the player's own release differs from the release's tag", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
