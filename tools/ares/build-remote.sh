#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Build tr64's private ares (tools/ares/README.md) on the remote emulator host
# for test/run/ares-remote.sh: the same ares revision and the same patches as
# the local private build, a full CMake build there (a binary built on another
# distribution is not copied across).
#
#   tools/ares/build-remote.sh [host-dir]      (host-dir: default tr64-ares)
#
# Build in a temporary directory; install only the binary, settings and
# manifests on success. The source and build trees are never published.
# ARES_PATCHES: the patches applied in this order (default "local-tree sram-persistence").
# The hardware-likeness build (test/run/ares.sh with TR64_ARES_HW=1) is a second, separate
# directory that never touches the first:
#   ARES_PATCHES="local-tree sram-persistence hw-likeness" tools/ares/build-remote.sh tr64-ares-hw
# Env: TR64_ARES_SSH (ssh target, required), ARES_SRC (the git
# checkout the revision is read from, default $HOME/src/ares),
# ARES_XDG (the settings source, default ~/.local/share/ares).
# ARES_JOBS: 1 to 4 (default 4); compiler and linker run at nice level 19.
# Same CMake options as the shared local build: Release, ARES_CORES=n64,
# ARES_BUILD_LOCAL=ON (-O3, -march=native of the host that builds it).
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$HERE/identity.sh"
SSH_TO="${TR64_ARES_SSH:?set TR64_ARES_SSH to the remote SSH target}"
SRC="${ARES_SRC:-$HOME/src/ares}"
XDG="${ARES_XDG:-$HOME/.local/share/ares}"
DIR="${1:-tr64-ares}"
PATCHES="${ARES_PATCHES:-local-tree sram-persistence}"
JOBS="${ARES_JOBS:-4}"
[[ "$JOBS" =~ ^[1-4]$ ]] || { echo "build-remote: ARES_JOBS must be 1..4" >&2; exit 2; }
[[ "$DIR" == tr64-ares || "$DIR" == tr64-ares-hw || "$DIR" == tr64-ares-coop ]] ||
 { echo "build-remote: refusing non-private ares destination $DIR" >&2; exit 2; }
patch_files=()
sampled=0
for p in $PATCHES; do
 [[ "$p" =~ ^[a-z0-9-]+$ && -f "$HERE/$p.patch" ]] || { echo "build-remote: invalid patch $p" >&2; exit 2; }
 patch_files+=("$HERE/$p.patch")
 if [[ "$p" == two-pad ]]; then sampled=1; fi
done
identity=
if [[ "$sampled" == 1 || "$DIR" == tr64-ares-coop ]]; then
 [[ "$DIR" == tr64-ares-coop && "$PATCHES" == "$ARES_COOP_PATCHES" ]] ||
  { echo "build-remote: the sampled patch stack requires the dedicated tr64-ares-coop destination" >&2; exit 2; }
 identity=$(sampled_ares_identity "$HERE")
fi
[[ "$(rtk git -C "$SRC" rev-parse "$ARES_REV^{commit}")" == "$ARES_REV" ]] ||
 { echo "build-remote: $SRC lacks $ARES_REV" >&2; exit 1; }
[[ -f "$XDG/settings.bml" ]] || { echo "build-remote: no $XDG/settings.bml" >&2; exit 1; }
SSH=(rtk ssh -o BatchMode=yes "$SSH_TO")
stage=$("${SSH[@]}" 'mktemp -d "$HOME/.tr64-ares-build.XXXXXXXX"')
[[ "$stage" == /*/.tr64-ares-build.* ]] || { echo "build-remote: invalid staging path" >&2; exit 1; }
trap '"${SSH[@]}" "rm -rf -- $(printf %q "$stage")"' EXIT
"${SSH[@]}" "mkdir -p $(printf '%q ' "$stage"/{src,patches,xdg/ares})"
rtk git -C "$SRC" archive "$ARES_REV" | "${SSH[@]}" "tar -x -C $(printf %q "$stage/src")"
rtk rsync -t "${patch_files[@]}" "$SSH_TO:$stage/patches/"
rtk rsync -rt --exclude Saves "$XDG/" "$SSH_TO:$stage/xdg/ares/"
"${SSH[@]}" "bash -s -- $(printf '%q ' "$stage" "$DIR" "$ARES_REV" "$PATCHES" "$JOBS" "$identity")" <<'REMOTE'
set -euo pipefail
stage=$1 dir=$2 rev=$3 patches=$4 jobs=$5 identity=$6
cd "$stage"
if [[ -n "$identity" ]]; then
 uploaded=$(
  {
   printf '%s\n' "$rev"
   for p in $patches; do
    digest=$(sha256sum -- "patches/$p.patch") || exit
    printf '%s %s\n' "$p" "${digest%% *}"
   done
  } | sha256sum
 )
 [[ "${uploaded%% *}" == "$identity" ]] || { echo 'build-remote: staged patch identity changed' >&2; exit 1; }
fi
for p in $patches; do
 patch -p1 -d src --forward --no-backup-if-mismatch -s <"patches/$p.patch"
done
flags=()
if [[ -n "$identity" ]]; then
 flags+=("-DCMAKE_CXX_FLAGS=-DTR64_ARES_IDENTITY=\\\"$identity\\\"")
fi
nice -n 19 cmake -S src -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DARES_CORES=n64 -DARES_BUILD_LOCAL=ON "${flags[@]}" >cmake.log 2>&1 ||
 { tail -20 cmake.log >&2; exit 1; }
nice -n 19 ninja -j "$jobs" -C build >ninja.log 2>&1 ||
 { grep -m 20 -E 'error|Error' ninja.log >&2; exit 1; }
binary=build/desktop-ui/ares
if [[ -n "$identity" ]]; then
 strings "$binary" | grep -Fx "TR64 sampled ares $identity" >/dev/null ||
  { echo 'build-remote: binary lacks sampled input identity' >&2; exit 1; }
 digest=$(sha256sum "$binary")
 printf '%s %s\n' "$identity" "${digest%% *}" >sampled-identity.txt
fi
{
 echo "revision $rev"
 sha256sum patches/*.patch src/ares/n64/cartridge/testharness.cpp xdg/ares/settings.bml "$binary"
 printf 'build: nice -n 19 ninja -j %s; CMake Release ARES_CORES=n64 ARES_BUILD_LOCAL=ON\n' "$jobs"
 c++ --version | sed -n '1p'
 [[ -z "$identity" ]] || cat sampled-identity.txt
} >manifest.txt
cd "$HOME"
for path in "$dir" "$dir/build" "$dir/build/desktop-ui" "$dir/xdg" "$dir/xdg/ares"; do
 [[ ! -L "$path" ]] || { echo "build-remote: refusing symlink $path" >&2; exit 1; }
done
mkdir -p "$dir/build/desktop-ui"
if [[ -d "$dir/xdg" ]]; then mv -T "$dir/xdg" "$stage/previous-xdg"; fi
mv -T "$stage/xdg" "$dir/xdg"
mv -fT "$stage/$binary" "$dir/$binary"
mv -fT "$stage/manifest.txt" "$dir/manifest.txt"
[[ -z "$identity" ]] || mv -fT "$stage/sampled-identity.txt" "$dir/sampled-identity.txt"
cat "$dir/manifest.txt"
REMOTE
