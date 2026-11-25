/* SPDX-License-Identifier: MIT */
#ifndef TR64_PLAT_N64_HOTDATA_H
#define TR64_PLAT_N64_HOTDATA_H
/* VR4300 data cache size; the hot data group is aligned to it. */
#define TR64_DCACHE_BYTES 8192u
/* Bytes from that boundary to the first hot object (multiple of 16). Measured
 * in docs/architecture.md section 6.2; the group is 6,688 bytes, so it covers
 * cache bytes 1,024 to 7,712. */
#ifndef TR64_HOT_DATA_PHASE
#define TR64_HOT_DATA_PHASE 1024u
#endif
/* The pools are carved from the first address at or above the image end that
 * is this many bytes (a multiple of 4096) past a data cache boundary, so the
 * pools' cache indices do not depend on the image size either. */
#ifndef TR64_POOL_PHASE
#define TR64_POOL_PHASE 4096u
#endif
extern char tr64_hot_data_start[],tr64_hot_data_end[];
#endif
