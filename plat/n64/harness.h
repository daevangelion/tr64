/* SPDX-License-Identifier: MIT */
/* Ported from MIT ac3n64 04134cc (original mechanism hl64 bd96926). */
#ifndef TR64_HARNESS_H
#define TR64_HARNESS_H

#include <libdragon.h>

#define TH_REG_CMD   0x13FE0000 /* write: 1=screenshot, 2=exit */
#define TH_REG_ARG0  0x13FE0004 /* screenshot: RDRAM addr / exit: code */
#define TH_REG_ARG1  0x13FE0008 /* screenshot: (width<<16)|height */
#define TH_REG_ARG2  0x13FE000C /* screenshot: bytes per pixel (2 or 4) */
#define TH_REG_MAGIC 0x13FE0010 /* read: 0x54455354 "TEST" when present */

static inline bool th_present(void)
{
	return io_read(TH_REG_MAGIC) == 0x54455354;
}

static inline void th_screenshot(const void *buffer, int width, int height,
	int bytes_per_pixel)
{
	if (!th_present())
		return;
	io_write(TH_REG_ARG0, PhysicalAddr(buffer));
	io_write(TH_REG_ARG1, ((uint32_t)width << 16) | (uint32_t)height);
	io_write(TH_REG_ARG2, (uint32_t)bytes_per_pixel);
	io_write(TH_REG_CMD, 1);
}

static inline void th_exit(int code)
{
	if (!th_present())
		return;
	io_write(TH_REG_ARG0, (uint32_t)code);
	io_write(TH_REG_CMD, 2);
}

#endif
