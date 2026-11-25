/* SPDX-License-Identifier: MIT */
/* Crash-report QR symbol: ISO/IEC 18004:2015 (QR Code 2005), model 2, version 13
 * (69x69 modules), error correction level M, alphanumeric mode, one fixed mask.
 * Payload packing: RFC 9285 base-45 over a versioned binary record (format 2).
 * No allocation, no tables of any size, no stdio: usable from the fatal path. */
#ifndef TR64_QR_H
#define TR64_QR_H
#include <stdint.h>

#define TR64_QR_MODULES 69u
#define TR64_QR_QUIET 4u
/* Symbol edge in modules including the quiet zone on both sides. */
#define TR64_QR_EDGE (TR64_QR_MODULES+2u*TR64_QR_QUIET)
/* Alphanumeric characters one symbol carries (version 13-M: 334 data bytes). */
#define TR64_QR_CHARS 483u
#define TR64_QR_FRAMES 7u
/* Fixed part of the record, in bytes, and what is left for call sites and message. */
#define TR64_QR_FIXED 273u
#define TR64_QR_ROOM 43u
/* Message bytes kept in preference to call sites. */
#define TR64_QR_MESSAGE_FIRST 32u
enum { TR64_QR_EXCEPTION=1,TR64_QR_ASSERT=2,TR64_QR_ERROR=3 };
/* flags bits */
enum {
 TR64_QR_F_STASH=1u<<0,    /* exception-entry stash valid (vector id, count) */
 TR64_QR_F_NESTED=1u<<1,   /* a second non-interrupt exception was taken before this report */
 TR64_QR_F_PC_MAPPED=1u<<2,/* a TLB probe of the PC found an entry */
 TR64_QR_F_UNIT_MAPPED=1u<<3, /* ... of the unit entry */
 TR64_QR_F_EPC_WORDS=1u<<4,/* epc_words were read */
 TR64_QR_F_ENTRY_WORDS=1u<<5, /* entry_words were read through kseg1 */
 TR64_QR_F_MAPPED_WORDS=1u<<6,/* mapped_words were read through the mapped address */
 TR64_QR_F_EXL=1u<<7       /* the exception was taken with EXL already set (inferred) */
};
/* One TLB entry as tlbr returns it; index 0xFF means none. */
typedef struct { uint8_t index;uint64_t hi;uint32_t lo0,lo1,mask; } tr64_qr_tlb_t;

/* Facts of one crash report. build is 12 upper-case hex digits; frames are
 * return addresses (call sites) after the PC, at most TR64_QR_FRAMES. gpr[i] is
 * the low 32 bits of $i (the record carries $1-$25 and $28-$31); bit i of
 * gpr_wide says the register's upper half is not the sign extension. */
typedef struct {
 uint8_t kind,code,nframes,flags,vec,count,tlb_valid,unit;
 const char *build,*message;
 uint32_t pc,ra,sr,cause,epc,tick,units,frame,gpr_wide;
 uint64_t badvaddr,entryhi,context;
 int32_t level;
 uint32_t gpr[32],epc_words[4],entry_words[4],mapped_words[4];
 tr64_qr_tlb_t pc_entry,unit_entry;
 uint32_t frames[TR64_QR_FRAMES];
} tr64_qr_record_t;

/* Write "TR64.2." + base45(record) + "." and return its length (at most
 * TR64_QR_CHARS, NUL excluded; out holds TR64_QR_CHARS+1 bytes). The message
 * keeps up to TR64_QR_MESSAGE_FIRST bytes before call sites take room; the
 * closing '.' keeps a reader that trims trailing blanks from eating a base-45
 * space. A frame address outside the three representable ranges ends the list. */
unsigned tr64_qr_pack(char *out,const tr64_qr_record_t *);

/* Paint text (QR alphanumeric characters only) as one symbol with its
 * quiet zone: scale*TR64_QR_EDGE pixels square at (x,y) of a 16-bit
 * framebuffer, light everywhere except dark modules. Returns 0 and paints
 * nothing when length exceeds TR64_QR_CHARS or a character is not in the set. */
int tr64_qr_draw(uint16_t *fb,unsigned stride,unsigned x,unsigned y,unsigned scale,
                const char *text,unsigned length,uint16_t dark,uint16_t light);
#endif
