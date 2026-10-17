# Linux native compatibility build

The inspected ONI Linux player requires glibc 2.35. Native libraries built on
the current development host require 2.38, so those host builds do not cover
the game's Linux baseline.

`tools/build_linux_compat.py` freezes the native sources and builds them in a
digest-pinned Ubuntu 22.04 image using GCC 11. It mounts that source snapshot
read-only, writes into a fresh output directory, runs the complete default
native suite and checks both shared libraries' required glibc symbols.
The snapshot contains native sources, CMake files and required generation/patch
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
directory for every native build so earlier evidence stays intact. The image
can be reused with `--skip-image-build` after its first build.

`compatibility-build.json` records every frozen source hash, source revision
and working-tree status, exact container image ID, native binary hashes,
required glibc versions and test completion. The package builder verifies the
supplied native binaries against that report and includes it as
`native-build-provenance.json`. This native build record remains separate
from the package assembly's current Git revision.

The first private `build-linux-ubuntu2204-v1` cohort passes all 13 native CTests
and all 89 host managed/native integration checks. Both shared libraries
require at most glibc 2.34, within the 2.35 target. It freezes native source
`61e8ef3` plus its recorded changes; later working-tree changes do not replace
that snapshot. This is native ABI and binary-floor evidence. Real-game checks
and all normal ownership, persistence and release gates still apply.

The second frozen cohort includes source through `9484f45`, including the
separate heat component from `988c507`. Both libraries still require at most
glibc 2.34 and all 13 native tests pass. Its actual Linux Unity gravity colony
(`automatic-admission-baseline-gravity-save-v1` and `...gravity-load-v1`) saves,
loads in a fresh process, autosaves again before its first new timestep with
the original pause depth, and matches all 120 resumed physical images exactly.
The original GUID/three duplicants, grid/rendering, outside native heat and
native compatibility cells pass. This tests the baseline-built libraries on
the current host; it does not claim a Unity run on an Ubuntu 22.04 host.
The provenance's `actual_game_test: false` describes the native build command;
the separate game reports retain the runtime evidence and exact package hashes.
