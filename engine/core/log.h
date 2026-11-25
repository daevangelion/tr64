/* SPDX-License-Identifier: MIT */
/* Ported from MIT ac3n64 04134cc (original mechanism hl64 bd96926). */
#ifndef TR64_LOG_H
#define TR64_LOG_H

#include <stdarg.h>
#include <stdint.h>

void	log_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void	log_vprintf(const char *fmt, va_list ap);
void	log_flush(void);
typedef struct { uint32_t peak_pending;uint64_t dropped; } log_stats_t;
void log_stats(log_stats_t *);
/* Direct diagnostic output only: never flush/reset pending ring bytes merely
 * to make a measured burst fit. Counters include the pre-trim overflow peak. */
void log_report(void);

#endif
