/* SPDX-License-Identifier: MIT */
/* Anchors of the hot data group (plat/n64/hot.ld, docs/architecture.md 6.2).
 *
 * The VR4300 data cache is 8 KiB, direct mapped by address, 16-byte lines: two
 * objects 8 KiB apart share lines. The group is one contiguous run: all of
 * .sbss (the small statics of libdragon, Tiny3D and ours), then the statics of
 * the frame loop listed in hot.ld at the start of .bss. The start anchor is
 * the first object of .sbss and is aligned to the cache size, so where the run
 * falls in the cache does not depend on how large .text, .rodata, .data or the
 * cold statics are; its size is the phase (bytes from the aligned boundary) at
 * which the run starts. The end anchor closes the hot part of .bss for
 * test/run/hot-check.py. Both are read by the placement report so the linker
 * keeps them.
 */
#include <stdint.h>
#include "plat/n64/hotdata.h"
char tr64_hot_data_start[TR64_HOT_DATA_PHASE] __attribute__((section(".sbss.tr64_hot_data_start"),aligned(TR64_DCACHE_BYTES)));
char tr64_hot_data_end[0] __attribute__((section(".bss.tr64_hot_data_end"),aligned(16)));
