"""Workshop upload: release checks, the item's ZIP layout, the published-file comparison and the
gates before anything is sent to Steam (docs/workshop-upload.md). No test contacts Steam."""
import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock
import zipfile

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
import mod_identity  # noqa: E402

SPEC = importlib.util.spec_from_file_location("workshop", TOOLS / "workshop.py")
WORKSHOP = importlib.util.module_from_spec(SPEC); SPEC.loader.exec_module(WORKSHOP)

REVISION = "ab" * 20
OWNER = 76561190000000001


def release(root: Path, version: str = "1.12.3", builds=(744825, 764025), kind: str = "release") -> Path:
    folder = root / "MixedAtmospheres"
    content = {name: f"{name} {version}".encode() for name in WORKSHOP.RELEASE_FILES}
    content["mod.yaml"] = mod_identity.mod_yaml("A description").encode()
    content["README.txt"] = b"Read me\n"
    for build in builds:
        prefix = "" if build == max(builds) else f"archived_versions/{build}/"
        content[prefix + "mod_info.yaml"] = mod_identity.mod_info(version, build).encode()
        if prefix:
            content[prefix + "MixedAtmospheres.Oni.dll"] = f"managed {build}".encode()
    for name, data in content.items():
        (folder / name).parent.mkdir(parents=True, exist_ok=True)
        (folder / name).write_bytes(data)
    manifest = {"kind": kind, "release_ready": kind == "release", "static_id": "MixedAtmospheres", "version": version,
                "supported_builds": list(builds), "source_revision": REVISION, "source_dirty": False,
                "files": {name: hashlib.sha256(data).hexdigest() for name, data in content.items()}}
    (folder / "package-manifest.json").write_text(json.dumps(manifest))
    return folder


def rewrite_manifest(folder: Path, **changes) -> None:
    manifest = json.loads((folder / "package-manifest.json").read_text())
    manifest.update(changes)
    (folder / "package-manifest.json").write_text(json.dumps(manifest))


def state_of(folder: Path, scratch: Path, updated: int = 1000) -> dict:
    """The published state the Workshop would report while it serves ``folder``."""
    archive = scratch / f"served-{updated}-{len(list(scratch.iterdir()))}.zip"
    WORKSHOP.write_upload_zip(folder, json.loads((folder / "package-manifest.json").read_text()), archive)
    return {"record": {"publishedfileid": "1", "creator": str(OWNER), "title": "T", "visibility": 0, "banned": 0,
                       "subscriptions": 2, "time_updated": updated, "file_size": archive.stat().st_size},
            "contents": WORKSHOP.inspect_published(archive), "archive": str(archive)}


class FakeClient:
    def __init__(self, **changes):
        self.calls, self.closed = [], False
        self.values = {"app": mod_identity.WORKSHOP_UPLOADER_APP, "steam_id": OWNER, "logged_on": True, "owns": True,
                       "cloud": {"account": True, "app": True, "total": 1 << 30, "available": 1 << 30, "stored_file": None},
                       "details": {"result": 1, "item": mod_identity.WORKSHOP_ITEM, "creator_app": mod_identity.WORKSHOP_UPLOADER_APP,
                                   "consumer_app": mod_identity.ONI_APP, "owner": OWNER, "updated": 1000,
                                   "file_name": "f.zip", "file_size": 10, "title": "T", "visibility": 0},
                       "update": {"result": 1, "item": mod_identity.WORKSHOP_ITEM, "needs_legal_agreement": False}}
        self.values.update(changes)

    def app(self): return self.values["app"]
    def steam_id(self): return self.values["steam_id"]
    def logged_on(self): return self.values["logged_on"]
    def owns(self, app): return self.values["owns"]
    def cloud(self): return self.values["cloud"]
    def item_details(self, item): return self.values["details"]
    def write_file(self, name, data): self.calls.append(("write", name, hashlib.sha256(data).hexdigest()))
    def close(self): self.closed = True

    def update_item(self, item, name, note):
        self.calls.append(("update", item, name, note))
        return self.values["update"]


class ReleaseFolder(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(); self.addCleanup(self.scratch.cleanup)
        self.root = Path(self.scratch.name)
        self.folder = release(self.root)

    def refused(self, text):
        return self.assertRaisesRegex(WORKSHOP.Refused, text)

    def test_complete_release_is_accepted(self):
        self.assertEqual(WORKSHOP.check_release(self.folder)["version"], "1.12.3")

    def test_test_packages_and_dirty_builds_are_refused(self):
        rewrite_manifest(self.folder, kind="release-test", release_ready=False)
        with self.refused("Only a release package"):
            WORKSHOP.check_release(self.folder)
        rewrite_manifest(self.folder, kind="release", release_ready=True, source_dirty=True)
        with self.refused("clean, recorded revision"):
            WORKSHOP.check_release(self.folder)

    def test_changed_added_and_missing_files_are_refused(self):
        (self.folder / "README.txt").write_bytes(b"edited")
        with self.refused("File changed after packaging: README.txt"):
            WORKSHOP.check_release(self.folder)
        (self.folder / "README.txt").write_bytes(b"Read me\n")
        (self.folder / "notes.txt").write_bytes(b"x")
        with self.refused("exactly its manifest"):
            WORKSHOP.check_release(self.folder)
        (self.folder / "notes.txt").unlink(); (self.folder / "game-contract.json").unlink()
        with self.refused("exactly its manifest"):
            WORKSHOP.check_release(self.folder)

    def test_development_files_are_refused_even_when_pinned(self):
        (self.folder / "development.json").write_bytes(b"{}")
        manifest = json.loads((self.folder / "package-manifest.json").read_text())
        manifest["files"]["development.json"] = hashlib.sha256(b"{}").hexdigest()
        rewrite_manifest(self.folder, files=manifest["files"])
        with self.refused("must not contain development.json"):
            WORKSHOP.check_release(self.folder)

    def test_each_supported_build_states_the_version(self):
        other = release(self.root / "other", version="1.12.4")
        name = "archived_versions/744825/mod_info.yaml"
        data = (other / name).read_bytes()
        (self.folder / name).write_bytes(data)
        manifest = json.loads((self.folder / "package-manifest.json").read_text())
        manifest["files"][name] = hashlib.sha256(data).hexdigest()
        rewrite_manifest(self.folder, files=manifest["files"])
        with self.refused("does not state version 1.12.3 for game build 744825"):
            WORKSHOP.check_release(self.folder)

    def test_hold_file_beside_the_folder(self):
        self.assertIsNone(WORKSHOP.hold_reason(self.folder))
        (self.root / "KNOWN-ISSUE.txt").write_text("Cycle 55\nfreeze")
        self.assertIn("Cycle 55 freeze", WORKSHOP.hold_reason(self.folder))


class UploadZip(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(); self.addCleanup(self.scratch.cleanup)
        self.root = Path(self.scratch.name)
        self.folder = release(self.root)
        self.manifest = WORKSHOP.check_release(self.folder)

    def test_layout_is_the_uploaders(self):
        WORKSHOP.write_upload_zip(self.folder, self.manifest, self.root / "a.zip")
        with zipfile.ZipFile(self.root / "a.zip") as archive:
            entries = archive.infolist()
        names = {entry.orig_filename for entry in entries}  # as stored: Windows readers show "/"
        self.assertEqual(names, {name.replace("/", "\\") for name in [*self.manifest["files"], "package-manifest.json"]})
        self.assertIn("native\\linux-x64\\libmixed_atmospheres.so", names)
        self.assertIn("archived_versions\\744825\\mod_info.yaml", names)
        self.assertTrue(all(entry.create_system == 0 and entry.compress_type == zipfile.ZIP_DEFLATED
                            and not entry.external_attr >> 16 for entry in entries))

    def test_same_folder_gives_the_same_bytes(self):
        first = WORKSHOP.write_upload_zip(self.folder, self.manifest, self.root / "a.zip")
        second = WORKSHOP.write_upload_zip(self.folder, self.manifest, self.root / "b.zip")
        self.assertEqual(first, second)
        self.assertEqual((self.root / "a.zip").read_bytes(), (self.root / "b.zip").read_bytes())

    def test_inspection_reproduces_the_manifest(self):
        WORKSHOP.write_upload_zip(self.folder, self.manifest, self.root / "a.zip")
        found = WORKSHOP.inspect_published(self.root / "a.zip")
        self.assertEqual((found["files"], found["problems"], found["version"], found["source_revision"]),
                         (self.manifest["files"], [], "1.12.3", REVISION))
        self.assertEqual(WORKSHOP.differing_files(found["files"], self.manifest), [])

    def test_inspection_reports_a_changed_or_unpinned_file(self):
        with zipfile.ZipFile(self.root / "bad.zip", "w") as archive:
            archive.writestr("package-manifest.json", (self.folder / "package-manifest.json").read_bytes())
            for name in self.manifest["files"]:
                archive.writestr(name.replace("/", "\\"), b"other" if name == "README.txt" else (self.folder / name).read_bytes())
            archive.writestr("extra.dll", b"x")
        found = WORKSHOP.inspect_published(self.root / "bad.zip")
        self.assertEqual(len(found["problems"]), 1)
        self.assertIn("2 file(s) differ from the manifest: README.txt, extra.dll", found["problems"][0])
        self.assertEqual(WORKSHOP.differing_files(found["files"], self.manifest), ["README.txt", "extra.dll"])

    def test_a_foreign_zip_is_not_a_package(self):
        with zipfile.ZipFile(self.root / "foreign.zip", "w") as archive:
            archive.writestr("mod.yaml", b"x")
        found = WORKSHOP.inspect_published(self.root / "foreign.zip")
        self.assertEqual((found["version"], found["problems"]), (None, ["no package manifest"]))

    def test_download_only_from_steams_content_hosts(self):
        for url in ("http://cdn.steamusercontent.com/ugc/1/A/", "https://example.com/ugc/1/A/",
                    "https://steamusercontent.com.example.org/x"):
            with self.assertRaisesRegex(WORKSHOP.Refused, "Unexpected download address"):
                WORKSHOP.download_published({"file_url": url, "file_size": 10}, self.root / "d.zip")
        with self.assertRaisesRegex(WORKSHOP.Refused, "Unexpected published file size"):
            WORKSHOP.download_published({"file_url": "https://cdn.steamusercontent.com/ugc/1/A/",
                                         "file_size": WORKSHOP.MAX_PUBLISHED_BYTES + 1}, self.root / "d.zip")


class SteamSide(unittest.TestCase):
    def test_versions_order_numerically(self):
        ordered = ["0.1.9", "1.9.0", "1.10.0", "1.12.1", "1.12.2", "1.12.2.1", "1.12.3"]
        self.assertEqual(sorted(ordered, key=WORKSHOP.version_key), ordered)
        with self.assertRaises(WORKSHOP.Refused):
            WORKSHOP.version_key("1.12.2-test")

    def test_callback_structures_have_steams_sizes(self):
        # Steamworks.NET's own packing test values for Linux (pack 4) and Windows (pack 8).
        for pack, sizes in ((4, (16, 9748)), (8, (24, 9760))):
            update, details = WORKSHOP.callback_structures(pack)
            self.assertEqual((WORKSHOP.ctypes.sizeof(update), WORKSHOP.ctypes.sizeof(details)), sizes)

    def readiness(self, record=None, size=100, **changes):
        record = {"creator": str(OWNER)} if record is None else record
        return WORKSHOP.steam_readiness(FakeClient(**changes), mod_identity.WORKSHOP_ITEM, record, size,
                                        mod_identity.WORKSHOP_UPLOADER_APP)

    def test_ready_client_has_no_blockers(self):
        facts, blockers = self.readiness()
        self.assertEqual(blockers, [])
        self.assertTrue(any("owns the item" in fact for fact in facts))

    def test_each_obstacle_blocks(self):
        cases = {"not Klei's uploader": {"app": 457140}, "no licence": {"owns": False},
                 "not the item's owner": {"steam_id": OWNER + 1},
                 "Steam Cloud is off": {"cloud": {"account": True, "app": False, "total": 1, "available": 1 << 30, "stored_file": None}},
                 "bytes free": {"cloud": {"account": True, "app": True, "total": 50, "available": 50, "stored_file": 40}},
                 "Offline Mode": {"logged_on": False}}
        for text, change in cases.items():
            with self.subTest(text):
                self.assertTrue(any(text in blocker for blocker in self.readiness(**change)[1]))
        details = dict(FakeClient().values["details"], creator_app=457140)
        self.assertTrue(any("belongs to apps" in blocker for blocker in self.readiness(details=details)[1]))
        replaced = {"account": True, "app": True, "total": 150, "available": 50, "stored_file": 60}
        self.assertEqual(self.readiness(cloud=replaced)[1], [])  # the replaced file's space counts
        self.assertTrue(any("exceeds one Steam Cloud write" in blocker
                            for blocker in self.readiness(size=WORKSHOP.MAX_CLOUD_WRITE + 1)[1]))


class Upload(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(); self.addCleanup(self.scratch.cleanup)
        self.root = Path(self.scratch.name)
        self.served = self.root / "served"; self.served.mkdir()
        self.old = release(self.root / "old", version="1.12.2")
        self.new = release(self.root / "new", version="1.12.3")
        self.note = self.root / "note.txt"; self.note.write_text("1.12.3: fixes the freeze.\n")
        self.client = FakeClient()
        self.states = [state_of(self.old, self.served, 1000)]
        patches = [mock.patch.object(WORKSHOP, "published_state", side_effect=lambda item, folder: self.states.pop(0)
                                     if len(self.states) > 1 else self.states[0]),
                   mock.patch.object(WORKSHOP, "connect", side_effect=lambda args: self.client),
                   mock.patch.object(WORKSHOP, "game_running", return_value=False),
                   mock.patch.object(WORKSHOP, "revision_pushed", return_value=True),
                   mock.patch.object(WORKSHOP.time, "sleep")]
        for patch in patches:
            patch.start(); self.addCleanup(patch.stop)

    def run_upload(self, *arguments, package=None):
        output, errors = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(errors):
            code = WORKSHOP.main(["upload", "--package", str(package or self.new), "--note-file", str(self.note),
                                  "--record", str(self.root / "record"), *arguments])
        return code, output.getvalue() + errors.getvalue()

    EXECUTE = ("--execute", "--confirm", "1.12.3", "--authorized", "user, 2026-11-22: upload 1.12.3")

    def test_without_execute_nothing_reaches_steam(self):
        code, text = self.run_upload()
        self.assertEqual(code, 0)
        self.assertIn("DRY RUN: nothing was sent to Steam", text)
        self.assertEqual(self.client.calls, [])
        record = json.loads((self.root / "record/record.json").read_text())
        self.assertEqual((record["executed"], record["version"], record["replaced"]["version"]), (False, "1.12.3", "1.12.2"))

    def test_execute_needs_the_version_and_the_authorization(self):
        for arguments, text in ((("--execute",), "--confirm must repeat"),
                                (("--execute", "--confirm", "1.12.2"), "--confirm must repeat"),
                                (("--execute", "--confirm", "1.12.3"), "--authorized must quote"),
                                (("--execute", "--confirm", "1.12.3", "--authorized", "ok"), "--authorized must quote")):
            code, output = self.run_upload(*arguments)
            self.assertEqual(code, 2); self.assertIn(text, output)
        self.assertEqual(self.client.calls, [])

    def test_upload_writes_then_updates_then_verifies(self):
        self.states.append(state_of(self.new, self.served, 2000))
        code, text = self.run_upload(*self.EXECUTE)
        self.assertEqual(code, 0, text)
        digest = hashlib.sha256((self.root / "record" / mod_identity.WORKSHOP_FILE).read_bytes()).hexdigest()
        self.assertEqual(self.client.calls, [
            ("write", mod_identity.WORKSHOP_FILE, digest),
            ("update", mod_identity.WORKSHOP_ITEM, mod_identity.WORKSHOP_FILE, "1.12.3: fixes the freeze.")])
        self.assertTrue(self.client.closed)
        self.assertIn("UPLOADED AND VERIFIED", text)
        record = json.loads((self.root / "record/record.json").read_text())
        self.assertTrue(record["executed"] and record["published"]["matches_package"])
        self.assertEqual(record["authorization"], self.EXECUTE[-1])

    def test_a_different_published_file_fails_verification(self):
        self.states.append(state_of(self.old, self.served, 2000))
        code, text = self.run_upload(*self.EXECUTE)
        self.assertEqual(code, 2); self.assertIn("FAILED: the Workshop's file differs", text)

    def test_unchanged_public_record_is_unconfirmed(self):
        code, text = self.run_upload(*self.EXECUTE, "--verify-seconds", "0")
        self.assertEqual(code, 3); self.assertIn("UNCONFIRMED", text)

    def test_steam_refusal_and_failure(self):
        self.client = FakeClient(logged_on=False)
        code, text = self.run_upload(*self.EXECUTE)
        self.assertEqual((code, self.client.calls), (2, [])); self.assertIn("Offline Mode", text)
        self.client = FakeClient(update={"result": 15, "item": 0, "needs_legal_agreement": False})
        code, text = self.run_upload(*self.EXECUTE)
        self.assertEqual(code, 2); self.assertIn("Steam answered result 15", text)

    def test_gates_before_steam(self):
        (self.new.parent / "HOLD.txt").write_text("wait")
        code, text = self.run_upload(*self.EXECUTE)
        self.assertEqual(code, 2); self.assertIn("on hold", text)
        (self.new.parent / "HOLD.txt").unlink()
        with mock.patch.object(WORKSHOP, "revision_pushed", return_value=False):
            code, text = self.run_upload(*self.EXECUTE)
        self.assertEqual(code, 2); self.assertIn("not on origin/main", text)
        with mock.patch.object(WORKSHOP, "game_running", return_value=True):
            code, text = self.run_upload(*self.EXECUTE)
        self.assertEqual(code, 2); self.assertIn("ONI process is running", text)
        self.note.write_text(" \n")
        code, text = self.run_upload(*self.EXECUTE)
        self.assertEqual(code, 2); self.assertIn("change note", text)
        self.assertEqual(self.client.calls, [])

    def test_older_or_same_version_needs_a_deliberate_flag(self):
        self.states = [state_of(self.new, self.served, 1000)]
        code, text = self.run_upload(package=self.old)
        self.assertEqual(code, 2); self.assertIn("--replace-newer", text)
        code, text = self.run_upload("--replace-newer", package=self.old)
        self.assertEqual(code, 0); self.assertIn("DRY RUN", text)
        code, text = self.run_upload()
        self.assertEqual(code, 0); self.assertIn("nothing to upload", text)


class FetchMod(unittest.TestCase):
    """Another mod's published ZIP, unpacked for the tests that load it (fetch-mod); untrusted input."""
    def setUp(self):
        self.root = Path(tempfile.mkdtemp())

    def archive(self, names) -> Path:
        path = self.root / "mod.zip"
        with zipfile.ZipFile(path, "w") as archive:
            for name in names:
                archive.writestr(name, name)
        return path

    def test_a_mod_unpacks_with_windows_separators_into_a_new_folder(self):
        count = WORKSHOP.extract_mod(self.archive(["mod.yaml", "Other.dll", "anim\\assets\\a.png"]), self.root / "Other")
        self.assertEqual(count, 3)
        self.assertEqual((self.root / "Other/anim/assets/a.png").read_text(), "anim\\assets\\a.png")
        with self.assertRaises(FileExistsError):
            WORKSHOP.extract_mod(self.archive(["mod.yaml"]), self.root / "Other")

    def test_entries_outside_the_folder_or_without_a_mod_are_refused(self):
        for names in (["mod.yaml", "../escape.dll"], ["mod.yaml", "/abs.dll"], ["mod.yaml", "..\\escape.dll"], ["Other.dll"]):
            with self.assertRaises(WORKSHOP.Refused, msg=names):
                WORKSHOP.extract_mod(self.archive(names), self.root / ("x" + str(len(list(self.root.iterdir())))))


if __name__ == "__main__":
    unittest.main()
