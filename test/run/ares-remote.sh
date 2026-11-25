#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Ported from MIT hl64 test/run/ares-remote.sh (the same author's sibling project).
#
# test/run/ares.sh's run on another machine (TR64_ARES_HOST=<remote-host>), results
# brought back to the paths a local run writes, so the harness scripts
# (scene.sh --ares, m0.sh --ares, fatal-tests.py, tools/ship/boot.py) cannot
# tell the two apart. ares.sh execs this script when TR64_ARES_HOST is set.
#
#   test/run/ares-remote.sh DIR ROM LOG TIMEOUT   ares.sh's contract
#   test/run/ares-remote.sh --check               the host answers, its ares exists
#
# A run: ROM (relative paths: to DIR), the ARES_TEST_INPUT file and the
# ARES_SRAM_IN image go to HOST:~/tr64-runs/<id>/ (the ROM as a delta against
# the last ROM sent, compared by checksum). The host verifies the staged ROM's
# SHA-256 before launching ares in <id>/run on its own Xvfb with
# test/run/ares-options.sh's options and the forwarded ARES_* variables, after
# taking one of TR64_ARES_REMOTE_SLOTS (default 6) slot locks
# ~/tr64-runs/.slot<i> (independent of this machine's two slots), its
# deadline starting then, as locally. While it runs the log streams into LOG
# and every value written to the ARES_TEST_INPUT file is applied on the host
# in order (live pad input; see below); SIGTERM / SIGINT here stops ares there (SRAM export on TERM
# included) and still copies the results back, exit 143 / 130. Then ares'
# working directory (ares-test-*.ppm) comes back into DIR, its output to
# LOG, ARES_HOST_SHOT_DIR's pictures to that directory, ARES_SRAM_OUT to its
# path. Host paths in the log are written as the local paths, so the log is
# the one a local run writes. Files a local run leaves in DIR that are
# local process state (ares.pid, xvfb.pid, xvfb.dir) are not written; slot.txt
# (the remote slot index, written when the slot is taken), waiting.pid and
# wall.txt are, and ares-host.txt names the host, its load and the verified ROM
# SHA-256 (rom_sha256=) for a run that reached ares.
# Exit: ares' status (124 timeout), 2 configuration, 65 ROM SHA-256 mismatch,
# 255 the host failed.
#
# TR64_SHOT_PRUNE=1 (set by tools/ship/boot.py and drive.py, which thin the
# pictures with their ShotPruner while the run goes): the host thins its
# pictures the same way (test/run/ares-prune.py) before they are copied back.
# Variables forwarded: ARES_EXPANSION_PAK, ARES_HOST_SCREENSHOT,
# ARES_HOST_SHOT_EVERY_MS, ARES_HOST_SHOT_MAX, ARES_HOST_PRINTF, ARES_TRACE_CPU;
# file-valued (staged / fetched): ARES_TEST_INPUT, ARES_SRAM_IN, ARES_SRAM_OUT,
# ARES_HOST_SHOT_DIR. ARES_BINARY, ARES_TIMEOUT and ARES_TEST_HARNESS are not
# forwarded (the host's own private ares, tools/ares/README.md); any other
# ARES_* variable stops the run (exit 2) rather than run differently.
# Env: TR64_ARES_HOST (non-empty, not local), TR64_ARES_SSH (ssh target,
#      default $TR64_ARES_HOST),
#      TR64_ARES_REMOTE_SLOTS (default 6), TR64_ARES_REMOTE_ICD (the Vulkan
#      ICD file ares' RDP uses there, default lvp_icd.json: llvmpipe, as the
#      local runs).
# Host setup (once): tools/ares/build-remote.sh (~/tr64-ares). TR64_ARES_HW=1 uses ~/tr64-ares-hw instead
# (ARES_PATCHES="local-tree sram-persistence hw-likeness" tools/ares/build-remote.sh tr64-ares-hw), with
# ARES_HW_LIKE=$TR64_ARES_HW_LIKE (default fetch; icache is opt-in, unconfirmed) and the interpreter; see tools/ares/README.md.
#
# Pad input is lossless and ordered. The ARES_TEST_INPUT file holds only the latest value, so a value
# replaced before the emulator read it (a press and its release written within a poll interval, or
# delivered in one burst after a stall of the connection) is lost: that is how a press goes missing under
# load. The caller therefore also appends every value it writes, one line each, to the journal
# ARES_TEST_INPUT.seq (tools/ship/drive.py pad_start / pad_write; the journal must exist, or the run stops
# with exit 2). This side forwards the journal from byte 0, none skipped; the host applies each line in turn
# and keeps it until the cart has logged two more `play-frame:` lines (the cart has ticked twice with the
# value on the pad, so it has read it) or PAD_DWELL_POLLS * 0.05 s passed (a level load or a film logs no
# ticks: the next value is then not held back for ever).
# ARES_TEST_INPUT_2 enables the private sampled-input variant. The primary
# .seq path then carries a versioned global journal (port, word, axes);
# a full controller response must acknowledge each entry before the next.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KEEP_RUNS=50
PAD_DWELL_POLLS=200
SHA256_RE='^[0123456789abcdefABCDEF]{64}$'

rom_sha256() {
 local digest
 digest=$(sha256sum <"$1") || return 1
 digest=${digest%  -} # sha256sum on stdin prints the digest followed by "  -"
 [[ "$digest" =~ $SHA256_RE ]] || return 1
 printf '%s\n' "${digest,,}"
}

# ---- the host's pad writer: ares-remote.sh --pad-side ID NAME (journal lines on stdin) ----
if [[ "${1:-}" == --pad-side ]]; then
 R="$HOME/tr64-runs/$2" name=$3 log="$HOME/tr64-runs/$2/ares.log"
 while [[ ! -e "$R/exit" ]] && IFS= read -r l; do
  [[ -n "$l" ]] || continue
  from=$(stat -c %s "$log" 2>/dev/null || echo 0)
  printf '%s\n' "$l" >"$R/input/.pad.tmp" && mv -f "$R/input/.pad.tmp" "$R/input/$name"
  for ((i = 0; i < PAD_DWELL_POLLS; ++i)); do
   [[ ! -e "$R/exit" ]] || break
   n=$(tail -c "+$((from + 1))" "$log" 2>/dev/null | grep -c '^play-frame: ' || true)
   (( n < 2 )) || break
   sleep 0.05
  done
 done
 exit 0
fi

if [[ "${1:-}" == --sampled-pad-side ]]; then
 R="$HOME/tr64-runs/$2" first=$3 second=$4 identity=$5
 result=0
 python3 "$R/bin/pad_relay.py" relay "$R/input/$first" "$R/input/$second" \
  "$R/run/pad-samples.txt" "$R/pad-started" "$R/exit" "$R/run/pad-windows.txt" "$identity" || result=$?
 if ((result)); then
  if [[ ! -e "$R/exit" && -f "$R/side.pid" ]]; then kill -USR1 "$(cat "$R/side.pid")" 2>/dev/null || true; fi
 fi
 exit "$result"
fi

# ---- the host side: ares-remote.sh --remote-side ID TIMEOUT ROM ROM_SHA [VAR=VALUE ...] ----
if [[ "${1:-}" == --remote-side ]]; then
 id=$2 to=$3 rom=$4 expected_sha=$5
 shift 5
 R="$HOME/tr64-runs/$id"
 ARES_DIR=tr64-ares
 for kv in "$@"; do export "${kv?}"; done
 # The hardware-likeness lane (TR64_ARES_HW=1): its own build, ARES_HW_LIKE for the CPU, the interpreter.
 [[ -z "${TR64_ARES_HW:-}" ]] || { ARES_DIR=tr64-ares-hw; export ARES_HW_LIKE="${TR64_ARES_HW_LIKE:-fetch}"; }
 if [[ -n "${ARES_TEST_INPUT_2:-}" ]]; then ARES_DIR=tr64-ares-coop; fi
 ARES="$HOME/$ARES_DIR/build/desktop-ui/ares"
 # shellcheck source=ares-options.sh
 . "$R/bin/ares-options.sh"
 stop=0 relay_stop=0 pid= xpid= slotfd= c=0
 trap 'stop=1; [[ -z "$pid" ]] || kill -TERM -- "-$pid" 2>/dev/null || true' TERM INT
 trap 'if [[ -n "$pid" ]] && kill -TERM -- "-$pid" 2>/dev/null; then relay_stop=1; fi' USR1
 echo "$$" >"$R/side.pid"
 finish() {
  echo "$c" >"$R/exit"
 }
 # The other side needs an exit file whatever happens here: a failure of this script is the host's (255), not ares' status.
 trap 'rc=$?; if [[ ! -e "$R/exit" ]]; then echo "ares-remote: host side failed (status $rc)" >&2; [[ -e "$R/wall" ]] || echo "0.000 - $(hostname -s)" >"$R/wall"; echo 255 >"$R/exit"; fi' EXIT
 [[ "$expected_sha" =~ $SHA256_RE ]] \
  || { echo "ares-remote: invalid expected ROM SHA-256" >&2; exit 255; }
 actual_sha=$(rom_sha256 "$R/rom/$rom") \
  || { echo "ares-remote: cannot hash staged ROM $rom" >&2; exit 255; }
 if [[ "$actual_sha" != "${expected_sha,,}" ]]; then
  printf 'ares-remote: ROM SHA-256 mismatch for %s (expected %s, got %s)\n' "$rom" "$expected_sha" "$actual_sha" >&2
  echo "0.000 - $(hostname -s)" >"$R/wall"; c=65; finish; exit "$c"
 fi
 if [[ -n "${ARES_TEST_INPUT_2:-}" ]]; then
  install="$HOME/$ARES_DIR"
  expected_ares=${TR64_EXPECT_ARES_ID:-}
  # Bind the checked executable by inode before taking a slot. A rebuild
  # replaces the installed inode; it cannot replace this run's binary.
  declared= digest= extra=
  if [[ -x "$ARES" && "$expected_ares" =~ $SHA256_RE && -r "$install/sampled-identity.txt" ]] &&
     read -r declared digest extra <"$install/sampled-identity.txt" &&
     [[ "$declared" == "$expected_ares" && "$digest" =~ $SHA256_RE && -z "$extra" ]] &&
     ln -L -- "$ARES" "$R/sampled-ares" &&
     [[ "$(rom_sha256 "$R/sampled-ares")" == "$digest" ]] &&
     strings "$R/sampled-ares" | grep -Fx "TR64 sampled ares $expected_ares" >/dev/null; then
   ARES="$R/sampled-ares"
   printf 'ares_identity=%s ares_sha256=%s\n' "$declared" "$digest" >"$R/ares-identity"
  else
   echo 'ares-remote: sampled ares missing or mismatched; rebuild with ARES_PATCHES="local-tree sram-persistence two-pad" tools/ares/build-remote.sh tr64-ares-coop' >&2
   echo "0.000 - $(hostname -s)" >"$R/wall"; c=67; finish; exit "$c"
  fi
 fi
 [[ -x "$ARES" ]] || {
  echo "ares missing: $ARES (tools/ares/build-remote.sh)" >&2
  echo "0.000 - $(hostname -s)" >"$R/wall"; c=2; finish; exit 2
 }
 [[ -z "${ARES_TEST_INPUT:-}" ]] || export ARES_TEST_INPUT="$R/input/$ARES_TEST_INPUT"
 [[ -z "${ARES_TEST_INPUT_2:-}" ]] || export ARES_TEST_INPUT_2="$R/input/$ARES_TEST_INPUT_2"
 [[ -z "${ARES_SRAM_IN:-}" ]] || export ARES_SRAM_IN="$R/sram/in.srm"
 [[ -z "${ARES_SRAM_OUT:-}" ]] || { mkdir -p "$R/sram"; export ARES_SRAM_OUT="$R/sram/out.srm"; }
 [[ -z "${ARES_HOST_SHOT_DIR:-}" ]] || { mkdir -p "$R/shots"; export ARES_HOST_SHOT_DIR="$R/shots"; }
 mkdir -p "$R/run"
 # keep the newest KEEP_RUNS run directories, this one included (ids sort by start time)
 # (another run's cleanup may remove a directory while this lists them: not an error)
 { find "$HOME/tr64-runs" -mindepth 1 -maxdepth 1 -type d -name '2*' -printf '%f\n' 2>/dev/null | sort -r \
  | tail -n +$((KEEP_RUNS + 1)) | while read -r old; do rm -rf "${HOME:?}/tr64-runs/$old"; done; } || true
 # a slot: wait until one of the lock files is free
 slots=${TR64_ARES_REMOTE_SLOTS:-6} got=
 while [[ $stop == 0 && -z "$got" ]]; do
  for ((i = 0; i < slots; i++)); do
   exec {slotfd}>>"$HOME/tr64-runs/.slot$i"
   if flock -n "$slotfd"; then got=$i; break; fi
   exec {slotfd}>&-
  done
  [[ -n "$got" ]] || sleep 0.2
 done
 if [[ -z "$got" ]]; then
  echo "ares-remote: run $id stopped before it started" >&2
  echo "0.000 - $(hostname -s)" >"$R/wall"; c=143; finish; exit 143
 fi
 echo "ares-remote: slot $got on $(hostname -s)" >&2
 xd="$(mktemp -d "${TMPDIR:-/tmp}/tr64-xvfb.XXXXXX")"
 mkfifo "$xd/display"
 # The emulator group inherits the slot lock; the X server must not.
 setsid Xvfb -displayfd 3 -screen 0 1280x800x24 3>"$xd/display" >"$xd/log" 2>&1 {slotfd}>&- &
 xpid=$!
 if ! read -r -t 15 n <"$xd/display"; then
  echo 'Xvfb did not report a private display' >&2
  kill -TERM -- "-$xpid" 2>/dev/null || true; rm -rf "$xd"; c=2; finish; exit 2
 fi
 # the copy of the settings every run starts from (ares rewrites it at exit)
 cp -a "$HOME/$ARES_DIR/xdg" "$R/xdg"
 start=$(date +%s%N)
 if [[ -n "${ARES_TEST_INPUT_2:-}" ]]; then
  export TR64_ARES_SAMPLE_LOG="$R/run/pad-samples.txt"
  : >"$TR64_ARES_SAMPLE_LOG"
 fi
 (
  cd "$R/run"
  export DISPLAY=":$n" ARES_TEST_HARNESS=1 "${ARES_ENV[@]}" XDG_DATA_HOME="$R/xdg" \
   VK_ICD_FILENAMES="/usr/share/vulkan/icd.d/${TR64_ARES_REMOTE_ICD:-lvp_icd.json}"
  exec setsid timeout -k 3 "$to" "$ARES" "${ARES_SETTINGS[@]}" "$R/rom/$rom"
 ) >"$R/ares.log" 2>&1 &
 pid=$!
 [[ -z "${ARES_TEST_INPUT_2:-}" ]] || : >"$R/pad-started"
 # the log on stdout while ares runs: the other side's LOG
 tail -n +1 -F --pid="$pid" "$R/ares.log" 2>/dev/null &
 tpid=$!
 sample_pid=
 if [[ -n "${ARES_TEST_INPUT_2:-}" ]]; then
  tail -n +1 -s .1 -F --pid="$pid" "$TR64_ARES_SAMPLE_LOG" >&2 2>/dev/null &
  sample_pid=$!
 fi
 ppid=
 if [[ -n "${TR64_SHOT_PRUNE:-}" ]]; then
  python3 "$R/bin/ares-prune.py" "$R/ares.log" "$pid" &
  ppid=$!
 fi
 if (( stop )); then kill -TERM -- "-$pid" 2>/dev/null || true; fi
 while :; do
  c=0
  wait "$pid" || c=$?
  kill -0 "$pid" 2>/dev/null || break
 done
 pid=
 # A relay-triggered shutdown is not the emulator's verdict. The client
 # waits for the relay and will return its actual reason (66 or 67).
 if (( relay_stop && !stop && (c == 124 || c == 143 || c == 137) )); then c=0; fi
 elapsed=$(( $(date +%s%N) - start ))
 wait "$tpid" 2>/dev/null || true
 [[ -z "$sample_pid" ]] || wait "$sample_pid" 2>/dev/null || true
 [[ -z "$ppid" ]] || wait "$ppid" || true
 kill -TERM -- "-$xpid" 2>/dev/null || true
 rm -rf "$xd"
 exec {slotfd}>&-
 printf '%d.%03d %s %s rom_sha256=%s\n' "$((elapsed/1000000000))" "$((elapsed%1000000000/1000000))" "$(cut -d' ' -f1 /proc/loadavg)" "$(hostname -s)" "$actual_sha" >"$R/wall"
 finish
 # the next run's ROM delta basis
 mkdir -p "$HOME/tr64-runs/.basis/rom"
 ln -f "$R/rom/$rom" "$HOME/tr64-runs/.basis/rom/.$rom.$$" \
  && mv -f "$HOME/tr64-runs/.basis/rom/.$rom.$$" "$HOME/tr64-runs/.basis/rom/$rom"
 exit "$c"
fi

# ---- this side ----
HOST="${TR64_ARES_HOST:?set TR64_ARES_HOST to the remote emulator host}"
SSH_TO="${TR64_ARES_SSH:-$HOST}"
SSH=(ssh -o BatchMode=yes -o ServerAliveInterval=30 -o ServerAliveCountMax=10)
export RSYNC_RSH="${SSH[*]}"

if [[ "${1:-}" == --check ]]; then
 AD=tr64-ares; [[ -z "${TR64_ARES_HW:-}" ]] || AD=tr64-ares-hw
 "${SSH[@]}" -n "$SSH_TO" "test -x ~/$AD/build/desktop-ui/ares && command -v Xvfb >/dev/null" \
  || { echo "ares-remote: $SSH_TO unreachable, or no ~/$AD/build/desktop-ui/ares (tools/ares/build-remote.sh) / Xvfb there" >&2; exit 2; }
 exit 0
fi

[[ $# == 4 ]] || { echo 'usage: ares-remote.sh output-dir rom log timeout-seconds | --check' >&2; exit 2; }
mkdir -p "$1"
DIR="$(cd "$1" && pwd)"
rm -f -- "$DIR/ares-host.txt" "$DIR/ares-identity.txt" "$DIR/slot.txt" \
 "$DIR/pad-relay-status.txt" "$DIR/pad-windows.txt" "$DIR/pad-samples.txt" "$DIR/pad-samples.live.txt"
abs() { case "$1" in /*) echo "$1" ;; *) echo "$DIR/$1" ;; esac; }
ROM="$(abs "$2")"
case "$3" in /*) LOG="$3" ;; *) LOG="$PWD/$3" ;; esac   # as ares.sh: relative to the caller
TO=$4
[[ -f "$ROM" ]] || { echo "ares-remote: no ROM $ROM" >&2; exit 2; }
ROM_SHA=$(rom_sha256 "$ROM") || { echo "ares-remote: cannot hash ROM $ROM" >&2; exit 2; }
printf '%s\n' "$$" >"$DIR/waiting.pid"
ID="$(date -u +%Y%m%d-%H%M%S)-$(hostname -s)-$$-$RANDOM"
RR="tr64-runs/$ID"

# ---- environment: forwarded, staged, or refused ----
STOP=0
ST="$(mktemp -d "${TMPDIR:-/tmp}/ares-remote.XXXXXX")"
trap 'rm -rf "$ST"' EXIT
mkdir -p "$ST/bin" "$ST/rom"
for f in ares-remote.sh ares-options.sh ares-prune.py; do ln -s "$HERE/$f" "$ST/bin/$f"; done
ln -s "$HERE/../../tools/ship/drive.py" "$ST/bin/drive.py"
ln -s "$ROM" "$ST/rom/${ROM##*/}"
FWD=() PADIN= PADIN2= SRAM_OUT= SHOTS=
for v in $(compgen -e | grep '^ARES_' || true); do
 case "$v" in
  ARES_BINARY | ARES_TIMEOUT | ARES_TEST_HARNESS | ARES_TEST_INPUT | ARES_TEST_INPUT_2 | ARES_SRAM_IN | ARES_SRAM_OUT | ARES_HOST_SHOT_DIR) ;;
  ARES_EXPANSION_PAK | ARES_HOST_SCREENSHOT | ARES_HOST_SHOT_EVERY_MS | ARES_HOST_SHOT_MAX | ARES_HOST_PRINTF | ARES_TRACE_CPU)
   FWD+=("$v=${!v}") ;;
  *) echo "ares-remote: $v cannot be forwarded to $SSH_TO; unset it or run locally (TR64_ARES_HOST unset)" >&2; exit 2 ;;
 esac
done
if [[ -n "${ARES_TEST_INPUT:-}" ]]; then
 PADIN="$(abs "$ARES_TEST_INPUT")"
 [[ -f "$PADIN" ]] || { echo "ares-remote: no ARES_TEST_INPUT file $PADIN" >&2; exit 2; }
 if [[ -z "${ARES_TEST_INPUT_2:-}" && ! -f "$PADIN.seq" ]]; then
  echo "ares-remote: no pad journal $PADIN.seq (tools/ship/drive.py pad_start / pad_write)" >&2; exit 2
 fi
 mkdir -p "$ST/input"
 cp "$PADIN" "$ST/input/${PADIN##*/}"
 FWD+=("ARES_TEST_INPUT=${PADIN##*/}")
fi
if [[ -n "${ARES_TEST_INPUT_2:-}" ]]; then
 PADIN2="$(abs "$ARES_TEST_INPUT_2")"
 [[ -n "$PADIN" && -z "${TR64_ARES_HW:-}" ]] || { echo "ares-remote: pad 2 also requires pad 1; the hardware-likeness variant is separate" >&2; exit 2; }
 [[ -f "$PADIN2" ]] || { echo "ares-remote: pad-unsampled port=2: missing pad file $PADIN2" >&2; exit 66; }
 python3 "$HERE/../../tools/ares/pad_relay.py" journal "$PADIN.seq" || exit $?
 first=${PADIN##*/} second=${PADIN2##*/}
 [[ "$first" != "$second" && "$first.tmp" != "$second" && "$second.tmp" != "$first" ]] ||
  { echo "ares-remote: pad filenames and staging names must be distinct" >&2; exit 2; }
 # Unnumbered idle until the relay supplies entry 1, never a newer value
 # copied out of the caller's live file while the ROM is being staged.
 printf '0 0 0\n' >"$ST/input/${PADIN##*/}"
 printf '0 0 0\n' >"$ST/input/${PADIN2##*/}"
 ln -s "$HERE/../../tools/ares/pad_relay.py" "$ST/bin/pad_relay.py"
 . "$HERE/../../tools/ares/identity.sh"
 sampled_id=$(sampled_ares_identity "$HERE/../../tools/ares")
 [[ "$sampled_id" =~ $SHA256_RE ]] || { echo 'ares-remote: cannot compute sampled ares identity' >&2; exit 2; }
 FWD+=("ARES_TEST_INPUT_2=${PADIN2##*/}" "TR64_EXPECT_ARES_ID=$sampled_id")
fi
if [[ -n "${ARES_SRAM_IN:-}" ]]; then
 [[ -f "$(abs "$ARES_SRAM_IN")" ]] || { echo "ares-remote: no ARES_SRAM_IN file $(abs "$ARES_SRAM_IN")" >&2; exit 2; }
 mkdir -p "$ST/sram"
 ln -s "$(abs "$ARES_SRAM_IN")" "$ST/sram/in.srm"
 FWD+=("ARES_SRAM_IN=1")
fi
if [[ -n "${ARES_SRAM_OUT:-}" ]]; then
 SRAM_OUT="$(abs "$ARES_SRAM_OUT")"
 FWD+=("ARES_SRAM_OUT=1")
fi
if [[ -n "${ARES_HOST_SHOT_DIR:-}" ]]; then
 SHOTS="$(abs "$ARES_HOST_SHOT_DIR")"
 FWD+=("ARES_HOST_SHOT_DIR=1")
fi
[[ -z "${TR64_SHOT_PRUNE:-}" ]] || FWD+=("TR64_SHOT_PRUNE=1")
[[ -z "${TR64_ARES_REMOTE_SLOTS:-}" ]] || FWD+=("TR64_ARES_REMOTE_SLOTS=$TR64_ARES_REMOTE_SLOTS")
[[ -z "${TR64_ARES_REMOTE_ICD:-}" ]] || FWD+=("TR64_ARES_REMOTE_ICD=$TR64_ARES_REMOTE_ICD")
[[ -z "${TR64_ARES_HW:-}" ]] || FWD+=("TR64_ARES_HW=1" "TR64_ARES_HW_LIKE=${TR64_ARES_HW_LIKE:-fetch}")

stop_remote() {
 "${SSH[@]}" -n "$SSH_TO" "cd $RR && for i in 1 2 3 4 5 6 7 8 9 10; do
  if [ -s side.pid ]; then kill -TERM \$(cat side.pid) 2>/dev/null; break; fi
  [ -e exit ] && break; sleep 1; done" || true
}
# A signal stops the run on the host and the copy-back still happens; the
# status is the signal's, as ares.sh's trap exits 128+N.
STOPCODE=0 RUNNING=0
trap 'STOPCODE=143; STOP=1; [[ $RUNNING == 0 ]] || stop_remote' TERM
trap 'STOPCODE=130; STOP=1; [[ $RUNNING == 0 ]] || stop_remote' INT

RHOME="$("${SSH[@]}" -n "$SSH_TO" 'mkdir -p tr64-runs/.basis && printf %s "$HOME"')" \
 || { echo "ares-remote: $SSH_TO unreachable" >&2; exit 255; }
# Builds with the same pack can have identical basename, size and mtime.
# Keep the ROM basis for delta transfer, but never skip a transfer on those alone.
rsync -rtLz --checksum --mkpath --copy-dest=../.basis "$ST/" "$SSH_TO:$RR/" \
 || { [[ $STOP == 0 ]] || exit "$STOPCODE"; echo "ares-remote: copy to $SSH_TO:$RR failed" >&2; exit 255; }
[[ $STOP == 0 ]] || exit "$STOPCODE"

# the host's paths in the log are this machine's
esc() { printf '%s' "$1" | sed -e 's/[][\\.*^$|&/]/\\&/g'; }
resc() { printf '%s' "$1" | sed -e 's/[\\|&]/\\&/g'; }
SEDX=()
[[ -z "$SHOTS" ]] || SEDX+=(-e "s|$(esc "$RHOME/$RR/shots")|$(resc "$SHOTS")|g")
[[ -z "$SRAM_OUT" ]] || SEDX+=(-e "s|$(esc "$RHOME/$RR/sram/out.srm")|$(resc "$SRAM_OUT")|g")
[[ -z "${ARES_SRAM_IN:-}" ]] || SEDX+=(-e "s|$(esc "$RHOME/$RR/sram/in.srm")|$(resc "$(abs "$ARES_SRAM_IN")")|g")
[[ -z "$PADIN" ]] || SEDX+=(-e "s|$(esc "$RHOME/$RR/input/${PADIN##*/}")|$(resc "$PADIN")|g")
[[ -z "$PADIN2" ]] || SEDX+=(-e "s|$(esc "$RHOME/$RR/input/${PADIN2##*/}")|$(resc "$PADIN2")|g")
rewrite() { if [[ ${#SEDX[@]} == 0 ]]; then cat; else sed -u "${SEDX[@]}"; fi; }

# the run; its log streams into LOG meanwhile (as a local run's grows). The
# pipeline ignores TERM / INT: a group-wide TERM must not cut the stream, the
# host side ends by itself once stopped. Messages on stderr: the host's slot
# line becomes slot.txt, the rest is passed on.
args=(bash "$RR/bin/ares-remote.sh" --remote-side "$ID" "$TO" "${ROM##*/}" "$ROM_SHA" "${FWD[@]}")
slot_watch() {
 local l
 while IFS= read -r l; do
  if [[ "$l" =~ ^ares-remote:\ slot\ ([0-9]+)\ on ]]; then printf '%s\n' "${BASH_REMATCH[1]}" >"$DIR/slot.txt"; fi
  if [[ -n "$PADIN2" && ( "$l" == "TR64 sampled ares "* || "$l" == "TESTHARNESS pad-sampled "* ) ]]; then
   printf '%s\n' "$l" >>"$DIR/pad-samples.live.txt"; continue
  fi
  printf '%s\n' "$l" >&2
 done
}
RUNNING=1
(
 trap '' TERM INT
 "${SSH[@]}" -n "$SSH_TO" "$(printf '%q ' "${args[@]}")" 2> >(slot_watch) | rewrite >"$LOG"
) &
RPID=$!
PAD_PID=
pad_fwd() (
 set +o pipefail # The ssh writer, not the FIFO feeder, owns the verdict.
 local off=0 size side
 if [[ -n "$PADIN2" ]]; then
  side="--sampled-pad-side $ID $(printf '%q ' "${PADIN##*/}" "${PADIN2##*/}" "$sampled_id")"
 else side="--pad-side $ID $(printf %q "${PADIN##*/}")"
 fi
 while [[ ! -e "$ST/done" ]]; do
  size=$(stat -c %s "$PADIN.seq")
  if (( size > off )); then
   { tail -c "+$((off + 1))" "$PADIN.seq" | head -c "$((size - off))"; } || true
   off=$size
  fi
  sleep 0.03
 done | "${SSH[@]}" "$SSH_TO" "bash $RR/bin/ares-remote.sh $side"
)
if [[ -n "$PADIN" ]]; then
 pad_fwd &
 PAD_PID=$!
fi
while :; do
 wait "$RPID" || true
 kill -0 "$RPID" 2>/dev/null || break
done
: >"$ST/done"
PAD_STATUS=0
if [[ -n "$PAD_PID" ]]; then
 if [[ -n "$PADIN2" ]]; then
  # While waiting, wait may return on a signal. The receipt must be the
  # forwarder's terminal result, not the status of an interrupted wait.
  while :; do
   PAD_STATUS=0; wait "$PAD_PID" || PAD_STATUS=$?
   kill -0 "$PAD_PID" 2>/dev/null || break
  done
  printf '%s\n' "$PAD_STATUS" >"$DIR/pad-relay-status.txt"
 else
  wait "$PAD_PID" 2>/dev/null || true
 fi
fi
RUNNING=0

# results: ares' working directory into DIR, its output to LOG (the streamed
# copy made exact), the pictures, the SRAM
mkdir -p "$ST/back"
rsync -t "$SSH_TO:$RR/wall" "$SSH_TO:$RR/exit" "$ST/back/" 2>/dev/null \
 || { echo "ares-remote: run $ID on $SSH_TO left no result" >&2; exit 255; }
if "${SSH[@]}" -n "$SSH_TO" "test -f $RR/ares.log"; then
 rsync -t "$SSH_TO:$RR/ares.log" "$ST/back/ares.log" || { echo "ares-remote: copy back of the log failed" >&2; exit 255; }
 rewrite <"$ST/back/ares.log" >"$LOG.new"
 mv -f "$LOG.new" "$LOG"
 rsync -rtz "$SSH_TO:$RR/run/" "$DIR/" || { echo "ares-remote: copy back from $SSH_TO:$RR/run failed" >&2; exit 255; }
 if [[ -n "$SHOTS" ]]; then
  mkdir -p "$SHOTS"
  rsync -rtz "$SSH_TO:$RR/shots/" "$SHOTS/" || { echo "ares-remote: copy back of the pictures failed" >&2; exit 255; }
 fi
 if [[ -n "$SRAM_OUT" ]] && "${SSH[@]}" -n "$SSH_TO" "test -f $RR/sram/out.srm"; then
  mkdir -p "$(dirname "$SRAM_OUT")"
  rsync -t "$SSH_TO:$RR/sram/out.srm" "$SRAM_OUT" || { echo "ares-remote: copy back of the SRAM failed" >&2; exit 255; }
 fi
fi
read -r wall load host rom_record <"$ST/back/wall"
printf '%s\n' "$wall" >"$DIR/wall.txt"
[[ -z "$rom_record" ]] || printf '%s load=%s wall=%s %s\n' "$host" "$load" "$wall" "$rom_record" >"$DIR/ares-host.txt"
if [[ -n "$PADIN2" && -n "$rom_record" ]]; then
 rsync -t "$SSH_TO:$RR/ares-identity" "$DIR/ares-identity.txt" ||
  { echo 'ares-remote: missing sampled ares identity receipt' >&2; exit 255; }
fi
[[ $STOP == 0 ]] || exit "$STOPCODE"
rc="$(cat "$ST/back/exit")"
[[ "$rc" == 0 ]] || exit "$rc"
if [[ -n "$PADIN2" ]]; then
 [[ "$PAD_STATUS" == 0 ]] || exit "$PAD_STATUS"
 python3 "$ST/bin/pad_relay.py" verify "$DIR/pad-samples.txt" "$PADIN.seq" "$DIR/pad-windows.txt" "$sampled_id"
fi
