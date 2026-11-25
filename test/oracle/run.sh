#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu
export PYTHONDONTWRITEBYTECODE=1
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
"$HERE/build.sh"
exec "${PYTHON:-python3}" "$HERE/fixtures.py" "$@"
