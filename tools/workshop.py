#!/usr/bin/env python3
"""The Steam Workshop item: what is published, and how a release folder gets there.

  status     Read the item's public record, download the published file and
             compare it with its own manifest and, with --package, with a
             release folder. Needs no Steam sign-in and changes nothing.
  build      Check a release folder and write the single ZIP that the item
             holds, in the layout of Klei's uploader.
  preflight  Connect to the signed-in Steam client as Klei's uploader and
             report whether an upload could go ahead. Writes nothing.
  upload     Replace the published file with a release folder. A dry run
             unless --execute is given with the version and the user's
             authorization.
  fetch-mod  Download another mod's published Workshop file (e.g. 3609531831,
             Rephysicalized) into local/other-mods, for the tests that load
             it beside this one. Third-party mods never enter git.

The item is a legacy single-file item: ONI build 744825 reads a Workshop mod
only as one ZIP file, so the folder form that steamcmd's workshop_build_item
publishes would stop the mod there. See docs/workshop-upload.md.
"""
from __future__ import annotations

import argparse
import ctypes
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time
import urllib.parse
import urllib.request
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import mod_identity  # noqa: E402

ROOT = mod_identity.ROOT
DETAILS_URL = "https://api.steampowered.com/ISteamRemoteStorage/GetPublishedFileDetails/v1/"
CONTENT_HOSTS = (".steamusercontent.com",)
# The published file is untrusted input: bounded, read in memory, never extracted.
MAX_PUBLISHED_BYTES, MAX_ENTRIES, MAX_UNPACKED_BYTES = 512 << 20, 5000, 2 << 30
# Steam Cloud accepts at most this much in one FileWrite call.
MAX_CLOUD_WRITE = 100 << 20
ZIP_DATE = (2026, 1, 1, 0, 0, 0)
RELEASE_FILES = ("MixedAtmospheres.Oni.dll", "MixedAtmospheres.Core.dll", "game-contract.json",
                 "native/linux-x64/libmixed_atmospheres.so", "native/linux-x64/libmixed_atmospheres_oni.so",
                 "native/win-x64/mixed_atmospheres.dll", "native/win-x64/mixed_atmospheres_oni.dll")
# A file with one of these names beside the release folder holds the upload.
HOLD_FILES = ("HOLD.txt", "KNOWN-ISSUE.txt")
VISIBILITY = {0: "public", 1: "friends only", 2: "hidden", 3: "unlisted"}
PRIVATE_STEAM_HOME = Path.home() / ".local/share/oni-headless/steam-home"
E_RESULT_OK = 1


class Refused(Exception):
    """The step must not go ahead; the message says why."""


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def version_key(text: str) -> tuple[int, ...]:
    if not re.fullmatch(r"\d+(?:\.\d+){1,3}", text or ""):
        raise Refused(f"Not a numeric version: {text!r}")
    return tuple(int(part) for part in text.split("."))


def utc(seconds: int) -> str:
    return datetime.fromtimestamp(seconds, timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC")


# ---------------------------------------------------------------- the release folder

def check_release(package: Path) -> dict:
    """The folder is an unmodified, upload-ready release: return its manifest."""
    package = package.resolve()
    manifest_file = package / "package-manifest.json"
    if not manifest_file.is_file():
        raise Refused(f"{package} has no package-manifest.json: supply the release folder itself")
    manifest = json.loads(manifest_file.read_text(encoding="utf-8"))
    if manifest.get("kind") != "release" or manifest.get("release_ready") is not True:
        raise Refused("Only a release package is uploaded (this one is "
                      f"{manifest.get('kind')!r}, release_ready {manifest.get('release_ready')!r})")
    if manifest.get("source_dirty") is not False or not re.fullmatch(r"[0-9a-f]{40}", str(manifest.get("source_revision"))):
        raise Refused("The package was not built from a clean, recorded revision")
    if manifest.get("static_id") != mod_identity.STATIC_ID:
        raise Refused("The package is not Mixed Atmospheres")
    files = manifest.get("files")
    if not isinstance(files, dict) or not set(RELEASE_FILES).issubset(files):
        raise Refused("The package lacks a managed DLL, the game contract or a platform's native pair")
    present = {path.relative_to(package).as_posix() for path in package.rglob("*") if path.is_file()}
    if any(path.is_symlink() for path in package.rglob("*")):
        raise Refused("The package contains a symbolic link")
    if present != set(files) | {"package-manifest.json"}:
        extra, missing = sorted(present - set(files) - {"package-manifest.json"}), sorted(set(files) - present)
        raise Refused(f"The folder does not hold exactly its manifest's files (extra {extra}, missing {missing})")
    for name, expected in files.items():
        if sha256((package / name).read_bytes()) != expected:
            raise Refused(f"File changed after packaging: {name}")
    import package_release  # the packager's own content rules (no development files, no stray root DLL)
    try:
        package_release.check_release_contents(package, [package / name for name in present])
    except ValueError as error:
        raise Refused(str(error)) from None
    version = manifest.get("version")
    version_key(version)
    builds = manifest.get("supported_builds") or []
    if not builds or any(type(build) is not int for build in builds):
        raise Refused("The manifest lists no supported game builds")
    for build in builds:
        folder = "" if build == max(builds) else f"archived_versions/{build}/"
        name = folder + "mod_info.yaml"
        if name not in files:
            raise Refused(f"No {name} for game build {build}")
        if (package / name).read_text(encoding="utf-8") != mod_identity.mod_info(version, build):
            raise Refused(f"{name} does not state version {version} for game build {build}")
    if f'staticID: "{mod_identity.STATIC_ID}"' not in (package / "mod.yaml").read_text(encoding="utf-8"):
        raise Refused("mod.yaml does not carry the mod's staticID")
    return manifest


def hold_reason(package: Path) -> str | None:
    for name in HOLD_FILES:
        file = package.resolve().parent / name
        if file.is_file():
            return f"{file}: " + " ".join(file.read_text(encoding="utf-8", errors="replace").split())[:300]
    return None


def write_upload_zip(package: Path, manifest: dict, destination: Path) -> str:
    """The item's single file, as Klei's uploader writes it: the folder's files
    at the archive root, backslash separators, deflate, no directory entries.
    Fixed dates and order: the same folder gives the same bytes."""
    package = package.resolve()
    destination.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(destination, "x", zipfile.ZIP_DEFLATED) as archive:
        for name in sorted([*manifest["files"], "package-manifest.json"]):
            data = (package / name).read_bytes()
            if name in manifest["files"] and sha256(data) != manifest["files"][name]:
                raise Refused(f"File changed after packaging: {name}")
            info = zipfile.ZipInfo("entry", ZIP_DATE)
            # Set after construction: ZipInfo would turn the separators into "/" on Windows.
            info.filename = info.orig_filename = name.replace("/", "\\")
            # Written as on Windows: no Unix modes, only the DOS archive attribute.
            info.compress_type, info.create_system, info.external_attr = zipfile.ZIP_DEFLATED, 0, 0x20
            archive.writestr(info, data, compresslevel=9)
    found = inspect_published(destination)
    if found["problems"] or found["files"] != manifest["files"]:
        raise Refused("The upload ZIP does not reproduce the release folder: " + "; ".join(found["problems"]))
    return sha256(destination.read_bytes())


# ---------------------------------------------------------------- the published item

def published_record(item: int, timeout: float = 60) -> dict:
    """Steam's public record of the item (no key, no sign-in)."""
    body = urllib.parse.urlencode({"itemcount": 1, "publishedfileids[0]": item}).encode()
    with urllib.request.urlopen(urllib.request.Request(DETAILS_URL, data=body), timeout=timeout) as response:
        details = json.load(response)["response"]["publishedfiledetails"][0]
    if int(details.get("publishedfileid", 0)) != item or details.get("result") != E_RESULT_OK:
        raise Refused(f"Steam has no readable public record of item {item} (result {details.get('result')}): "
                      "a hidden or removed item cannot be checked this way")
    return details


def download_published(record: dict, destination: Path, timeout: float = 600) -> None:
    url, size = record.get("file_url", ""), int(record.get("file_size", 0))
    parts = urllib.parse.urlsplit(url)
    if parts.scheme != "https" or not any((parts.hostname or "").endswith(host) for host in CONTENT_HOSTS):
        raise Refused(f"Unexpected download address for the published file: {url!r}")
    if not 0 < size <= MAX_PUBLISHED_BYTES:
        raise Refused(f"Unexpected published file size: {size}")
    with urllib.request.urlopen(url, timeout=timeout) as response, destination.open("xb") as output:
        remaining = size + 1
        while remaining > 0 and (block := response.read(min(1 << 20, remaining))):
            output.write(block)
            remaining -= len(block)
    if destination.stat().st_size != size:
        raise Refused(f"The download has {destination.stat().st_size} bytes; Steam's record says {size}")


def inspect_published(archive_path: Path) -> dict:
    """What a published ZIP holds: its manifest's identity, every file's hash,
    and whether the files are exactly the ones its manifest pins."""
    problems: list[str] = []
    with zipfile.ZipFile(archive_path) as archive:
        entries = [entry for entry in archive.infolist() if not entry.filename.endswith(("/", "\\"))]
        if len(entries) > MAX_ENTRIES or sum(entry.file_size for entry in entries) > MAX_UNPACKED_BYTES:
            raise Refused("The published ZIP is larger than any release")
        files: dict[str, str] = {}
        manifest: dict = {}
        for entry in entries:
            name = entry.filename.replace("\\", "/")
            data = archive.read(entry)
            if name in files or name.startswith("/") or ".." in name.split("/"):
                problems.append(f"unexpected entry name {name!r}")
            if name == "package-manifest.json":
                try:
                    manifest = json.loads(data)
                except ValueError:
                    problems.append("unreadable package-manifest.json")
            else:
                files[name] = sha256(data)
    pinned = manifest.get("files") if isinstance(manifest.get("files"), dict) else None
    if pinned is None:
        problems.append("no package manifest")
    elif pinned != files:
        changed = sorted(name for name in set(pinned) | set(files) if pinned.get(name) != files.get(name))
        problems.append(f"{len(changed)} file(s) differ from the manifest: " + ", ".join(changed[:5]))
    return {"version": manifest.get("version"), "source_revision": manifest.get("source_revision"),
            "kind": manifest.get("kind"), "release_ready": manifest.get("release_ready"),
            "supported_builds": manifest.get("supported_builds"), "files": files, "problems": problems,
            "sha256": sha256(archive_path.read_bytes()), "bytes": archive_path.stat().st_size}


OTHER_MODS = ROOT / "local/other-mods"


def safe_member(name: str) -> str | None:
    """A ZIP entry's path with forward slashes, or None when it would leave its folder."""
    path = name.replace("\\", "/")
    parts = [part for part in path.split("/") if part not in ("", ".")]
    if not parts or path.startswith("/") or ".." in parts or ":" in parts[0]:
        return None
    return "/".join(parts)


def extract_mod(archive_path: Path, destination: Path) -> int:
    """Unpack a published mod ZIP (untrusted: bounded, no path leaves ``destination``) into a new folder."""
    with zipfile.ZipFile(archive_path) as archive:
        entries = [entry for entry in archive.infolist() if not entry.filename.endswith(("/", "\\"))]
        if len(entries) > MAX_ENTRIES or sum(entry.file_size for entry in entries) > MAX_UNPACKED_BYTES:
            raise Refused("The published ZIP is larger than a mod should be")
        names = [safe_member(entry.filename) for entry in entries]
        if any(name is None for name in names) or len(set(names)) != len(names):
            raise Refused("The published ZIP has entries outside its folder or twice")
        if "mod.yaml" not in names:
            raise Refused("The published ZIP holds no mod.yaml at its top")
        destination.mkdir(parents=True)
        for entry, name in zip(entries, names):
            target = destination / name
            target.parent.mkdir(parents=True, exist_ok=True)
            with archive.open(entry) as source, target.open("xb") as output:
                output.write(source.read(MAX_UNPACKED_BYTES + 1))
        return len(entries)


def command_fetch_mod(args) -> int:
    record = published_record(args.mod_item)
    name = args.name or re.sub(r"[^A-Za-z0-9_.-]", "", str(record.get("title") or "")) or str(args.mod_item)
    folder = OTHER_MODS / "src" / name
    if folder.exists():
        raise Refused(f"{folder} exists; remove it first to fetch item {args.mod_item} again")
    downloads = OTHER_MODS / "download"
    downloads.mkdir(parents=True, exist_ok=True)
    archive = downloads / f"{args.mod_item}-{record.get('time_updated')}.zip"
    if not archive.exists():
        download_published(record, archive)
    count = extract_mod(archive, folder)
    print(f"{record.get('title')} (Workshop item {args.mod_item}, updated {utc(int(record.get('time_updated') or 0))}): "
          f"{count} files in {folder}")
    return 0


def published_state(item: int, folder: Path) -> dict:
    """The public record and the inspected published file, kept in ``folder``."""
    record = published_record(item)
    archive = folder / f"published-{record.get('time_updated')}.zip"
    if not archive.exists():
        download_published(record, archive)
    return {"record": {key: record.get(key) for key in (
                "publishedfileid", "creator", "creator_app_id", "consumer_app_id", "filename", "file_size", "title",
                "time_created", "time_updated", "visibility", "banned", "subscriptions", "hcontent_file")},
            "contents": inspect_published(archive), "archive": str(archive)}


def differing_files(published: dict, manifest: dict) -> list[str]:
    ours = manifest["files"]
    return sorted(name for name in set(ours) | set(published) if ours.get(name) != published.get(name))


def describe_state(state: dict) -> list[str]:
    record, contents = state["record"], state["contents"]
    visibility = VISIBILITY.get(record.get("visibility"), f"visibility {record.get('visibility')}")
    lines = [f"Workshop item {record['publishedfileid']} \"{record.get('title')}\": {visibility}"
             + (", BANNED" if record.get("banned") else "") + f", {record.get('subscriptions')} subscribers",
             f"Published file: {contents['bytes']:,} bytes, uploaded {utc(int(record['time_updated']))}"]
    if contents["version"]:
        lines.append(f"It holds Mixed Atmospheres {contents['version']} (revision "
                     f"{str(contents['source_revision'])[:7]}, {contents['kind']}), "
                     + ("every file as its manifest pins it" if not contents["problems"] else "BUT " + "; ".join(contents["problems"])))
    else:
        lines.append("It is not a Mixed Atmospheres package: " + "; ".join(contents["problems"]))
    return lines


# ---------------------------------------------------------------- the Steam client

PACK = 8 if os.name == "nt" else 4  # Steam's callback structures: 8 on Windows, 4 elsewhere


def callback_structures(pack: int = PACK) -> tuple[type, type]:
    """RemoteStorageUpdatePublishedFileResult_t (1316) and
    RemoteStorageGetPublishedFileDetailsResult_t (1318), as the game's Steamworks.NET declares them."""
    class UpdateResult(ctypes.Structure):
        _pack_, _layout_ = pack, "ms"  # plain packed fields, as Steam declares them
        _fields_ = [("result", ctypes.c_int32), ("item", ctypes.c_uint64), ("needs_legal_agreement", ctypes.c_bool)]

    class ItemDetails(ctypes.Structure):
        _pack_, _layout_ = pack, "ms"  # plain packed fields, as Steam declares them
        _fields_ = [("result", ctypes.c_int32), ("item", ctypes.c_uint64), ("creator_app", ctypes.c_uint32),
                    ("consumer_app", ctypes.c_uint32), ("title", ctypes.c_char * 129),
                    ("description", ctypes.c_char * 8000), ("file", ctypes.c_uint64), ("preview", ctypes.c_uint64),
                    ("owner", ctypes.c_uint64), ("created", ctypes.c_uint32), ("updated", ctypes.c_uint32),
                    ("visibility", ctypes.c_int32), ("banned", ctypes.c_bool), ("tags", ctypes.c_char * 1025),
                    ("tags_truncated", ctypes.c_bool), ("file_name", ctypes.c_char * 260),
                    ("file_size", ctypes.c_int32), ("preview_size", ctypes.c_int32), ("url", ctypes.c_char * 256),
                    ("file_type", ctypes.c_int32), ("accepted", ctypes.c_bool)]
    return UpdateResult, ItemDetails


UPDATE_RESULT_ID, ITEM_DETAILS_ID = 1316, 1318


def steam_library(game: Path) -> Path:
    names = (("OxygenNotIncluded_Data/Plugins/x86_64/steam_api64.dll",) if os.name == "nt" else
             ("OxygenNotIncluded_Data/Plugins/AnyCPU/libsteam_api.so", "OxygenNotIncluded_Data/Plugins/x86_64/libsteam_api.so",
              "OxygenNotIncluded_Data/Plugins/libsteam_api.so"))
    for name in names:
        if (game / name).is_file():
            return game / name
    raise Refused(f"No Steam API library in {game}: pass the ONI installation of this platform with --game-dir")


class SteamClient:
    """The few Steamworks calls an upload needs, through the game's own
    libsteam_api and the Steam client that is already signed in. Nothing here
    signs in or handles credentials: without a signed-in client it refuses."""

    def __init__(self, library: Path, app: int, steam_home: Path | None = None):
        self.home, self.previous_home = None, os.environ.get("HOME")
        if steam_home is not None:
            # libsteam_api finds the client through ~/.steam; point this process at the private
            # client's links the way the harness does for a game, and at nothing else of that home.
            if not (steam_home / ".steam/steam.pid").exists():
                raise Refused(f"No Steam client is installed under {steam_home}")
            self.home = tempfile.TemporaryDirectory(prefix="workshop-home-")
            (Path(self.home.name) / ".steam").symlink_to(steam_home / ".steam")
            os.environ["HOME"] = self.home.name
        os.environ["SteamAppId"] = os.environ["SteamGameId"] = str(app)
        self.api = api = ctypes.CDLL(str(library))
        pointer, u64, u32, i32, text, flag = (ctypes.c_void_p, ctypes.c_uint64, ctypes.c_uint32, ctypes.c_int32,
                                              ctypes.c_char_p, ctypes.c_bool)
        signatures = {
            "SteamAPI_Init": (flag, []), "SteamAPI_Shutdown": (None, []), "SteamAPI_RunCallbacks": (None, []),
            "SteamAPI_SteamUser_v021": (pointer, []), "SteamAPI_SteamApps_v008": (pointer, []),
            "SteamAPI_SteamUtils_v010": (pointer, []), "SteamAPI_SteamRemoteStorage_v016": (pointer, []),
            "SteamAPI_ISteamUser_GetSteamID": (u64, [pointer]), "SteamAPI_ISteamUser_BLoggedOn": (flag, [pointer]),
            "SteamAPI_ISteamApps_BIsSubscribedApp": (flag, [pointer, u32]),
            "SteamAPI_ISteamUtils_GetAppID": (u32, [pointer]),
            "SteamAPI_ISteamUtils_IsAPICallCompleted": (flag, [pointer, u64, ctypes.POINTER(flag)]),
            "SteamAPI_ISteamUtils_GetAPICallResult": (flag, [pointer, u64, pointer, ctypes.c_int, ctypes.c_int, ctypes.POINTER(flag)]),
            "SteamAPI_ISteamUtils_GetAPICallFailureReason": (ctypes.c_int, [pointer, u64]),
            "SteamAPI_ISteamRemoteStorage_IsCloudEnabledForAccount": (flag, [pointer]),
            "SteamAPI_ISteamRemoteStorage_IsCloudEnabledForApp": (flag, [pointer]),
            "SteamAPI_ISteamRemoteStorage_GetQuota": (flag, [pointer, ctypes.POINTER(u64), ctypes.POINTER(u64)]),
            "SteamAPI_ISteamRemoteStorage_FileWrite": (flag, [pointer, text, pointer, i32]),
            "SteamAPI_ISteamRemoteStorage_FileRead": (i32, [pointer, text, pointer, i32]),
            "SteamAPI_ISteamRemoteStorage_FileExists": (flag, [pointer, text]),
            "SteamAPI_ISteamRemoteStorage_GetFileSize": (i32, [pointer, text]),
            "SteamAPI_ISteamRemoteStorage_GetPublishedFileDetails": (u64, [pointer, u64, u32]),
            "SteamAPI_ISteamRemoteStorage_CreatePublishedFileUpdateRequest": (u64, [pointer, u64]),
            "SteamAPI_ISteamRemoteStorage_UpdatePublishedFileFile": (flag, [pointer, u64, text]),
            "SteamAPI_ISteamRemoteStorage_UpdatePublishedFileSetChangeDescription": (flag, [pointer, u64, text]),
            "SteamAPI_ISteamRemoteStorage_CommitPublishedFileUpdate": (u64, [pointer, u64]),
        }
        for name, (result, arguments) in signatures.items():
            function = getattr(api, name)
            function.restype, function.argtypes = result, arguments
        if not api.SteamAPI_Init():
            raise Refused(f"Steam did not accept this process as app {app}: the client must be running and signed in "
                          "with the account that owns it (the user signs in; nothing here does)")
        self.user, self.apps, self.utils, self.storage = (
            api.SteamAPI_SteamUser_v021(), api.SteamAPI_SteamApps_v008(), api.SteamAPI_SteamUtils_v010(),
            api.SteamAPI_SteamRemoteStorage_v016())
        if not all((self.user, self.apps, self.utils, self.storage)):
            self.close()
            raise Refused("The Steam client does not offer the interfaces of this libsteam_api")

    def close(self) -> None:
        self.api.SteamAPI_Shutdown()
        if self.home:
            self.home.cleanup()
            self.home = None
            if self.previous_home is not None:
                os.environ["HOME"] = self.previous_home

    def app(self) -> int: return self.api.SteamAPI_ISteamUtils_GetAppID(self.utils)
    def steam_id(self) -> int: return self.api.SteamAPI_ISteamUser_GetSteamID(self.user)
    def logged_on(self) -> bool: return bool(self.api.SteamAPI_ISteamUser_BLoggedOn(self.user))
    def owns(self, app: int) -> bool: return bool(self.api.SteamAPI_ISteamApps_BIsSubscribedApp(self.apps, app))

    def cloud(self) -> dict:
        total, available = ctypes.c_uint64(), ctypes.c_uint64()
        known = self.api.SteamAPI_ISteamRemoteStorage_GetQuota(self.storage, ctypes.byref(total), ctypes.byref(available))
        name = mod_identity.WORKSHOP_FILE.encode()
        present = self.api.SteamAPI_ISteamRemoteStorage_FileExists(self.storage, name)
        return {"account": bool(self.api.SteamAPI_ISteamRemoteStorage_IsCloudEnabledForAccount(self.storage)),
                "app": bool(self.api.SteamAPI_ISteamRemoteStorage_IsCloudEnabledForApp(self.storage)),
                "total": total.value if known else None, "available": available.value if known else None,
                # The previous upload, once this client has fetched the uploader's Steam Cloud.
                "stored_file": self.api.SteamAPI_ISteamRemoteStorage_GetFileSize(self.storage, name) if present else None}

    def _result(self, call: int, structure: type, identifier: int, seconds: float):
        if not call:
            raise Refused("Steam did not start the request")
        failed, deadline = ctypes.c_bool(False), time.monotonic() + seconds
        while not self.api.SteamAPI_ISteamUtils_IsAPICallCompleted(self.utils, call, ctypes.byref(failed)):
            if time.monotonic() > deadline:
                raise Refused(f"Steam did not answer within {seconds:.0f} s")
            self.api.SteamAPI_RunCallbacks()
            time.sleep(0.05)
        value = structure()
        if not self.api.SteamAPI_ISteamUtils_GetAPICallResult(self.utils, call, ctypes.byref(value), ctypes.sizeof(value),
                                                              identifier, ctypes.byref(failed)) or failed.value:
            reason = self.api.SteamAPI_ISteamUtils_GetAPICallFailureReason(self.utils, call)
            raise Refused(f"Steam's request failed (failure reason {reason})")
        return value

    def item_details(self, item: int, seconds: float = 60) -> dict:
        call = self.api.SteamAPI_ISteamRemoteStorage_GetPublishedFileDetails(self.storage, item, 0)
        value = self._result(call, callback_structures()[1], ITEM_DETAILS_ID, seconds)
        return {"result": value.result, "item": value.item, "creator_app": value.creator_app,
                "consumer_app": value.consumer_app, "owner": value.owner, "updated": value.updated,
                "file_name": value.file_name.decode("utf-8", "replace"), "file_size": value.file_size,
                "title": value.title.decode("utf-8", "replace"), "visibility": value.visibility}

    def write_file(self, name: str, data: bytes) -> None:
        encoded = name.encode()
        if not self.api.SteamAPI_ISteamRemoteStorage_FileWrite(self.storage, encoded, data, len(data)):
            raise Refused(f"Steam Cloud refused {name} ({len(data):,} bytes)")
        size = self.api.SteamAPI_ISteamRemoteStorage_GetFileSize(self.storage, encoded)
        buffer = ctypes.create_string_buffer(len(data))
        read = self.api.SteamAPI_ISteamRemoteStorage_FileRead(self.storage, encoded, buffer, len(data)) if size == len(data) else -1
        if read != len(data) or buffer.raw != data:
            raise Refused(f"Steam Cloud does not return {name} as written")

    def update_item(self, item: int, name: str, note: str, seconds: float = 1800) -> dict:
        handle = self.api.SteamAPI_ISteamRemoteStorage_CreatePublishedFileUpdateRequest(self.storage, item)
        if handle in (0, 0xFFFFFFFFFFFFFFFF):
            raise Refused("Steam did not open an update request for the item")
        if not self.api.SteamAPI_ISteamRemoteStorage_UpdatePublishedFileFile(self.storage, handle, name.encode()):
            raise Refused("Steam did not accept the new file for the update")
        if not self.api.SteamAPI_ISteamRemoteStorage_UpdatePublishedFileSetChangeDescription(self.storage, handle, note.encode()):
            raise Refused("Steam did not accept the change note")
        call = self.api.SteamAPI_ISteamRemoteStorage_CommitPublishedFileUpdate(self.storage, handle)
        value = self._result(call, callback_structures()[0], UPDATE_RESULT_ID, seconds)
        return {"result": value.result, "item": value.item, "needs_legal_agreement": bool(value.needs_legal_agreement)}


def steam_readiness(client, item: int, record: dict | None, size: int, uploader: int) -> tuple[list[str], list[str]]:
    """What stands between this client and an upload: (facts, blockers). Reads only."""
    facts, blockers = [], []
    if client.app() != uploader:
        blockers.append(f"Steam runs this process as app {client.app()}, not Klei's uploader ({uploader})")
    if not client.owns(uploader):
        blockers.append(f"The signed-in account has no licence for Klei's uploader (app {uploader})")
    if record is not None:
        if str(client.steam_id()) != str(record.get("creator")):
            blockers.append("The signed-in Steam account is not the item's owner")
        else:
            facts.append("The signed-in account owns the item")
    cloud = client.cloud()
    if not cloud["account"] or not cloud["app"]:
        blockers.append(f"Steam Cloud is off (account {cloud['account']}, uploader {cloud['app']}): the item's file "
                        "travels through the uploader's Steam Cloud")
    # The new file replaces the stored one, whose space therefore counts as free.
    free = None if cloud["available"] is None else cloud["available"] + (cloud.get("stored_file") or 0)
    if free is not None and free < size:
        blockers.append(f"The uploader's Steam Cloud has {free:,} bytes free; the file needs {size:,}")
    stored = cloud.get("stored_file")
    facts.append(f"Steam Cloud: on, {cloud['available']} of {cloud['total']} bytes free, "
                 + (f"previous upload file present ({stored:,} bytes)" if stored is not None else "no upload file on this machine yet"))
    if size > MAX_CLOUD_WRITE:
        blockers.append(f"The file ({size:,} bytes) exceeds one Steam Cloud write ({MAX_CLOUD_WRITE:,})")
    if not client.logged_on():
        blockers.append("The Steam client is in Offline Mode or not connected: bring it online first "
                        "(docs/workshop-upload.md#the-online-window)")
        return facts, blockers
    details = client.item_details(item)
    if details["result"] != E_RESULT_OK or details["item"] != item:
        blockers.append(f"Steam does not return the item to this account (result {details['result']})")
    else:
        facts.append(f"Steam's own record: \"{details['title']}\", {details['file_name']}, {details['file_size']:,} bytes, "
                     f"uploaded {utc(details['updated'])}, creator app {details['creator_app']}")
        if details["owner"] != client.steam_id():
            blockers.append("Steam's record names another account as the item's owner")
        if details["creator_app"] != uploader or details["consumer_app"] != mod_identity.ONI_APP:
            blockers.append(f"The item belongs to apps {details['creator_app']}/{details['consumer_app']}, "
                            f"not {uploader}/{mod_identity.ONI_APP}")
    return facts, blockers


def revision_pushed(revision: str) -> bool:
    """Whether the revision is pushed work: on origin/main, or on a release line origin/release/*
    (a hotfix of the released version is cut from its release branch while main carries later work)."""
    refs = subprocess.run(["git", "-C", str(ROOT), "for-each-ref", "--format=%(refname:short)", "refs/remotes/origin/release"],
                          capture_output=True, text=True).stdout.split()
    return any(subprocess.run(["git", "-C", str(ROOT), "merge-base", "--is-ancestor", revision, ref],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
               for ref in ["origin/main", *refs])


def game_running() -> bool:
    if os.name == "nt":
        return False
    return subprocess.run(["pgrep", "-x", "OxygenNotInclud"], stdout=subprocess.DEVNULL).returncode == 0


def connect(args) -> "SteamClient":
    home = args.steam_home
    if home is None and os.name != "nt" and PRIVATE_STEAM_HOME.is_dir():
        home = PRIVATE_STEAM_HOME
    if home is not None and home == PRIVATE_STEAM_HOME:
        # The private client must stay invisible to the user's friends (user decision 2026-11-06).
        import oni_headless
        state = PRIVATE_STEAM_HOME.parent
        oni_headless.steam_offline(state, oni_headless.read_session(state))
    return SteamClient(steam_library(args.game_dir), mod_identity.WORKSHOP_UPLOADER_APP, home)


# ---------------------------------------------------------------- commands

def work_folder(args, label: str) -> Path:
    folder = args.record or ROOT / "artifacts/workshop" / (datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ-") + label)
    folder.mkdir(parents=True, exist_ok=True)
    return folder.resolve()


def command_status(args) -> int:
    with tempfile.TemporaryDirectory(prefix="workshop-status-") as scratch:
        state = published_state(args.item, Path(args.record or scratch))
        print("\n".join(describe_state(state)))
        result, different = {"published": state}, False
        if args.package:
            manifest = check_release(args.package)
            changed = differing_files(state["contents"]["files"], manifest)
            different = bool(changed) or bool(state["contents"]["problems"])
            result["package"] = {"version": manifest["version"], "source_revision": manifest["source_revision"],
                                 "differing_files": changed}
            print(f"{args.package}: Mixed Atmospheres {manifest['version']} (revision {manifest['source_revision'][:7]}) "
                  + ("is what the Workshop serves, file for file" if not different else
                     f"is NOT what the Workshop serves ({len(changed)} of {len(manifest['files'])} files differ)"))
        if args.json:
            args.json.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return 1 if different or state["contents"]["problems"] else 0


def command_build(args) -> int:
    manifest = check_release(args.package)
    digest = write_upload_zip(args.package, manifest, args.output)
    print(f"{args.output}: Mixed Atmospheres {manifest['version']} ({manifest['source_revision'][:7]}), "
          f"{args.output.stat().st_size:,} bytes, SHA-256 {digest}")
    return 0


def command_preflight(args) -> int:
    record = published_record(args.item)
    size = int(record["file_size"])
    if args.package:
        with tempfile.TemporaryDirectory(prefix="workshop-preflight-") as scratch:
            write_upload_zip(args.package, check_release(args.package), Path(scratch) / mod_identity.WORKSHOP_FILE)
            size = (Path(scratch) / mod_identity.WORKSHOP_FILE).stat().st_size
    if game_running():
        raise Refused("An ONI process is running: use the Steam client between games, with the game slot held")
    client = connect(args)
    try:
        facts, blockers = steam_readiness(client, args.item, record, size, mod_identity.WORKSHOP_UPLOADER_APP)
    finally:
        client.close()
    for line in facts:
        print("ok:", line)
    for line in blockers:
        print("BLOCKED:", line)
    print("Nothing was written." + ("" if blockers else " An upload could go ahead."))
    return 1 if blockers else 0


def command_upload(args) -> int:
    package = args.package.resolve()
    manifest = check_release(package)
    version = manifest["version"]
    note = args.note_file.read_text(encoding="utf-8").strip()
    if not note or len(note.encode()) > 7999:
        raise Refused("The change note must hold 1 to 7,999 bytes of text")
    hold = hold_reason(package)
    if hold and not args.override_hold:
        raise Refused(f"The release is on hold ({hold}). Remove the hold file when the reason is gone.")
    folder = work_folder(args, version)
    before = published_state(args.item, folder)
    print("\n".join(describe_state(before)))
    changed = differing_files(before["contents"]["files"], manifest)
    if not changed and not before["contents"]["problems"]:
        print(f"The Workshop already serves {version} file for file: nothing to upload.")
        return 0
    live = before["contents"]["version"]
    if live and version_key(version) <= version_key(live) and not args.replace_newer:
        raise Refused(f"The Workshop serves {live}, which is not older than {version}. "
                      "Going back to an earlier release is a deliberate step: --replace-newer")
    if not revision_pushed(manifest["source_revision"]):
        raise Refused(f"Revision {manifest['source_revision'][:7]} is not on origin/main or an origin/release branch (fetch first): only pushed work is uploaded")
    archive = folder / mod_identity.WORKSHOP_FILE
    archive.unlink(missing_ok=True)
    digest = write_upload_zip(package, manifest, archive)
    data = archive.read_bytes()
    record = {"item": args.item, "version": version, "source_revision": manifest["source_revision"],
              "package": str(package), "upload_sha256": digest, "upload_bytes": len(data), "change_note": note,
              "replaced": {key: before["contents"][key] for key in ("version", "source_revision", "sha256", "bytes")},
              "replaced_uploaded": before["record"]["time_updated"], "authorization": args.authorized, "executed": False}
    print(f"Upload file: {archive} ({len(data):,} bytes, SHA-256 {digest}); {len(changed)} files differ from the Workshop's")
    print(f"Change note:\n  " + note.replace("\n", "\n  "))

    def finish(code: int) -> int:
        (folder / "record.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
        print(f"Record: {folder / 'record.json'}")
        return code

    if not args.execute:
        print(f"DRY RUN: nothing was sent to Steam. To upload: --execute --confirm {version} --authorized \"<the user's words and date>\"")
        return finish(0)
    if args.confirm != version:
        raise Refused(f"--confirm must repeat the version being uploaded ({version})")
    if not args.authorized or len(args.authorized.strip()) < 10:
        raise Refused("--authorized must quote the user's go-ahead for this version (AGENTS.md: no Workshop upload without it)")
    if game_running():
        raise Refused("An ONI process is running: upload between games, with the game slot held")
    client = connect(args)
    try:
        facts, blockers = steam_readiness(client, args.item, before["record"], len(data), mod_identity.WORKSHOP_UPLOADER_APP)
        if blockers:
            raise Refused("Steam is not ready for the upload:\n  " + "\n  ".join(blockers))
        client.write_file(mod_identity.WORKSHOP_FILE, data)
        record["executed"] = True
        record["steam_result"] = result = client.update_item(args.item, mod_identity.WORKSHOP_FILE, note)
    finally:
        client.close()
        if record["executed"] and "steam_result" not in record:
            finish(2)
    if result["result"] != E_RESULT_OK:
        print(f"FAILED: Steam answered result {result['result']}; run 'status' to see what the Workshop serves now.")
        return finish(2)
    if result["needs_legal_agreement"]:
        print("NOTE: Steam says the Workshop legal agreement needs accepting (the user does that on the item's page).")
    deadline = time.monotonic() + args.verify_seconds
    while True:
        after = published_state(args.item, folder)
        if int(after["record"]["time_updated"]) != int(before["record"]["time_updated"]):
            break
        if time.monotonic() > deadline:
            print("UNCONFIRMED: Steam accepted the update but its public record still shows the old file; run 'status' later.")
            return finish(3)
        time.sleep(15)
    print("\n".join(describe_state(after)))
    remaining = differing_files(after["contents"]["files"], manifest)
    record["published"] = {"time_updated": after["record"]["time_updated"], "sha256": after["contents"]["sha256"],
                           "matches_package": not remaining and not after["contents"]["problems"]}
    if not record["published"]["matches_package"]:
        print(f"FAILED: the Workshop's file differs from the package in {len(remaining)} files.")
        return finish(2)
    print(f"UPLOADED AND VERIFIED: the Workshop serves {version} ({manifest['source_revision'][:7]}) file for file, "
          f"uploaded {utc(int(after['record']['time_updated']))}. Add it to the list in docs/workshop-upload.md.")
    return finish(0)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--item", type=int, default=mod_identity.WORKSHOP_ITEM, help="Workshop item (default: the published mod)")
    commands = parser.add_subparsers(dest="command", required=True)

    def steam_options(command):
        command.add_argument("--game-dir", type=Path, default=Path("/mnt/data/share/oni/OxygenNotIncluded-linux"),
                             help="ONI installation of this platform, for its Steam API library")
        command.add_argument("--steam-home", type=Path,
                             help="Home folder of the signed-in Steam client (default on Linux: the private harness client)")

    status = commands.add_parser("status", help="What the Workshop serves now")
    status.add_argument("--package", type=Path, help="Release folder to compare with")
    status.add_argument("--record", type=Path, help="Keep the downloaded file in this folder")
    status.add_argument("--json", type=Path, help="Write the result here")
    build = commands.add_parser("build", help="Write the item's ZIP from a release folder")
    build.add_argument("--package", type=Path, required=True)
    build.add_argument("--output", type=Path, required=True, help="New ZIP file")
    preflight = commands.add_parser("preflight", help="Check the signed-in Steam client; writes nothing")
    preflight.add_argument("--package", type=Path, help="Release folder whose size must fit")
    steam_options(preflight)
    upload = commands.add_parser("upload", help="Replace the published file (dry run without --execute)")
    upload.add_argument("--package", type=Path, required=True, help="Release folder (MixedAtmospheres)")
    upload.add_argument("--note-file", type=Path, required=True, help="The change note players see, as plain text")
    upload.add_argument("--execute", action="store_true", help="Really upload")
    upload.add_argument("--confirm", help="The version being uploaded, repeated")
    upload.add_argument("--authorized", help="The user's go-ahead for this upload, quoted with its date")
    upload.add_argument("--replace-newer", action="store_true", help="Allow replacing a newer or equal published version")
    upload.add_argument("--override-hold", action="store_true", help="Upload although a hold file lies beside the folder")
    upload.add_argument("--record", type=Path, help="Folder for the upload file and record (default: artifacts/workshop/...)")
    upload.add_argument("--verify-seconds", type=int, default=900, help="How long to wait for Steam's public record to change")
    steam_options(upload)
    fetch = commands.add_parser("fetch-mod", help="Download another mod's Workshop file into local/other-mods")
    fetch.add_argument("mod_item", type=int, help="Its Workshop item, e.g. 3609531831 (Rephysicalized)")
    fetch.add_argument("--name", help="Folder name under local/other-mods/src (default: its title)")
    args = parser.parse_args(argv)
    try:
        return {"status": command_status, "build": command_build, "preflight": command_preflight,
                "upload": command_upload, "fetch-mod": command_fetch_mod}[args.command](args)
    except Refused as refusal:
        print(f"REFUSED: {refusal}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
