#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Ported from MIT ac3n64 04134cc. Direct children are required for kill/wait.
# ARES_BINARY selects a private emulator; unset keeps the shared default.
# TR64_ARES_HOST=<host> runs the same run on that machine instead:
# test/run/ares-remote.sh, same arguments, same output paths, own slots.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
. "$ROOT/test/run/xvfb.sh"
[[ $# == 4 ]] || { echo 'usage: ares.sh output-dir rom log timeout-seconds' >&2; exit 2; }
case "${TR64_ARES_HOST:-local}" in ''|local|localhost) ;; *) exec "$ROOT/test/run/ares-remote.sh" "$@" ;; esac
dir=$1 rom=$2 log=$3 timeout_s=$4
if [[ -d "$dir" ]]; then
 rtk rm -f -- "$dir/ares-host.txt" "$dir/slot.txt"
fi
[[ -z "${TR64_ARES_HW:-}" ]] || { echo 'ares.sh: TR64_ARES_HW=1 needs TR64_ARES_HOST=<remote-host> (the hardware-likeness ares is built there, tools/ares/README.md)' >&2; exit 2; }
[[ -z "${ARES_TEST_INPUT_2:-}" ]] || { echo 'ares.sh: pad 2 requires the private sampled-input variant on TR64_ARES_HOST=<remote-host>' >&2; exit 2; }
. "$ROOT/test/run/ares-options.sh"
binary=${ARES_BINARY-$HOME/src/ares/build/desktop-ui/ares}
[[ -x "$binary" ]] || { echo "ares missing: $binary" >&2; exit 2; }
rom_record=$(rtk proxy sha256sum -- "$rom") || { echo 'ares.sh: cannot hash ROM' >&2; exit 2; }
rom_sha=${rom_record%% *}
[[ "$rom_sha" =~ ^[0123456789abcdef]{64}$ ]] || { echo 'ares.sh: cannot hash ROM' >&2; exit 2; }
export TMPDIR="${TMPDIR:-/tmp/tr64/tmp}"
LOCKS_DIR="${TR64_ARES_LOCKS:-/tmp/tr64-locks}"
rtk mkdir -p "$dir" "$LOCKS_DIR" "$TMPDIR"
XVFB_PID= XVFB_DIR= pid= slotfd=
cleanup() {
 local child
 trap - EXIT INT TERM
 for child in "$pid" "$XVFB_PID"; do
  if [[ -n "$child" ]]; then
   # Before setsid, the child has no group of its own: kill its PID instead.
   kill -TERM -- "-$child" 2>/dev/null || kill -TERM "$child" 2>/dev/null || true
   wait "$child" 2>/dev/null || true
  fi
 done
 [[ -z "$XVFB_DIR" ]] || rtk rm -rf -- "$XVFB_DIR"
 [[ -z "$slotfd" ]] || { exec {slotfd}>&-; } 2>/dev/null || true
}
trap cleanup EXIT
trap 'cleanup; exit 130' INT
trap 'cleanup; exit 143' TERM
# Queue signal handling across fork/$! publication; a trap must never clean
# an unrecorded child. Exec resets these caught handlers in the children.
defer_signals() {
 launch_signal=0
 trap 'launch_signal=130' INT
 trap 'launch_signal=143' TERM
}
resume_signals() {
 trap 'cleanup; exit 130' INT
 trap 'cleanup; exit 143' TERM
 if [[ $launch_signal != 0 ]]; then cleanup; exit "$launch_signal"; fi
}
printf '%s\n' "$$" >"$dir/waiting.pid"
while :; do
 for slot in 0 1; do
  exec {slotfd}>>"$LOCKS_DIR/ares-slot$slot"
  if rtk flock -n "$slotfd"; then break 2; fi
  exec {slotfd}>&-
  slotfd=
 done
 rtk sleep 0.05
done
printf '%s\n' "$slot" >"$dir/slot.txt"
xvfb_start
start=$(rtk date +%s%N)
defer_signals
(
 cd "$dir"
 export DISPLAY="$DISPLAY_NUM" ARES_TEST_HARNESS=1 "${ARES_ENV[@]}"
 exec setsid timeout -k 3 "$timeout_s" "$binary" "${ARES_SETTINGS[@]}" "$rom"
) >"$log" 2>&1 &
# The emulator group intentionally inherits the lock: killing this shell
# without its trap must not allow a third emulator to start.
pid=$!
resume_signals
printf '%s\n' "$pid" >"$dir/ares.pid"
code=0
wait "$pid" || code=$?
pid=
elapsed=$(( $(rtk date +%s%N) - start ))
printf '%d.%03d\n' "$((elapsed/1000000000))" "$((elapsed%1000000000/1000000))" >"$dir/wall.txt"
[[ $(rtk proxy sha256sum -- "$rom") == "$rom_record" ]] || { echo 'ares.sh: ROM changed during run' >&2; exit 65; }
printf 'local rom_sha256=%s\n' "$rom_sha" >"$dir/ares-host.txt"
exit "$code"
