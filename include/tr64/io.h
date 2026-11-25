/* SPDX-License-Identifier: MIT */
/* Ported from MIT ac3n64 04134cc. */
#ifndef TR64_IO_H
#define TR64_IO_H
#include <stdint.h>
#define TR64_IO_REQUESTS 16
/* Caller retains the buffer until DONE or FAILED; no synchronous runtime API. */
typedef struct { uint8_t *dst; uint32_t offset, size, done; int state; } tr64_io_req_t;
enum { TR64_IO_IDLE, TR64_IO_QUEUED, TR64_IO_ACTIVE, TR64_IO_DONE, TR64_IO_FAILED };
enum { TR64_IO_SUBMIT_INVALID=-1, TR64_IO_SUBMIT_BUSY=-2 };
void tr64_io_init(uint32_t cart_size);
/* 0 queued; INVALID rejects malformed/binding/duplicate requests. BUSY means
 * a valid request cannot enter a full queue; it remains untouched for retry. */
int tr64_io_submit(tr64_io_req_t *request);
void tr64_io_frame(void);
void tr64_io_pump(void);
uint32_t tr64_io_frame_bytes(void);
/* Number of outstanding requests, queued or active; used to forbid rebinds. */
uint32_t tr64_io_pending(void);
#endif
