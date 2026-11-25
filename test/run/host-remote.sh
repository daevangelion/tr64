#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# Run a host-side command (an oracle campaign or directed suite, `make host`,
# a Python gate) on a remote machine instead of here:
#
#   test/run/host-remote.sh [options] -- COMMAND [ARGS...]
#   test/run/host-remote.sh [options] -- test/game/directed.py [ARGS...]
#
# COMMAND runs in a snapshot of this working tree (tracked and untracked
# files, `git ls-files -co --exclude-standard`) as its working directory, with
# PYTHONPATH=tools/cook, and with $TR64_WORK bound to ~/tr64-host/build/tr64 on the
# host (the tree is the one of the current directory, so a worktree runs its own
# files with any copy of this script), so work-root paths resolve there with no
# change to the suites. A first argument ending in .py runs under python3.
# Standard output and error stream back live; the exit status is COMMAND's
# (255: the host could not be reached, 2: usage).
#
# Always in place on the host (rsync -t, incremental): $TR64_WORK/data
# (PC and PSX level data, read-only) and $TR64_WORK/fixtures (the
# published oracles). Everything else a command reads that lives under
# $TR64_WORK and is not built by the command itself is named with --sync:
# a level pack for `make host`, say. Output a command leaves under
# $TR64_WORK stays on the host (builds and tmp persist between runs and
# are reused, e.g. a host build); --fetch brings paths back to the same
# local paths.
#
# Options:
#   --pre 'SHELL COMMAND'   run first, same place (repeatable)
#   --sync PATH             PATH under $TR64_WORK (file or directory, absolute, here) to
#                           the same place on the host first (repeatable)
#   --fetch PATH            PATH under $TR64_WORK (absolute, on the host) to the same
#                           place here afterwards, also when COMMAND failed (repeatable)
#   --name NAME             the snapshot family (default: this tree's directory name)
# Env: TR64_HOST_SSH (ssh target, required), TR64_WORK (work root, default /tmp/tr64),
#      TR64_HOST_JOBS (make -j for the command's `make`, default 8), TMPDIR on the host is
#      $TR64_WORK/tmp/<name>.
# Host setup: nothing beyond ssh, rsync, bubblewrap (user namespaces), a C and
# C++ compiler with 32-bit support (the oracle is -m32), python3, make. The
# host keeps the newest 6 snapshots per name under ~/tr64-host/repos.
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel 2>/dev/null || (cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd))"
SSH_TO="${TR64_HOST_SSH:?set TR64_HOST_SSH to the remote SSH target}"
WORK_ROOT="${TR64_WORK:-/tmp/tr64}"
SSH=(ssh -o BatchMode=yes -o ServerAliveInterval=30 -o ServerAliveCountMax=10)
export RSYNC_RSH="${SSH[*]}"
PRE=() SYNC=() FETCH=() NAME="$(basename "$ROOT")"
while [[ $# -gt 0 ]]; do
 case "$1" in
  --pre) [[ $# -ge 2 ]] || { echo 'host-remote: --pre needs a command' >&2; exit 2; }; PRE+=("$2"); shift 2 ;;
  --sync) [[ $# -ge 2 ]] || exit 2; SYNC+=("$2"); shift 2 ;;
  --fetch) [[ $# -ge 2 ]] || exit 2; FETCH+=("$2"); shift 2 ;;
  --name) [[ $# -ge 2 ]] || exit 2; NAME="$2"; shift 2 ;;
  --) shift; break ;;
  *) break ;;
 esac
done
[[ $# -ge 1 ]] || { echo 'usage: host-remote.sh [--pre CMD] [--sync PATH] [--fetch PATH] [--name N] -- COMMAND [ARGS...]' >&2; exit 2; }
[[ "$NAME" =~ ^[A-Za-z0-9._-]+$ ]] || { echo "host-remote: bad --name $NAME" >&2; exit 2; }
for p in "${SYNC[@]}" "${FETCH[@]}"; do
 [[ "$p" == "$WORK_ROOT"/* ]] || { echo "host-remote: $p is not under $WORK_ROOT" >&2; exit 2; }
done

# the snapshot: keyed by the content of the tree, so a changed tree is a new
# directory (host builds elsewhere never see a half-updated source tree)
cd "$ROOT"
HASH="$(git ls-files -co --exclude-standard -z | sort -z | xargs -0 sha256sum | sha256sum | cut -c1-16)"
H=tr64-host
"${SSH[@]}" -n "$SSH_TO" "mkdir -p $H/bin $H/build/tr64/{data,fixtures,builds,tmp/$NAME} $H/repos/$NAME
 printf '%s\n' '#!/bin/sh' '# rtk is not installed here: the Makefile and the suites only prefix commands with it' '[ \"\$1\" = proxy ] && shift' 'exec \"\$@\"' >$H/bin/rtk && chmod +x $H/bin/rtk" \
 || { echo "host-remote: $SSH_TO unreachable" >&2; exit 255; }
if ! "${SSH[@]}" -n "$SSH_TO" "test -f $H/repos/$NAME/$HASH/.complete"; then
 git ls-files -co --exclude-standard -z | rsync -rt --from0 --files-from=- --mkpath "$ROOT/" "$SSH_TO:$H/repos/$NAME/$HASH/" \
  && "${SSH[@]}" -n "$SSH_TO" "touch $H/repos/$NAME/$HASH/.complete" \
  || { echo "host-remote: copy of the tree failed" >&2; exit 255; }
fi
"${SSH[@]}" -n "$SSH_TO" "cd $H/repos/$NAME && ls -dt */ | tail -n +7 | xargs -r rm -rf" || true
rsync -rt "$WORK_ROOT/data" "$WORK_ROOT/fixtures" "$SSH_TO:$H/build/tr64/" \
 || { echo "host-remote: copy of data/fixtures failed" >&2; exit 255; }
for p in "${SYNC[@]}"; do
 [[ -e "$p" ]] || { echo "host-remote: --sync $p: no such path here" >&2; exit 2; }
 rel="${p#$WORK_ROOT}"
 if [[ -d "$p" ]]; then
  rsync -rt --mkpath "$p/" "$SSH_TO:$H/build/tr64$rel/" || { echo "host-remote: sync of $p failed" >&2; exit 255; }
 else
  rsync -t --mkpath "$p" "$SSH_TO:$H/build/tr64$rel" || { echo "host-remote: sync of $p failed" >&2; exit 255; }
 fi
done

# the command, in the snapshot, with $WORK_ROOT bound; its script goes in on stdin
JOBS="${TR64_HOST_JOBS:-8}"
inner='set -euo pipefail
export PATH="$HOME/tr64-host/bin:$PATH" PYTHONPATH=tools/cook TR64_WORK='"$(printf '%q' "$WORK_ROOT")"' TMPDIR='"$(printf '%q' "$WORK_ROOT/tmp/$NAME")"' PYTHONDONTWRITEBYTECODE=1
export MAKEFLAGS="-j'"$JOBS"'"
mkdir -p "$TMPDIR"
'
for c in "${PRE[@]}"; do inner+="$c"$'\n'; done
if [[ "$1" == *.py ]]; then inner+='exec python3 '; else inner+='exec '; fi
inner+="$(printf '%q ' "$@")"
code=0
"${SSH[@]}" "$SSH_TO" "cd $H/repos/$NAME/$HASH && bwrap --die-with-parent --bind / / --dev-bind /dev /dev --proc /proc --bind /tmp /tmp \
 --bind \$HOME/$H/build/tr64 $(printf '%q' "$WORK_ROOT") --chdir \$PWD bash -s" <<<"$inner" || code=$?
for p in "${FETCH[@]}"; do
 rel="${p#$WORK_ROOT}"
 if "${SSH[@]}" -n "$SSH_TO" "test -d $H/build/tr64$rel"; then
  mkdir -p "$p"; rsync -rt "$SSH_TO:$H/build/tr64$rel/" "$p/" || echo "host-remote: fetch of $p failed" >&2
 elif "${SSH[@]}" -n "$SSH_TO" "test -f $H/build/tr64$rel"; then
  mkdir -p "$(dirname "$p")"; rsync -t "$SSH_TO:$H/build/tr64$rel" "$p" || echo "host-remote: fetch of $p failed" >&2
 else
  echo "host-remote: nothing to fetch at $p" >&2
 fi
done
exit "$code"
