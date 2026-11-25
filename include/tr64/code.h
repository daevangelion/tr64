/* SPDX-License-Identifier: MIT */
#ifndef TR64_CODE_H
#define TR64_CODE_H
#include <stdint.h>
void code_boot(void);
/* Resident, IRQ-enabled thread only. Balanced scopes may nest; only the
 * outermost end leaves loading state. No implicit unload occurs on end. */
void code_loading_begin(void);
void code_loading_end(void);
/* Loading state or code_frame's transition callback only. Frames always come
 * from the budgeted code pool; there is no implicit lending/growth. The init
 * name is reserved to bootstrap. Unknown names, capacity/lifetime errors and
 * a second load per frame are fatal. */
void code_load(const char *name);
void code_unload(const char *name);
int code_is_loaded(const char *name);
/* Resolve the dispatch table on EVERY call. Unit NAME exports
 * uint32_t tr64_unit_NAME(uint32_t). Do not retain a function pointer.
 * An unloaded native call faults through the SDK's unit diagnostic. */
uint32_t code_call(const char *name,uint32_t argument);
/* One resident planner owns the single transition point for all subsystems.
 * Set/replace/clear it in loading state. Function and context remain resident
 * and valid until replaced; a unit cannot register a callback or transition. */
typedef void (*code_frame_handler_t)(uint32_t frame,void *context);
void code_set_frame_handler(code_frame_handler_t handler,void *context);
/* Begin the exclusive release transaction from idle, before callbacks may
 * drain owned work. Unlike ordinary loading scopes this cannot nest. */
void code_level_release_begin(void);
/* Engine release transaction only: exactly one outer loading scope, no active
 * dispatch. Clear the planner, unload all units and restart frame numbering.
 * Slot/TLB ownership is released, not re-booted or replaced by a new allocator. */
void code_level_reset(void);
/* Invoke once per production frame, before simulation/render work. Only the
 * planner may synchronously load/unload here; at most one load is permitted. */
void code_frame(uint32_t frame);
/* Running totals since boot: explicit code_call dispatches, and unit loads. Not the calls resident code makes
 * into a loaded unit's other functions, nor a unit's calls back into resident helpers. */
void code_stats(uint32_t *calls,uint32_t *loads);
void code_log(void);
#endif
