# SPDX-License-Identifier: MIT
# Sourced by test/run/ares.sh (local runs) and test/run/ares-remote.sh (the
# same runs on TR64_ARES_HOST): the one place that says how ares is started,
# so the two machines cannot drift apart. ARES_EXPANSION_PAK defaults to false.
ARES_SETTINGS=(--system 'Nintendo 64' --setting 'Video/Driver=OpenGL 3.2' --setting Audio/Driver=None
 --setting Input/Driver=SDL --setting General/HomebrewMode=true
 --setting Boot/AwaitGDBClient=false --setting Boot/Debugger=false
 --setting "Nintendo64/ExpansionPak=${ARES_EXPANSION_PAK:-false}")
# Exported into ares' environment next to ARES_TEST_HARNESS=1 and DISPLAY.
ARES_ENV=(LIBGL_ALWAYS_SOFTWARE=1)
# TR64_ARES_HW=1: the hardware-likeness lane (tools/ares/README.md, hw-likeness.patch): the
# interpreter, so the instruction and data caches are emulated. ares-remote.sh sets ARES_HW_LIKE.
[[ -z "${TR64_ARES_HW:-}" ]] || ARES_SETTINGS+=(--setting General/ForceInterpreter=true)
