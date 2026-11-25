# SPDX-License-Identifier: MIT
# Private X server. Direct child/group ownership intentionally bypasses rtk.
# Ported from MIT ac3n64 04134cc.
xvfb_start() {
 local n
 XVFB_DIR="$(rtk mktemp -d "$TMPDIR/xvfb.XXXXXX")"
 printf '%s\n' "$XVFB_DIR" >"$dir/xvfb.dir"
 rtk mkfifo "$XVFB_DIR/display"
 defer_signals
 setsid Xvfb -displayfd 3 -screen 0 1280x800x24 3>"$XVFB_DIR/display" >"$XVFB_DIR/log" 2>&1 {slotfd}>&- &
 XVFB_PID=$!
 resume_signals
 if ! read -r -t 15 n <"$XVFB_DIR/display"; then
  echo 'Xvfb did not report a private display' >&2; return 1
 fi
 DISPLAY_NUM=":$n"
 printf '%s\n' "$XVFB_PID" >"$dir/xvfb.pid"
}
