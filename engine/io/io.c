/* SPDX-License-Identifier: MIT */
/* Ported from MIT ac3n64 04134cc: one DMA at a time, 4 KiB pieces,
 * 32 KiB per frame, completion publication only after the backend finishes. */
#include <string.h>
#include "tr64/io.h"
#include "engine/core/sys.h"
static tr64_io_req_t *queue[TR64_IO_REQUESTS];
static unsigned head, count;
static uint32_t cart_bytes, frame_bytes, inflight;
void tr64_io_init(uint32_t size)
{
 if (size > TR64_CART_BYTES) sys_error("io: cart exceeds 64 MiB");
 memset(queue, 0, sizeof(queue)); head = count = 0;
 cart_bytes = size; frame_bytes = inflight = 0;
}
int tr64_io_submit(tr64_io_req_t *r)
{
 unsigned i;
 if (!r || !r->dst || !r->size || r->size > cart_bytes ||
     r->offset > cart_bytes - r->size ||
     ((uintptr_t)r->dst & 15u) || (r->offset & 1u) || (r->size & 1u)) return TR64_IO_SUBMIT_INVALID;
 for (i = 0; i < count; ++i)
  if (queue[(head + i) % TR64_IO_REQUESTS] == r) return TR64_IO_SUBMIT_INVALID;
 if(count==TR64_IO_REQUESTS)return TR64_IO_SUBMIT_BUSY;
 r->done = 0; r->state = TR64_IO_QUEUED;
 queue[(head + count++) % TR64_IO_REQUESTS] = r;
 return 0;
}
void tr64_io_frame(void) { frame_bytes = 0; sys_io_frame_begin(); }
void tr64_io_pump(void)
{
 tr64_io_req_t *r; uint32_t n; int result;
 if (!count) return;
 r = queue[head];
 if (inflight) {
  result = sys_read_poll();
  if (!result) return;
  if (result < 0) r->state = TR64_IO_FAILED;
  else { r->done += inflight; r->state = r->done == r->size ? TR64_IO_DONE : TR64_IO_QUEUED; }
  inflight = 0;
  if (r->state == TR64_IO_DONE || r->state == TR64_IO_FAILED) {
   queue[head] = NULL; head = (head + 1) % TR64_IO_REQUESTS; --count;
   return;
  }
 }
 if (frame_bytes >= TR64_IO_FRAME) return;
 n = r->size - r->done;
 if (n > TR64_IO_PIECE) n = TR64_IO_PIECE;
 if (n > TR64_IO_FRAME - frame_bytes) n = TR64_IO_FRAME - frame_bytes;
 if (sys_read_start(r->dst + r->done, r->offset + r->done, n)) {
  r->state = TR64_IO_FAILED; queue[head] = NULL;
  head = (head + 1) % TR64_IO_REQUESTS; --count; return;
 }
 r->state = TR64_IO_ACTIVE; inflight = n; frame_bytes += n;
}
uint32_t tr64_io_frame_bytes(void) { return frame_bytes; }
uint32_t tr64_io_pending(void) { return count; }
