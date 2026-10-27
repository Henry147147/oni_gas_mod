#!/bin/bash
# Run a timed or in-game command under the shared CPU locks in local/locks.
#   run_locked.sh whole CMD...   both halves, unpinned (in-game runs, whole-machine timings)
#   run_locked.sh low CMD...     cores 0-11 only (long runs)
#   run_locked.sh high CMD...    cores 12-23 only (timed work beside a long run)
# Whole takes the low lock, then the high lock (fixed order: no deadlock).
# Close lock descriptors in commands: compiler/other daemons must not retain
# them after the measured or test command exits. Builds/tests during a timed
# window use these locks too.
set -euo pipefail
T="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"; L="${MA_LOCK_DIR:-$T/../local/locks}"
part=${1:-}; shift || true
[ $# -gt 0 ] || { echo "usage: run_locked.sh whole|low|high CMD..." >&2; exit 2; }
mkdir -p "$L"
case "$part" in
  whole) exec flock --close "$L/cores-0-11.lock" flock --close "$L/cores-12-23.lock" "$@" ;;
  low)   exec flock --close "$L/cores-0-11.lock" taskset -c 0-11 "$@" ;;
  high)  exec flock --close "$L/cores-12-23.lock" taskset -c 12-23 "$@" ;;
  *) echo "usage: run_locked.sh whole|low|high CMD..." >&2; exit 2 ;;
esac
