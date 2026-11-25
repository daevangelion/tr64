/* SPDX-License-Identifier: MIT */
/* Ported from MIT ac3n64 04134cc. */
#ifndef TR64_SYS_H
#define TR64_SYS_H
#include <stdint.h>
#include <stddef.h>
#include "tr64/limits.h"
#include "tr64/eng.h"
typedef struct { uint16_t buttons; int8_t x, y; } tr64_pad_t;
void sys_log_write(const char *s, uint32_t n);
void sys_cache_writeback(void *p, uint32_t n);
void sys_memory_boot(void);
void sys_memory_report(void);
void sys_heap_freeze(void);
void sys_heap_check(void);
void sys_heap_report(void);
void sys_test_probe(unsigned probe);
uint32_t sys_io_frame_us(void);
void sys_io_frame_begin(void);
/* Host: cumulative model cost. Cart: completed engine/io enqueue-to-observed-
 * completion emulated microseconds, including queue/poll delay. Never resets. */
uint64_t sys_io_total_us(void);
void sys_code_init(void);
const void *sys_code_find(const char *name,uint32_t *pages);
void sys_code_load(const void *unit,void *const *frames,uint32_t count);
void sys_code_unload(const void *unit);
unsigned sys_code_wired(void);
unsigned sys_code_entries(void);
void sys_code_probe(const void *unit,int loaded);
void sys_code_before_call(const void *unit);
int sys_code_context_ok(void);
/* One poll for both ports; omit port 2 when only the primary is needed. */
void sys_pad_read(tr64_pad_t *pad,tr64_pad_t *second);
/* A read launches a single piece. Poll publishes its completion. */
int sys_read_start(void *dst, uint32_t offset, uint32_t bytes);
int sys_read_poll(void);
/* Host-only file binding; returns actual file size or -1. */
int sys_cart_open(const char *path);
/* Bind a raw ROM-relative byte range within the validated bundle extent;
 * no filesystem or heap work. N64 only, and no queued/active IO may exist.
 * Returns 0 on success, -1 on invalid range/in-flight DMA/host backend. */
int sys_cart_bind(uint32_t rom_offset,uint32_t bytes);
void sys_present(const uint16_t *pixels);
/* 0 captured, 1 no presented/capturable frame, -1 capture failure. */
int sys_shot(const char *path);
void sys_quit(int status) __attribute__((noreturn));
/* Microseconds modulo 2^32. Compare ONLY unsigned elapsed differences, with
 * intervals shorter than 2^32 us; never order absolute timestamps. This
 * contract applies equally to frame, I/O, render, video and audio timing. */
uint32_t sys_time_us(void);
/* Short, single-frame profiling only: subtract uint32 raw ticks, aggregate in
 * raw units, then convert once. Native CP0 Count wraps in about91s and ignores
 * gameplay/test clock rebase; host raw units are modeled microseconds.
 * These are not absolute timestamps or gameplay/deadline clocks. */
uint32_t sys_profile_ticks(void);
uint32_t sys_profile_us(uint32_t elapsed);
/* Test-build-only rebase: leave raw/model time and 64-bit I/O totals alone. */
void sys_test_clock_rebase(uint32_t target);
#endif
