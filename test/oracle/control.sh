#!/bin/sh
# SPDX-License-Identifier: MIT
# Negative-control oracle: the current oracle with one hunk taken back out.
#   test/oracle/control.sh NAME      builds controls/NAME.patch applied after every test/oracle/*.patch
# Output: $TR64_ORACLE_CONTROL_BUILD/NAME/tr1-oracle (default $TR64_WORK/builds/Oracle-control).
# Same pinned source, flags and compiler as build.sh. A control is never published as a fixture: the scripts that
# use one (`burn.py ceiling --control-oracle`) name the build they were run with.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
NAME=${1:?usage: control.sh NAME}
[ -f "$HERE/controls/$NAME.patch" ] || { echo "no controls/$NAME.patch" >&2; exit 2; }
SOURCE=${OPENLARA_SOURCE:-$HOME/src/OpenLara}
WORK_ROOT=${TR64_WORK:-/tmp/tr64}
BUILD=${TR64_ORACLE_CONTROL_BUILD:-$WORK_ROOT/builds/Oracle-control}/$NAME
export TMPDIR=${TMPDIR:-$WORK_ROOT/tmp/Oracle}
mkdir -p "$BUILD" "$TMPDIR"
REV=2333adef30ae2a5f3b32c5355e6e343d6aa0531a
CXX=${CXX:-g++}
FLAGS='-m32 -std=c++11 -O2 -fwrapv -fno-strict-aliasing -ffunction-sections -fdata-sections -D__TR64_ORACLE__'
rm -rf "$BUILD/source"
mkdir -p "$BUILD/source"
git -C "$SOURCE" archive "$REV" src/fixed LICENSE | tar -xf - -C "$BUILD/source"
for patch in "$HERE"/*.patch "$HERE/controls/$NAME.patch"; do
    patch -s -d "$BUILD/source" -p1 < "$patch"
done
# shellcheck disable=SC2086
"$CXX" $FLAGS -I"$BUILD/source/src/fixed" "$HERE/oracle.cpp" "$BUILD/source/src/fixed/common.cpp" -Wl,--gc-sections -o "$BUILD/tr1-oracle"
printf 'control oracle %s built: %s\n' "$NAME" "$BUILD/tr1-oracle"
