# Version 1.13.12: newer game builds that change nothing the mod uses

Every game build so far turned the published mod off, even when the game
changed nothing the mod uses, because the game compiles its build number
into code the mod patches and, on Windows, into its simulator library. On
2026-11-26 the user asked: "please also make the mod not fail on update if
nothing the mod touches is changed. work on this ASAP and prioritize it over
the new features." 1.13.12 is 1.13.11 (744825, 763796, 764025, 764961) with
that rule. No solver arithmetic, no checkpoint bytes and no save format
change; colonies saved by 1.13.11 load in it.

## The rule

A game build newer than the newest build the package supports is accepted
when everything the mod uses is unchanged apart from the build number
itself. Otherwise the mod stays off, and its notice says that an update of
Mixed Atmospheres is needed and names what changed. Older builds and builds
between two supported ones stay refused as before. The full description is
in [tolerated newer builds](game-update-resilience.md#tolerated-newer-builds).

- **Who checks.** ONI loads the package root for any build from the newest
  supported one on, and an archived adapter for a build between two
  supported ones. Only the root checks a newer build.
- **Managed.** Each contract entry also records its form with the build
  number read as a placeholder (`BuildMasked`), only where the number
  occurs: on all four builds the same seven entries (`Game.Save`,
  `SaveGame.GetSaveHeader`, `LoadScreen.DoLoad(string)`, the named
  `KleiVersion.ChangeList`, ...). An entry that differs on a newer game is
  forgiven only when the game's form, with the running build number read
  the same way, equals that recorded reading. The exact comparison is
  unchanged and runs first.
- **Native.** A Windows profile also records where `AppVersion::GetRevision`
  keeps the revision number and a revision-masked identity, which leaves out
  those 4 bytes and the Authenticode signature. Klei signs its files since
  764961. A `SimDLL` that matches it is "carried": its hooks, layouts and
  fingerprints are those of the verified image. The Linux library has been
  the same file since 744825.
- **The pipe biology guard** also accepts, on a tolerated build, a
  token-independent hash of its five game methods. That hash is the same on
  all four builds and both platforms, so the all-identity pipe biology does
  not fall back with "Mixed pipes started with fallbacks" after every update.
- **Player.log, when accepted:** `Game build N is newer than the build this
  version was checked against (M); every part of the game the mod uses is
  unchanged (...); 7 entries differ only by the build number:` followed by
  the entries. On Windows it also logs `Game SimDLL carried from the
  verified image of build M (...)`. No notice is shown at the main menu.
- **When refused:** the notice gives both builds and the first changed parts
  by name (for example `SelectToolHoverTextCard.UpdateHoverElements`), and
  says that an update of Mixed Atmospheres is needed. If the only
  differences are other mods' patches, the other-mods notice is shown, and it
  now says that the game is newer than the build the mod was checked against
  but unchanged apart from the build number.

## Checks

- Managed adapter compiled against each of the four builds on both
  platforms; each contract verifies with 0 differences against its own game
  and carries `BuildMasked` on the same seven entries; `GameContract
  controls`: 15 of 15 synthetic changes refused and the five newer-build
  controls pass on all eight; the canonical pipe scheduling signature is the
  expected one on all eight.
- `tools/tests/test_newer_build_tolerance.py` (adapter compiled against 763796
  only): 764025 accepted on Linux and Windows with exactly the seven entries
  forgiven; 764961 refused on both naming
  `SelectToolHoverTextCard.UpdateHoverElements`.
- The game's own Linux Mono (`tools/game_mono_contract_check.py`): the same
  two results; `--other-mod` on 764961 (its own contract) and on 764025
  (763796's contract, the newer-build wording) attributes every difference
  to the other mod.
- Native identity: the C++ and Python implementations agree on shared
  fixtures (`native_oni_image_identity`, `test_oni_hook_profiles.py`);
  763796, 764025 and 764961 share the masked identity; a flipped code byte
  is refused. The Windows backend under Wine: each image verified exactly,
  and a backend built with profiles up to 763796 only carries 764025 and
  764961 from 763796.
- On the combined tree: native Linux ctest 96 of 96; the Windows cross
  build with its identity test under Wine; the Python suite (613 tests, 11
  skipped for absent inputs, plus the harness's newer-game control);
  `tools/DistributionTests` 101 checks.
- In the game, the four-build development packages on 764961 and 764025
  (Linux player, Windows player under Proton): PENDING.
- In the game, the positive control (newest adapter 763796 on the 764025
  game, `--newer-game-control`): PENDING.
- In the game, the negative control (newest adapter 764025 on the 764961
  game): PENDING.
- The release packager and the release steps: PENDING.
