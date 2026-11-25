/* SPDX-License-Identifier: MIT */
#ifndef TR64_UNWIND_H
#define TR64_UNWIND_H
#include <stdint.h>
#define TR64_UNWIND_FRAMES 8u
#define TR64_UNWIND_SCAN 1024u
/* The SDK's interrupt/exception frame: 32 ABI bytes + reg_block_t (inthandler.S
 * EXC_STACK_SIZE). plat/n64/code.c asserts it against the pinned SDK. */
#define TR64_UNWIND_EXC_FRAME 592u
typedef struct {
 uint32_t pc,sp,fp,ra,stack_begin,stack_end;
 int interrupted;
 /* Pinned SDK interrupt trampoline; zero range disables frame recognition. */
 uint32_t interrupt_begin,interrupt_end;
} tr64_unwind_context_t;
/* Return a decoded32-bit word, or0 without dereferencing inaccessible memory.
 * code is1 for instructions and0 for stack slots. No symbol table or heap.
 * This is a bounded, best-effort MIPS prolog trace, not a DWARF unwinder. */
typedef int (*tr64_unwind_read_t)(void *,uint32_t,int,uint32_t *);
uint32_t tr64_unwind(const tr64_unwind_context_t *,tr64_unwind_read_t,void *,
                     uint32_t *,uint32_t);
#endif
