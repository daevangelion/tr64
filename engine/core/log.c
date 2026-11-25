/* SPDX-License-Identifier: MIT */
/* Ported from MIT ac3n64 04134cc (original mechanism hl64 bd96926). */
#include "engine/core/fmt.h"

#include "tr64/limits.h"
#include "engine/core/log.h"
#include "engine/core/sys.h"

#define RING_MASK	(TR64_LOG_RING_BYTES - 1u)

typedef char ring_size_is_pow2[(TR64_LOG_RING_BYTES & RING_MASK) == 0 ? 1 : -1];

static char	ring[TR64_LOG_RING_BYTES];
static uint32_t	head, flushed, dropped;
static uint32_t peak_pending;
static uint64_t dropped_total;

void log_vprintf(const char *fmt, va_list ap)
{
	char line[TR64_LOG_LINE_MAX];
	int n = tr64_vsnprintf(line, sizeof(line), fmt, ap);
	int i;

	if (n < 0)
		sys_error("invalid log format: %s",fmt);
	if (n >= (int)sizeof(line))
		n = (int)sizeof(line) - 1;
	for (i = 0; i < n; i++)
		ring[head++ & RING_MASK] = line[i];
	if(head-flushed>peak_pending)peak_pending=head-flushed;
	if (head - flushed > TR64_LOG_RING_BYTES)
	{
		dropped += head - flushed - TR64_LOG_RING_BYTES;
		dropped_total += head - flushed - TR64_LOG_RING_BYTES;
		flushed = head - TR64_LOG_RING_BYTES;
	}
}

void log_printf(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	log_vprintf(fmt, ap);
	va_end(ap);
}

void log_flush(void)
{
	if (dropped)
	{
		char msg[48];
		int n = tr64_snprintf(msg, sizeof(msg), "log: %lu bytes dropped\n",
			(unsigned long)dropped);

		dropped = 0;
		sys_log_write(msg, (uint32_t)n);
	}
	while (flushed != head)
	{
		uint32_t at = flushed & RING_MASK;
		uint32_t n = head - flushed;

		if (n > TR64_LOG_RING_BYTES - at)
			n = TR64_LOG_RING_BYTES - at;
		sys_log_write(ring + at, n);
		flushed += n;
	}
}

void log_stats(log_stats_t *out)
{
	out->peak_pending=peak_pending;
	out->dropped=dropped_total;
}

void log_report(void)
{
	char line[112];
	int n=tr64_snprintf(line,sizeof(line),
		"log-ring: bytes=%u peak_pending=%lu dropped=%llu\n",
		TR64_LOG_RING_BYTES,(unsigned long)peak_pending,(unsigned long long)dropped_total);
	if(n<0 || n>=(int)sizeof(line))sys_error("log stats formatting");
	sys_log_write(line,(uint32_t)n);
}
