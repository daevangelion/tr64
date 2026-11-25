#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Ported from MIT ac3n64 04134cc. Full M0 includes the real cook by default.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
WORK_ROOT="${TR64_WORK:-/tmp/tr64}"
export BUILD_DIR="${BUILD_DIR:-$WORK_ROOT/builds/Skeleton}"
export TMPDIR="${TMPDIR:-$WORK_ROOT/tmp/Skeleton}"
export TR64_DATA="${TR64_DATA:-$WORK_ROOT/data}"
export TR64_TEST=1
rtk mkdir -p "$TMPDIR" "$BUILD_DIR"
ares=0 partial=0 hw=0
for arg in "$@"; do
 case "$arg" in
  --ares) ares=1 ;;
  --skeleton-only) partial=1 ;;
  --hw) hw=1 ;;
  *) echo 'usage: m0.sh [--ares] [--skeleton-only] [--hw]' >&2; exit 2 ;;
 esac
done
if [[ $partial == 0 ]]; then
 [[ -d "$TR64_DATA" && "$(rtk make -s -C "$ROOT" --no-print-directory cook-enabled)" =~ (^|[[:space:]])1($|[[:space:]]) ]] || { echo 'M0 FAIL: cook.mk or TR64_DATA missing (use --skeleton-only for explicit partial verification)' >&2; exit 1; }
fi
rtk make -C "$ROOT" --no-print-directory host unit sdk-check
"$ROOT/test/run/scene.sh" --host -j 1 "$ROOT/test/scenes/boot.route"
"$ROOT/test/run/scene.sh" --host -j 1 "$ROOT/test/scenes/clock-wrap.route"
rtk python3 "$ROOT/test/run/controls.py" --host
rtk python3 "$ROOT/test/run/loader-tests.py"
rtk make -C "$ROOT" --no-print-directory provcheck
if [[ $partial == 0 ]]; then
 rtk make -C "$ROOT" --no-print-directory cook-tests cook-m0
fi
if [[ $ares == 1 ]]; then
 ARES_EXPANSION_PAK=false "$ROOT/test/run/scene.sh" --ares -j 1 "$ROOT/test/scenes/boot.route"
 ARES_EXPANSION_PAK=false "$ROOT/test/run/scene.sh" --ares -j 1 "$ROOT/test/scenes/clock-wrap.route"
 # controls.py --cart drives this machine's two ares slot locks (lifecycle self-tests, fuser, own process
 # groups): it cannot run on the remote emulator host, so it stays local whatever TR64_ARES_HOST says.
 env -u TR64_ARES_HOST rtk python3 "$ROOT/test/run/controls.py" --cart
 rtk python3 "$ROOT/test/run/loader-tests.py" --ares
 ARES_EXPANSION_PAK=true "$ROOT/test/run/scene.sh" --ares -j 1 "$ROOT/test/scenes/boot.route"
 TR64_TEST=0 BUILD_DIR="$BUILD_DIR/shipping" ARES_EXPANSION_PAK=false "$ROOT/test/run/scene.sh" --ares -j 1 "$ROOT/test/scenes/boot.route"
fi
if [[ $hw == 1 ]]; then
 # The hardware-likeness lane (tools/ares/README.md): the boot scene, one run, in the interpreter ares
 # that fetches kernel-mapped addresses and indexes cache operations the way the FPGA core does.
 # Passes on a build whose boot works on that core, fails on one that does not.
 [[ -n "${TR64_ARES_HOST:-}" ]] || { echo 'M0 FAIL: --hw needs TR64_ARES_HOST=<remote-host> (~/tr64-ares-hw, tools/ares/build-remote.sh)' >&2; exit 1; }
 TR64_ARES_HW=1 ARES_EXPANSION_PAK=false "$ROOT/test/run/scene.sh" --ares -j 1 "$ROOT/test/scenes/boot.route"
fi
if [[ $partial == 1 ]]; then
 echo 'M0 PARTIAL (cook not run)'
else
 echo 'M0 PASS'
fi
