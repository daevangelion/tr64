/* SPDX-License-Identifier: MIT */
/* Ported from MIT ac3n64 04134cc; tr64 stock-memory and bus limits. */
#ifndef TR64_LIMITS_H
#define TR64_LIMITS_H
#define TR64_RDRAM_BYTES (4u * 1024u * 1024u)
#define TR64_CART_BYTES (64u * 1024u * 1024u)
#define TR64_LOG_RING_BYTES 4096u
#define TR64_LOG_LINE_MAX 512
#define TR64_WIDTH 320
#define TR64_HEIGHT 240
#define TR64_TICK_HZ 30u
#define TR64_IO_PIECE 4096u
#define TR64_IO_FRAME 32768u
#define TR64_ROUTE_MAX 4096u
#define TR64_SCENE_TICKS_MAX 600u
#endif
