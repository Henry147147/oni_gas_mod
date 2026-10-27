# Linux native compatibility build

The inspected ONI Linux player requires glibc 2.35. Native libraries built on
the current development host require 2.38, so host builds do not cover the
game's Linux baseline. The project requires a stricter glibc 2.34 ceiling
for packaged native libraries.

`tools/build_linux_compat.py` freezes the native sources and builds them in a
digest-pinned Ubuntu 22.04 image using GCC 11. It mounts the source snapshot
read-only, writes into a fresh output directory, runs the complete default
native suite and checks both shared libraries' required glibc symbols. The
snapshot contains native sources, CMake files and required generation/patch
scripts; licensed game files and private profiles are outside it.

```sh
python3 tools/build_linux_compat.py --output build-linux-ubuntu2204 --jobs 4
python3 tools/package_probe.py \
  --native build-linux-ubuntu2204/native/libmixed_atmospheres.so \
  --native-observer build-linux-ubuntu2204/native/libmixed_atmospheres_oni.so \
  --native-build-info build-linux-ubuntu2204/compatibility-build.json \
  --output artifacts/linux-compatible-probe
```

Build the normal managed adapter first, or supply its directory with
`--managed-dir`. Docker and host `readelf` are required. Use a fresh output
directory for every native build. Build and test parallelism both follow
`--jobs`. The image can be reused with
`--skip-image-build` after its first build.

`compatibility-build.json` records every frozen source hash, the source revision
and working-tree status (tracked files deleted in the working tree are skipped
and listed as `deleted_in_working_tree`), the exact container image ID, native binary hashes,
required glibc versions and test completion. Both the builder and supplied-provenance
package check enforce the 2.34 ceiling. Older reports declaring 2.35 are accepted
only when every actual recorded library requirement is at most 2.34.
The packaging helper tests run independently of test discovery order.
The package builder verifies the
supplied native binaries against that report and includes it as
`native-build-provenance.json`. This native build record is separate from the
package assembly's current Git revision, and its `actual_game_test: false`
describes the build command only.

Current status, 2026-10-27: the production owner, native storage heat ABI2 and
conduit energy-completion observer pass all 89 default CTests in Ubuntu 22.04
(`build-release-ubuntu2204-conduit`). Both shared libraries require at most
glibc 2.34. Earlier 55-test evidence remains historical. The shared geometry's
scaled square root replaces the `std::hypot` call that made an earlier observer
require 2.35.
Earlier reference-owner packages pass actual Linux Unity
colonies, saves, fresh-process loads, paused autosaves and exact continuation;
the current compatible pair also passes the seven-check drained-reservoir
source and cold/menu loading on the development Linux host
(`release-conduit-completion-compatible-source` / `compatible-load`). Its 24
held storage sections preserve one identical history hash. Broader production
game and complete continuation acceptance remain open.
No Unity run on an Ubuntu 22.04 host has been made. Real-game ownership,
persistence and release gates still apply.
