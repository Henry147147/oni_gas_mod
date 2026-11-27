# Workshop upload

How a release folder reaches the Steam Workshop, and how to tell what the
Workshop serves. The tool is `tools/workshop.py`. **Every upload needs the
user's go-ahead for that version** (AGENTS.md); the tool sends nothing to Steam
unless it is given `--execute`, the version and that go-ahead.

## The item

| | |
| --- | --- |
| Item | [3842047795](https://steamcommunity.com/sharedfiles/filedetails/?id=3842047795), created 2026-11-18, public |
| Page title | "Realistic Gas Physics" (the user's; `mod.yaml` keeps "Mixed Atmospheres") |
| Made with | Klei's "Oxygen Not Included Uploader", Steam tool 636750; ONI (457140) consumes it |
| Form | One file, `mod_publish_data_file.zip`, held in the uploader's Steam Cloud |

`tools/mod_identity.py` holds these numbers.

The item is a *legacy single-file* item, and it has to stay one. ONI 744825's
`KMod.Steam.MakeMod` accepts a Workshop mod only when Steam reports a file, and
opens it as a ZIP; builds 763796 and 764025 also accept a folder. `steamcmd
+workshop_build_item` publishes a folder, so it would stop the mod on 744825,
which the package supports. The upload therefore goes the way Klei's uploader
goes: write the ZIP to the uploader's Steam Cloud, then point the item at it.

The ZIP holds the release folder's files at its root, with backslash
separators, deflated, without directory entries. That is the layout Klei's
uploader writes (compared with the published 1.12.2 and 1.10.1 files), and the
one every ONI mod on the Workshop is read from. The release ZIP on the share
(`MixedAtmospheres-<version>.zip`) has a `MixedAtmospheres/` top folder for
manual installs and is not the file to upload.

## What the Workshop serves

```sh
python3 tools/workshop.py status
python3 tools/workshop.py status --package /mnt/data/share/oni/releases/<version>/MixedAtmospheres
```

`status` reads Steam's public record of the item, downloads the published file
and compares every file in it with its own `package-manifest.json` and, with
`--package`, with a release folder. It needs no sign-in and changes nothing. It
exits 0 only when the published file is consistent and, if a folder is given,
equal to it file for file. Use it after an upload made by hand as well.

Uploads so far:

| Uploaded (UTC) | Version | Revision | How it is known |
| --- | --- | --- | --- |
| 2026-11-18 17:52 | 0.1.8 | `4bdfceb` | the item's creation time; version from the project record |
| 2026-11-20 02:07 | 1.10.1 | `7d738d1` | the private client's downloaded copy, checked against its manifest |
| 2026-11-20 | 1.10.2 | `f51be6a` | the user's statement |
| 2026-11-21 23:10 | 1.12.2 | `0da7afd` | `status` on 2026-11-22: 39 files as the manifest pins them |
| 2026-11-22 20:29 | 1.12.2.3 | `9c7bda6` | `status` on 2026-11-23: every file as the manifest pins it |
| 2026-11-24 02:53 | 1.13.3 | `6a116e5` | `status` on 2026-11-24: every file as the manifest pins it |
| 2026-11-24 23:01 | 1.13.5 | `993c3c3` | `status` on 2026-11-25: every file as the manifest pins it; uploaded by the user while its release chain was running |
| 2026-11-25 03:09 | 1.13.6 | `d63861d` | `status` on 2026-11-25: every file as the manifest pins it; uploaded by the user while its release chain was running |
| 2026-11-25 15:21 | 1.13.8 | `6dd5a59` | uploaded with `tools/workshop.py` at the user's request of that day (the tool's first upload); it printed `UPLOADED AND VERIFIED`, and `status --package` agrees file for file |
| 2026-11-25 20:02 | 1.13.9 | `6458df3` | uploaded with `tools/workshop.py` at the user's request of that day; `UPLOADED AND VERIFIED`, and `status --package` agrees file for file |
| 2026-11-26 21:47 | 1.13.10 | `32f6aca` | uploaded with `tools/workshop.py` by another session; the record of the 1.13.11 upload names it as the file it replaced |
| 2026-11-27 00:11 | 1.13.11 | `e09397d` | uploaded with `tools/workshop.py` at the user's request of 2026-11-26; the published file agrees with the release folder file for file |

The first four were uploaded by the user with Klei's uploader; this project's
record does not say how 1.12.2.3 and 1.13.3 were uploaded (1.13.3 went up
while its acceptance run was still in progress; [its
result](playtest-1.13.3.md#staged-package)). 1.13.7 was staged and never
uploaded; 1.13.8 contains it. Add a line for each later upload.

## Uploading a version

1. **Stage the release folder**: an unmarked `tools/package_release.py` package
   ([distribution](distribution.md#release-folder)) of a pushed revision, on
   the share under `releases/<version>/MixedAtmospheres`. A release test
   (`--release-test`) is refused.
2. **Write the change note** players will see on the item's Change Notes tab,
   as a plain text file beside the folder (`CHANGE-NOTE.txt`). Say what changed
   for a player and whether existing colonies keep loading.
3. **Dry run.** It checks the folder, reads what the Workshop serves, builds
   the upload file and writes a record; it contacts Steam only to read.

   ```sh
   python3 tools/workshop.py upload --package <folder> --note-file <folder>/../CHANGE-NOTE.txt
   ```

4. **Ask the user**, naming the version, the revision, the test status and the
   change note. An earlier go-ahead does not cover a later version.
5. **Open the online window** ([below](#the-online-window)) and check Steam:

   ```sh
   flock local/locks/game.lock python3 tools/workshop.py preflight --package <folder>
   ```

6. **Upload**, in the same window:

   ```sh
   flock local/locks/game.lock python3 tools/workshop.py upload --package <folder> \
     --note-file <folder>/../CHANGE-NOTE.txt \
     --execute --confirm <version> --authorized "<the user's words, with the date>"
   ```

   The tool repeats the checks, writes the file to the uploader's Steam Cloud,
   reads it back, updates the item with the change note, then waits until
   Steam's public record changes, downloads the published file and compares it
   with the folder. It prints `UPLOADED AND VERIFIED` and exits 0 only when
   they are equal file for file. Exit 3 (`UNCONFIRMED`) means Steam accepted
   the update but the public record had not changed within 15 minutes: run
   `status --package` later. Exit 2 is a refusal or a failure; `status` then
   says what the Workshop serves.
7. **Close the online window**, add the upload to the list above and to
   AGENTS.md, and tell the user. Subscribers receive the file when their Steam
   client next updates the item; the user's Windows game is the check of the
   Workshop copy itself (ONI's Mods screen shows the version).

The record of each run (`record.json`, the upload file, the downloaded
published files) is written to `artifacts/workshop/<time>-<version>` unless
`--record` names another folder.

### What the tool refuses

- A folder that is not a clean release: test packages, uncommitted sources, a
  file that differs from the manifest, an extra or missing file, a development
  or validation file, a `mod_info.yaml` that does not state the version for
  each supported game build.
- A revision that is not on `origin/main`.
- A version that is not newer than the published one, unless `--replace-newer`
  is given. Going back to the previous release after a bad upload is that
  case: upload its kept folder with `--replace-newer` and a change note that
  says so. Keep earlier release folders on the share: a rollback comes from
  there, not from Steam.
- A folder with `HOLD.txt` or `KNOWN-ISSUE.txt` beside it, unless
  `--override-hold` is given. Remove the file when its reason is gone.
- `--execute` without `--confirm <version>` and `--authorized "<text>"`.
- A Steam client that is offline, signed in with another account than the
  item's owner, without a licence for the uploader, with Steam Cloud off or
  with too little Steam Cloud space; an ONI process that is still running.

### What it leaves alone

The item's title, description, pictures, tags and visibility. The user edits
those on the item's Steam page. `docs/steam-community-draft.bbcode` is the
description's source.

## The online window

The upload runs through the private Steam client of the in-game harness
(`~/.local/share/oni-headless`), which is signed in with the user's account and
normally in [Offline Mode](steam-offline-playtesting.md). The tool types no
credentials and never signs in: if Steam asks for a sign-in, stop and ask the
user.

1. Wait until no acceptance run or game uses the harness, and tell other
   sessions. Going online or offline restarts Steam; a game that is running
   would lose it.
2. With the slot held (`flock local/locks/game.lock ...` for each step), choose
   **Steam → Go Online** in the private client (`tools/oni_headless.py input`
   and `screenshot`, or the desktop at `http://127.0.0.1:6080`). Friends
   presence stays offline: the tool sets it before it connects, as the harness
   does before every game.
3. Run `preflight`, then `upload`.
4. Choose **Steam → Go Offline** and check that the window shows OFFLINE MODE.

While the tool is connected, Steam counts the uploader tool as running on the
account, as it does when the user runs Klei's uploader.

On another machine, pass `--steam-home` (Linux: the home folder of the
signed-in client) and `--game-dir` (an ONI installation of that platform, for
its `libsteam_api`). On Windows the tool uses the running Steam client and
`steam_api64.dll`; that route has not been run.

## By hand

Klei's uploader on the user's Windows PC remains a complete alternative: copy
`releases/<version>/MixedAtmospheres` from the share to the PC, start "Oxygen
Not Included Uploader" (Steam library, Tools), select the existing item, point
it at the folder, enter the change note and publish. Then run `status
--package <folder>` here: it verifies a hand upload exactly as it verifies the
tool's.

## How the upload works

`workshop.py` loads the game's own `libsteam_api` and calls, as app 636750:

| Step | Steam call |
| --- | --- |
| Store the file | `ISteamRemoteStorage::FileWrite`, then `GetFileSize` and `FileRead` to compare |
| Open the update | `CreatePublishedFileUpdateRequest(item)` |
| Attach file and note | `UpdatePublishedFileFile`, `UpdatePublishedFileSetChangeDescription` |
| Commit | `CommitPublishedFileUpdate`, awaited through `ISteamUtils::GetAPICallResult` |

`preflight` makes only the reading calls: the signed-in account, its licence
for the uploader, Steam Cloud state and space, and Steam's own record of the
item (`GetPublishedFileDetails`), which also proves the asynchronous result
path that the commit uses.

## What has been verified

- `status`, `build` and the dry run of `upload`, against the published item
  and the staged 1.12.2 and 1.12.2.1 folders (2026-11-22).
- The checks and the order of the Steam calls, with a stand-in client
  (`tools/tests/test_workshop.py`); the two result structures have the sizes
  Steam declares for Linux and Windows.
- Every Steam function the tool calls is exported by the `libsteam_api.so` of
  ONI 764025.
- `preflight` against the private client in Offline Mode (2026-11-22): Steam
  accepts the process as the uploader (636750) although the tool is not
  installed there, the signed-in account has its licence and owns the item,
  and the uploader's Steam Cloud is on with its whole quota free. This client
  holds no copy of the upload file yet. The only blocker reported was Offline
  Mode, as intended.

- The whole procedure, on 2026-11-25 for 1.13.8: Steam → Go Online in the
  private client (no sign-in was asked for), `preflight` online (Steam's own
  record of the item, the first asynchronous call), `upload --execute` (the
  file written to the uploader's Steam Cloud and read back, the item updated
  with the change note, the public record changed within the same minute,
  the published file downloaded and equal to the folder file for file), then
  Steam → Go Offline and OFFLINE MODE shown again. The record is
  `artifacts/v1.13.8/6dd5a59/workshop-upload`.

The Windows route (`steam_api64.dll` with the user's own client) has not been
run; the upload by hand above remains available.
