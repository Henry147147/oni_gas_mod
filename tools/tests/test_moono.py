"""tools/moono.py without the network: release assets, SHA256SUMS, safe unpacking and the checks."""
import hashlib
import io
import json
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest
from unittest import mock
import zipfile

sys.path.insert(0, str(Path(__file__).parents[1]))
import moono

VERSION = "2026.11.26.02"


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


FILES = {"README.md": b"readme", "player/OniMoono": b"#!host", "player/OniMoono.dll": b"MZ\x0cMoonoRelease\x0ev2026.11.26.02\x00"}


def sums_text(files=FILES) -> str:
    return "".join(f"{sha(data)}  {name}\n" for name, data in sorted(files.items()))


def tarball(files=FILES, extra=None) -> bytes:
    buffer = io.BytesIO()
    with tarfile.open(fileobj=buffer, mode="w:gz") as tar:
        for name, data in files.items():
            info = tarfile.TarInfo("oni-moono/" + name); info.size = len(data); info.mode = 0o755 if name == "player/OniMoono" else 0o644
            tar.addfile(info, io.BytesIO(data))
        for info, data in extra or []:
            tar.addfile(info, io.BytesIO(data) if data is not None else None)
    return buffer.getvalue()


def release(archive: bytes, sums: bytes, platform="linux-x64", suffix=".tar.gz") -> dict:
    base = f"https://github.com/{moono.REPOSITORY}/releases/download/v{VERSION}/"
    return {"tag_name": "v" + VERSION, "target_commitish": "36d6e4eb", "published_at": "2026-11-26T03:00:00Z", "assets": [
        {"name": f"oni-moono-{VERSION}-{platform}{suffix}", "size": len(archive), "digest": "sha256:" + sha(archive),
         "browser_download_url": base + f"oni-moono-{VERSION}-{platform}{suffix}"},
        {"name": f"oni-moono-{VERSION}-{platform}.SHA256SUMS", "size": len(sums), "digest": "sha256:" + sha(sums),
         "browser_download_url": base + f"oni-moono-{VERSION}-{platform}.SHA256SUMS"}]}


class MoonoFetchTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(); self.root = Path(self.temporary.name)

    def tearDown(self):
        self.temporary.cleanup()

    def served(self, record: dict, blobs: dict):
        def request(url, accept="application/octet-stream"):
            if url.startswith(moono.API):
                return json.dumps(record).encode()
            return blobs[url.rsplit("/", 1)[1]]
        return mock.patch.object(moono, "request", side_effect=request)

    def test_fetch_checks_the_archive_and_every_file_and_unpacks_the_player(self):
        archive, sums = tarball(), sums_text().encode()
        record = release(archive, sums)
        names = [a["name"] for a in record["assets"]]
        with self.served(record, dict(zip(names, (archive, sums)))) as served:
            facts = moono.fetch(VERSION, False, self.root)
        self.assertEqual(served.call_args_list[0].args[0], moono.API + "/tags/v" + VERSION)
        self.assertEqual((facts["release"], facts["commit"], facts["player_release"]), ("v" + VERSION, "36d6e4eb", "v" + VERSION))
        player = Path(facts["player"])
        self.assertEqual(player, self.root / "linux/oni-moono/player")
        self.assertTrue((player / "OniMoono").stat().st_mode & 0o111)
        self.assertEqual((player / "OniMoono.dll").read_bytes(), FILES["player/OniMoono.dll"])
        self.assertTrue((self.root / f"release-{VERSION}.json").is_file())
        # A second fetch reuses the verified files and replaces the player folder whole.
        (player / "stale").write_text("x")
        with self.served(record, {}):
            moono.fetch(VERSION, False, self.root)
        self.assertFalse((player / "stale").exists())

    def test_a_changed_archive_or_a_file_that_differs_from_the_sums_is_refused(self):
        archive, sums = tarball(), sums_text().encode()
        record = release(archive, sums)
        names = [a["name"] for a in record["assets"]]
        with self.served(record, dict(zip(names, (archive + b"x", sums)))):
            with self.assertRaisesRegex(ValueError, "digest"):
                moono.fetch(VERSION, False, self.root)
        changed = dict(FILES, **{"README.md": b"other"})
        other = tarball(changed)
        record = release(other, sums)
        with self.served(record, dict(zip(names, (other, sums)))):
            with self.assertRaisesRegex(ValueError, "changed"):
                moono.fetch(VERSION, False, self.root)
        self.assertFalse((self.root / "linux/oni-moono").exists())

    def test_entries_outside_the_folder_links_and_unlisted_files_are_refused(self):
        sums = moono.parse_sums(sums_text())
        for name in ("../evil", "oni-moono/../../evil", "/abs/evil", "elsewhere/file"):
            info = tarfile.TarInfo(name); info.size = 1
            path = self.root / "a.tar.gz"; path.write_bytes(tarball(extra=[(info, b"x")]))
            with self.assertRaises(ValueError, msg=name):
                moono.unpack_linux(path, sums, self.root / "out")
        link = tarfile.TarInfo("oni-moono/player/link"); link.type = tarfile.SYMTYPE; link.linkname = "/etc/passwd"
        path = self.root / "b.tar.gz"; path.write_bytes(tarball(extra=[(link, None)]))
        with self.assertRaisesRegex(ValueError, "plain file"):
            moono.unpack_linux(path, sums, self.root / "out")
        extra = tarfile.TarInfo("oni-moono/player/extra.dll"); extra.size = 1
        path.write_bytes(tarball(extra=[(extra, b"x")]))
        with self.assertRaisesRegex(ValueError, "extra"):
            moono.unpack_linux(path, sums, self.root / "out")
        with self.assertRaisesRegex(ValueError, "escapes"):
            moono.parse_sums(f"{'0' * 64}  ../x\n")
        with self.assertRaisesRegex(ValueError, "unsafe release tag"):
            moono.release_info("v1/../../x")

    def test_the_windows_archive_is_checked_member_by_member_and_left_packed(self):
        buffer = io.BytesIO()
        with zipfile.ZipFile(buffer, "w") as packed:
            for name, data in FILES.items():
                packed.writestr("oni-moono/" + name, data)
        sums = sums_text().encode()
        path = self.root / "w.zip"; path.write_bytes(buffer.getvalue())
        moono.check_zip(path, moono.parse_sums(sums.decode()))
        with zipfile.ZipFile(path, "a") as packed:
            packed.writestr("oni-moono/player/added.dll", b"x")
        with self.assertRaisesRegex(ValueError, "extra"):
            moono.check_zip(path, moono.parse_sums(sums.decode()))

    def test_requests_name_no_one(self):
        self.assertNotIn("@", moono.USER_AGENT)
        self.assertNotRegex(moono.USER_AGENT.lower(), "henry|oni_gas_mod")


if __name__ == "__main__":
    unittest.main()
