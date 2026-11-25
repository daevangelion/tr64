#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SOURCE=${OPENLARA_SOURCE:-$HOME/src/OpenLara}
WORK_ROOT=${TR64_WORK:-/tmp/tr64}
BUILD=${TR64_ORACLE_BUILD:-$WORK_ROOT/builds/Oracle}
export TMPDIR=${TMPDIR:-$WORK_ROOT/tmp/Oracle}
mkdir -p "$BUILD" "$TMPDIR"
exec 9>"$BUILD/build.lock"
flock 9
REV=2333adef30ae2a5f3b32c5355e6e343d6aa0531a
CXX=${CXX:-g++}
FLAGS='-m32 -std=c++11 -O2 -fwrapv -fno-strict-aliasing -ffunction-sections -fdata-sections -D__TR64_ORACLE__'
KEY=$({ printf '%s\n' "$REV" "$FLAGS"; "$CXX" --version; (cd "$HERE" && sha256sum ./*.cpp ./*.h ./*.patch ./build.sh); } | sha256sum | cut -d ' ' -f 1)
if [ "${1:-}" != --fresh ] && [ -x "$BUILD/tr1-oracle" ] && [ -f "$BUILD/key" ] && [ "$(cat "$BUILD/key")" = "$KEY" ]; then
    printf 'oracle build cache hit %s\n' "$KEY"
    exit 0
fi
# This job owns this one export; no shared source tree is modified.
rm -rf "$BUILD/source"
mkdir -p "$BUILD/source"
git -C "$SOURCE" archive "$REV" src/fixed LICENSE | tar -xf - -C "$BUILD/source"
for patch in "$HERE"/*.patch; do
    [ -f "$patch" ] || continue
    patch -s -d "$BUILD/source" -p1 < "$patch"
done
# Intentionally use upstream integer math and literal tables, not libm substitutes.
# shellcheck disable=SC2086
"$CXX" $FLAGS -I"$BUILD/source/src/fixed" "$HERE/oracle.cpp" "$BUILD/source/src/fixed/common.cpp" -Wl,--gc-sections -o "$BUILD/tr1-oracle.new"
mv "$BUILD/tr1-oracle.new" "$BUILD/tr1-oracle"
printf '%s\n' "$KEY" > "$BUILD/key"
printf '%s\n' "$REV" > "$BUILD/revision"
printf '%s\n' "$FLAGS" > "$BUILD/flags"
"$CXX" --version > "$BUILD/compiler"
printf 'oracle built %s\n' "$KEY"
