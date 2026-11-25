/* SPDX-License-Identifier: MIT */
#ifndef TR64_ENG_H
#define TR64_ENG_H
#include <stdint.h>
/* Game owns one serialisable block, allocated after the level core is ready.
 * Uses the remaining state pool without resetting renderer workspace. The
 * production allocator fails closed, naming state, if its budget is exceeded. */
void *eng_game_state_alloc(uint32_t bytes);
/* After stopping sound before game_level_stop and draining/releasing the
 * renderer, recycle the whole level without reboot. Resident IRQ-enabled
 * caller, outside any code loading/dispatch/frame callback. Repeats the
 * idempotent sound stop before I/O/lease checks and resource teardown; clears
 * resources, planner, code frames and level/state allocations. Boot-owned GPU,
 * audio, library and fixed pools stay intact. Next load uses mem_level_begin. */
void eng_level_release(void);
/* Optional engine-owned storage/haptics. A missing storage hook is the game's
 * named NOT_PORTED condition, never fake success or an empty-save result.
 * Save runs in resident orchestration after the inventory unit returns, not
 * inside code_call. Metadata/haptics hooks must not page or dispatch units. */
enum { TR64_ENG_SAVE_FAILED=0, TR64_ENG_SAVE_OK=1, TR64_ENG_SAVE_BLOCKED=2 };
int eng_game_save(void) __attribute__((weak));
int eng_game_has_save(void) __attribute__((weak)); /* 1 valid save,0 none */
void eng_game_save_settings(void) __attribute__((weak));
void eng_game_vibrate(void) __attribute__((weak));
/* The same fatal path used by the rest of the engine, not a game fallback. */
void sys_error(const char *fmt, ...) __attribute__((noreturn,format(printf,1,2)));
#endif
