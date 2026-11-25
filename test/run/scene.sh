#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Ported from MIT ac3n64 04134cc.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
exec rtk python3 "$ROOT/test/run/scene.py" "$@"
