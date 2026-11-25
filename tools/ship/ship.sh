#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Build/check/deliver only through the production build and shared ares harness.
# check/deliver verdicts: 0 pass, 1 failed gate/refusal, 2 usage, 3 invalid run (rerun).
set -euo pipefail
ROOT=$(rtk git -C "$(dirname "$0")" rev-parse --show-toplevel)
WORK_ROOT="${TR64_WORK:-/tmp/tr64}"
BUILD_DIR=${BUILD_DIR:-$WORK_ROOT/builds/Demo/v1}
PACK=${TR64_LEVEL_PACK:?set TR64_LEVEL_PACK to a compatible campaign pack with SN64 sound sections}
export TMPDIR=${TMPDIR:-$WORK_ROOT/tmp/Demo}
GATE=${TR64_GATE_DIR:-$BUILD_DIR/gate}
SOURCE=${TR64_SOURCE:-$([[ $(basename "$PACK") == psx-* ]] && echo psx || echo pc)}
rtk mkdir -p "$BUILD_DIR" "$TMPDIR"

cart_make() {
  local dir=$1
  shift
  rtk make -C "$ROOT" BUILD_DIR="$dir" TR64_PLAY=1 TR64_TEST=0 TR64_LEVEL_PACK="$PACK" "$@"
}

gate_make() {
  local dir=$1
  shift
  rtk python3 "$ROOT/tools/ship/gate_place.py" --source "$SOURCE" --out "$dir/generated/gate_place.h"
  cart_make "$dir" TR64_SCENE_TEST_SOURCES=tools/ship/gate-place.c "$@"
}

expected_identities() {
  # Never let a sidecar from the ROM under test supply its own expectation.
  id_dir=$(rtk mktemp -d "$TMPDIR/ship-id.XXXXXX")
  trap 'rtk rm -rf -- "$id_dir"' EXIT
  cart_make "$id_dir/base" cart-identity
  gate_make "$id_dir/gate" cart-identity
}

case ${1:-} in
 build)
  cart_make "$BUILD_DIR" rom -j4
  rtk python3 -c 'import hashlib,pathlib,sys; p=pathlib.Path(sys.argv[1]); print("campaign-pack sha256="+hashlib.sha256(p.read_bytes()).hexdigest())' "$PACK"
  ;;
 build-gate)
  # The gate ROM: the same sources plus tools/ship/gate-place.c (placement chords: L+Z a deadly fall,
  # L+R the end trigger, L+B a flat patch for the pace run; L+A steps the burn knob).
  # Never delivered. Placements come from the player's level data, written to
  # the gate build's generated directory; nothing derived from the data is committed.
  gate_make "$GATE" rom -j4
  ;;
 check)
  # Base run on the delivered ROM (boot, videos, title, levels, save through the picker), the power-cycle
  # load of that save on the delivered ROM, the gate ROM's death page and natural level end, and its pace
  # run (game time against the hardware clock with 0 and 40 ms burned after every frame).
  # A pack with videos always gets the video verdict. TR64_VIDEO_ABSENT=ESCAPE for a pack whose edition
  # lacks that film (PlayStation disc); it must match what the pack lacks.
  gate_make "$GATE" rom -j4
  expected_identities
  videos=$(rtk python3 "$ROOT/tools/ship/boot.py" --probe-pack "$PACK")
  rtk python3 "$ROOT/tools/ship/boot.py" --rom "$BUILD_DIR/tr64.z64" --out "$BUILD_DIR/play-check" --reload --sound --cutscene cut4 \
   --identity-header "$id_dir/base/generated/cart_identity.h" --gate-identity-header "$id_dir/gate/generated/cart_identity.h" \
   --pack "$PACK" --save-load --gate-rom "$GATE/tr64.z64" --pace-burn "${TR64_PACE_BURN:-0,40}" --pace-timeout "${TR64_PACE_TIMEOUT:-3600}" ${videos:+--video ${TR64_VIDEO_ABSENT:+--video-absent $TR64_VIDEO_ABSENT}} \
   --timeout "${TR64_GATE_TIMEOUT:-1500}" --secondary-timeout "${TR64_GATE_TIMEOUT:-1500}"
  ;;
 deliver)
  [[ ${TR64_VISUAL_REVIEWED:-0} == 1 ]] || { echo 'Inspect play-check/vi pictures first; set TR64_VISUAL_REVIEWED=1' >&2; exit 2; }
  [[ -z $(rtk proxy git -C "$ROOT" status --porcelain) ]] || { echo 'Commit all source changes, including untracked files, before delivery' >&2; exit 2; }
  expected_identities
  sha=$(rtk git -C "$ROOT" rev-parse --short=12 HEAD)
  local_md5=$(rtk md5sum "$BUILD_DIR/tr64.z64"); local_md5=${local_md5%% *}
  rtk python3 "$ROOT/tools/ship/deliver_check.py" --summary "$BUILD_DIR/play-check/summary.json" --rom-md5 "$local_md5" \
   --identity-header "$id_dir/base/generated/cart_identity.h" --gate-identity-header "$id_dir/gate/generated/cart_identity.h" \
   --pack "$PACK" --gate-rom "$GATE/tr64.z64"
  build=$(rtk python3 -c 'import pathlib,re,sys; print(re.search(r"TR64_BUILD_ID \"([0-9A-F]+)\"",pathlib.Path(sys.argv[1]).read_text())[1])' "$id_dir/base/generated/build_id.h")
  symbols="${TR64_SHIP_EVIDENCE:-$WORK_ROOT/evidence/ship}/$build"
  rtk mkdir -p "$symbols"
  # Retain each delivered ROM variant; never replace another build's symbols.
  # Older deliveries kept only the ROM: their addresses need an exact rebuild.
  rtk tar --zstd -cf "$symbols/$local_md5.tar.zst.tmp" -C "$BUILD_DIR" tr64.elf tr64.map generated/build_id.h generated/cart_identity.h size.json play-check/summary.json
  rtk mv "$symbols/$local_md5.tar.zst.tmp" "$symbols/$local_md5.tar.zst"
  name="tr64-demo-$sha-$SOURCE.z64"
  deliver_ssh="${TR64_DELIVER_SSH:?set TR64_DELIVER_SSH to the delivery SSH target}"
  rtk rsync -t "$BUILD_DIR/tr64.z64" "$deliver_ssh:roms/$name"
  remote_md5=$(rtk ssh "$deliver_ssh" "md5sum roms/$name"); remote_md5=${remote_md5%% *}
  [[ "$local_md5" == "$remote_md5" ]] || { echo 'remote MD5 mismatch' >&2; exit 1; }
  printf 'Delivered roms/%s\nsource=%s\nmd5=%s\n' "$name" "$sha" "$local_md5"
  ;;
 *) echo 'usage: tools/ship/ship.sh build|build-gate|check|deliver' >&2; exit 2 ;;
esac
