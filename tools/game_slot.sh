#!/bin/bash
# game_slot.sh low|high|whole CMD...
# Runs ONE in-game command (harness run, helper script) in the single game slot:
# takes local/locks/game.lock, then the CPU lock half (run_locked.sh), runs CMD,
# then waits until no game process remains and keeps the slot 90 s longer so the
# next game starts clean. Exit status is CMD's. Never chain several games under
# one call; release between games.
set -uo pipefail
T="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"; L="${MA_LOCK_DIR:-$T/../local/locks}"
part=${1:-}; shift || true
[ $# -gt 0 ] || { echo "usage: game_slot.sh low|high|whole CMD..." >&2; exit 2; }
mkdir -p "$L"
exec flock --close "$L/game.lock" bash -c '
  T="$1"; part="$2"; shift 2
  echo "[game-slot] start $(date -u +%H:%M:%S) pid $$: $*" >&2
  "$T/run_locked.sh" "$part" "$@"; rc=$?
  for i in $(seq 1 120); do pgrep -x OxygenNotInclud >/dev/null || break; sleep 1; done
  pgrep -x OxygenNotInclud >/dev/null && echo "[game-slot] WARNING: game process still present after 120 s" >&2
  sleep 90
  echo "[game-slot] released $(date -u +%H:%M:%S) rc=$rc" >&2
  exit $rc' game-slot "$T" "$part" "$@"
