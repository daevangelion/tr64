/* SPDX-License-Identifier: MIT */
/* tr64 hardware probe: plain libdragon (no N64_TLBCODE), one screen, one
 * photograph. Every step records what the CPU actually did for the mechanisms
 * the tlbcode units rely on, installs its own exception vectors so a faulting
 * step is reported and the next one runs, and ends with tlbcode_load's and
 * sys_code_probe's own register sequences. Glyph table: MIT ac3n64 04134cc via
 * plat/n64/fatal.c. Legend and QR layout: tools/hwprobe/README.md. */
#include <libdragon.h>
#include <vi.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "engine/core/qr.h"
#include "plat/n64/harness.h"

extern uint32_t rompak_search_ext(const char *ext, size_t *size);
extern uint64_t probe_call(uint64_t (*fn)(uint64_t), uint64_t arg);
extern uint64_t probe_jalr(uint32_t entry);
extern uint64_t probe_ret(void);
extern uint64_t probe_spin(uint32_t n);
extern uint64_t probe_load(uint32_t addr);
extern uint64_t probe_store(uint32_t addr);
extern uint64_t probe_load_exl(uint32_t addr, uint64_t sentinel);
extern void probe_boot_tlb(uint32_t wired);
extern const uint32_t probe_vec_template[16];
extern char probe_load_insn[], probe_store_insn[], probe_load_exl_insn[];

/* What probe_common saves (probe-asm.S writes these offsets). */
typedef struct {
	uint32_t vec, cause, sr, pad;
	uint64_t epc, badv, ctx, xctx, hi;
	uint32_t count, pad2;
} rec_t;
_Static_assert(sizeof(rec_t) == 64, "probe_rec layout");
volatile rec_t probe_rec __attribute__((aligned(8)));
uint64_t probe_jb[12] __attribute__((aligned(8)));

enum { P_PASS, P_FAIL, P_FAULT, P_FILL };
static const char letter[4] = { 'P', 'F', 'X', 'D' };

#define PAGE 4096u
#define VA_UNIT 0xC1000000u /* the code unit area */
#define VA_MISS 0xC2000000u /* never mapped */
#define VA_INV 0xC3000000u  /* mapped with V=0 */
#define VA_RO 0xC4000000u   /* mapped with D=0 */
#define VA_BIG 0xC0400000u  /* 1 MiB pair, physical 0.. */
#define SR_WIDE (C0_STATUS_KX | C0_STATUS_SX | C0_STATUS_UX)
#define NSLOT 4u            /* first slot the grid tests write */
#define HI_ARCH 0xC00000FFFFFFE0FFull /* R, VPN2 39:13, ASID: the architectural bits */

static uint8_t frames[2][PAGE] __attribute__((aligned(PAGE)));
static uint8_t image[2 * PAGE] __attribute__((aligned(16)));
static uint8_t rom_copy[2 * PAGE] __attribute__((aligned(16)));
static uint32_t rom_page;
static int rom_ok;
static uint32_t sr_base;
static uint16_t framebuffer[320 * 240] __attribute__((aligned(64)));

static uint32_t expected_word(unsigned page, unsigned offset)
{ return 0xD0A70000u + page * 0x10000u + (offset >> 2); }

/* The page pair every execution and data test maps: page 0 holds the code
 * stubs (+0x00 ori v0,0x5A5A; jr ra. +0x20 lw v0,0x20(t9); jr ra: reads +0x40),
 * then words from +0x100; page 1 is all words. The same bytes are in the ROM
 * (tools/hwprobe/page.py) and compared after the first DMA. */
static void image_build(uint8_t *to)
{
	uint32_t *w = (uint32_t *)to;
	unsigned i;
	for (i = 0; i < 2 * PAGE / 4; ++i) w[i] = 0x0000000Du; /* break */
	w[0] = 0x34025A5Au; w[1] = 0x03E00008u; w[2] = 0;
	w[8] = 0x8F220020u; w[9] = 0x03E00008u; w[10] = 0;
	w[16] = 0xC0DEC0DEu;
	for (i = 0x100 / 4; i < PAGE / 4; ++i) w[i] = expected_word(0, i * 4);
	for (i = 0; i < PAGE / 4; ++i) w[PAGE / 4 + i] = expected_word(1, i * 4);
	w[2 * PAGE / 4 - 1] = 0x48575052u; /* "HWPR" */
}

/* ---- CP0 and TLB ---------------------------------------------------- */
typedef struct { uint64_t hi; uint32_t lo0, lo1, mask; } tlb_t;
#define MFC0(reg) ({ uint32_t v_; __asm__ volatile("mfc0 %0,$" #reg : "=r"(v_)); v_; })
#define DMFC0(reg) ({ uint64_t v_; __asm__ volatile("dmfc0 %0,$" #reg : "=r"(v_)); v_; })

/* hi_style 0: mtc0 of the 32-bit address, as tlbcode's map_pair. 1: dmtc0 of
 * the full sign-extended 64-bit value. */
static void tlb_set(unsigned slot, unsigned hi_style, uint32_t va, uint32_t mask, uint32_t lo0, uint32_t lo1)
{
	C0_WRITE_INDEX(slot);
	C0_WRITE_PAGEMASK(mask);
	if (hi_style == 0) {
		C0_WRITE_ENTRYHI(va);
	} else {
		uint64_t wide = (uint64_t)(int64_t)(int32_t)va;
		__asm__ volatile("dmtc0 %0,$10; nop; nop" :: "r"(wide));
	}
	C0_WRITE_ENTRYLO0(lo0);
	C0_WRITE_ENTRYLO1(lo1);
	C0_TLBWI();
	C0_WRITE_PAGEMASK(0);
	C0_WRITE_ENTRYHI(0);
}
static void tlb_get(unsigned slot, tlb_t *e)
{
	C0_WRITE_INDEX(slot);
	C0_TLBR();
	e->hi = DMFC0(10);
	e->lo0 = (uint32_t)DMFC0(2);
	e->lo1 = (uint32_t)DMFC0(3);
	e->mask = MFC0(5);
	C0_WRITE_PAGEMASK(0);
	C0_WRITE_ENTRYHI(0);
}
static uint32_t tlb_probe(uint32_t va)
{
	uint32_t index;
	uint64_t wide = (uint64_t)(int64_t)(int32_t)va;
	__asm__ volatile("dmtc0 %0,$10; nop; nop" :: "r"(wide));
	C0_TLBP();
	index = MFC0(0);
	C0_WRITE_ENTRYHI(0);
	return index;
}
/* tlbcode's own invalid entries: distinct non-overlapping VPN2 above 1 GiB. */
static void tlb_clear(void)
{
	unsigned i;
	for (i = 0; i < 32; ++i) tlb_set(i, 0, 0x40000000u + i * 8192u, 0, 0, 0);
}

/* ---- guarded execution ------------------------------------------------ */
typedef struct { unsigned verdict; rec_t rec; uint64_t value; } outcome_t;
static void guarded(outcome_t *o, uint64_t (*fn)(uint64_t), uint64_t arg, uint32_t sr, uint64_t want)
{
	uint32_t before = probe_rec.count;
	uint64_t got;
	C0_WRITE_STATUS(sr);
	__asm__ volatile("nop; nop; nop; nop");
	got = probe_call(fn, arg);
	C0_WRITE_STATUS(sr_base);
	__asm__ volatile("nop; nop; nop; nop");
	o->value = got;
	memcpy(&o->rec, (const void *)&probe_rec, sizeof(rec_t));
	o->verdict = probe_rec.count != before ? P_FAULT : (uint32_t)got == (uint32_t)want ? P_PASS : P_FAIL;
}
static uint64_t call_jalr(uint64_t a) { return probe_jalr((uint32_t)a); }
static uint64_t call_load(uint64_t a) { return probe_load((uint32_t)a); }
static uint64_t call_store(uint64_t a) { return probe_store((uint32_t)a); }
static uint64_t call_exl(uint64_t a) { return probe_load_exl((uint32_t)a, 0x12345678u); }

/* ---- results ------------------------------------------------------------ */
typedef struct { uint8_t vec, exc, ok; uint32_t epc, badv; } miss_t;
static struct {
	uint32_t prid, config, wired, random;
	uint8_t t[5];          /* T1 TLB 1M, T2 TLB 4K, T3 TLBP, T4 (unused), T5 1M alias exec */
	uint8_t data[2][2][4]; /* style, mode, lo (1B 1F) x fill (DMA CPU) */
	uint8_t jr[2][2][8], lw[2][2][8]; /* style, mode, lo x cache (none, invalidate) x fill */
	uint8_t seq[2];        /* tlbcode_load sequence with and without the sys_code_probe snapshot */
	uint8_t alias[3], ic, spd, ratio; /* PA-alias fetch: KX=1 kseg2, KX=0 kseg2, KX=1 kuseg; Hit_Invalidate_I colour; kseg2 speed */
	uint64_t alias_value[3];
	uint32_t alias_ticks[3], ic_value[3];
	uint32_t ticks[2], rdram[2], pfn;
	miss_t miss[7], seqf;
	const char *what;
	int done;
} R;

static void rlog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void rlog(const char *fmt, ...)
{
	char line[256];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(line, sizeof line, fmt, ap);
	va_end(ap);
	fputs(line, stderr);
}
static void show_rec(const char *tag, const outcome_t *o)
{
	rlog("probe: %s -> %c value=%llx\n", tag, letter[o->verdict], (unsigned long long)o->value);
	if (o->verdict == P_FAULT)
		rlog("probe:   fault vec=%lu cause=%08lx sr=%08lx epc=%016llx badv=%016llx ctx=%016llx xctx=%016llx hi=%016llx\n",
			(unsigned long)o->rec.vec, (unsigned long)o->rec.cause, (unsigned long)o->rec.sr,
			(unsigned long long)o->rec.epc, (unsigned long long)o->rec.badv, (unsigned long long)o->rec.ctx,
			(unsigned long long)o->rec.xctx, (unsigned long long)o->rec.hi);
}

static void dma_fill(void *frame, uint32_t rom_off)
{
	data_cache_hit_writeback_invalidate(frame, PAGE);
	dma_wait_finished(dma_read_async(frame, rom_page + rom_off, PAGE));
}
static void cpu_fill(void *frame, const uint8_t *src)
{
	memcpy(frame, src, PAGE);
	data_cache_hit_writeback_invalidate(frame, PAGE);
}
/* 0: DMA from the ROM page; 1: CPU copy and write back. Returns whether both
 * frames now hold the image in RDRAM, read through kseg1. */
static int fill_frames(unsigned how)
{
	unsigned i;
	for (i = 0; i < 2; ++i) {
		if (how == 0) dma_fill(frames[i], i * PAGE);
		else cpu_fill(frames[i], image + i * PAGE);
	}
	return !memcmp(UncachedAddr(frames[0]), image, PAGE) && !memcmp(UncachedAddr(frames[1]), image + PAGE, PAGE);
}
static uint32_t pa(const void *p) { return PhysicalAddr(p); }

/* One page-pair mapping at VA_UNIT, 4 KiB pages, EntryLo flags (0x1B or 0x1F). */
static void map_unit(unsigned hi_style, uint32_t flags)
{
#ifdef HWPROBE_NEGATIVE
	flags &= ~2u; /* negative control: V=0 so every mapped test must report a fault */
#endif
	tlb_set(NSLOT, hi_style, VA_UNIT, 0, (pa(frames[0]) >> 6) | flags, (pa(frames[1]) >> 6) | flags);
}

static void render(void);
static void render_short(unsigned failed);
static void step(const char *what) { R.what = what; render(); }

/* ---- tests -------------------------------------------------------------- */
static void t_env(void)
{
	R.prid = MFC0(15); R.config = MFC0(16); R.wired = MFC0(6); R.random = MFC0(1);
	R.rdram[0] = *(volatile uint32_t *)0xA3F00000u; R.rdram[1] = *(volatile uint32_t *)0xA3F0000Cu;
	rlog("probe: env prid=%08lx config=%08lx sr=%08lx wired=%lu random=%lu\n", (unsigned long)R.prid,
		(unsigned long)R.config, (unsigned long)sr_base, (unsigned long)R.wired, (unsigned long)R.random);
}

static int readback_ok(const tlb_t *e, uint32_t va, uint32_t mask, uint32_t lo0, uint32_t lo1)
{
	uint64_t want = ((uint64_t)(int64_t)(int32_t)va) & HI_ARCH & ~(uint64_t)mask;
	return (e->hi & HI_ARCH) == want && (e->lo0 & 0x3FFFFFFFu) == lo0 && (e->lo1 & 0x3FFFFFFFu) == lo1 &&
		(e->mask & 0x01FFE000u) == mask;
}

/* T1/T2: write, read back, translate a data word through the mapping. */
static void t_readback(unsigned which)
{
	tlb_t e;
	uint32_t va = which ? VA_UNIT : VA_BIG, mask = which ? 0 : 0x1FE000u;
	uint32_t lo0, lo1, want;
	int ok;
	outcome_t o;
	fill_frames(1);
	if (which) {
		lo0 = (pa(frames[0]) >> 6) | 0x1B; lo1 = (pa(frames[1]) >> 6) | 0x1B;
	} else {
		lo0 = 0x1F; lo1 = (0x100000u >> 6) | 0x1F;
	}
	tlb_set(NSLOT + which, 0, va, mask, lo0, lo1);
	tlb_get(NSLOT + which, &e);
	ok = readback_ok(&e, va, mask, lo0, lo1);
	rlog("probe: T%u readback hi=%016llx lo0=%08lx lo1=%08lx mask=%08lx; wrote va=%08lx lo0=%08lx lo1=%08lx mask=%08lx -> %s\n",
		which + 1, (unsigned long long)e.hi, (unsigned long)e.lo0, (unsigned long)e.lo1, (unsigned long)e.mask,
		(unsigned long)va, (unsigned long)lo0, (unsigned long)lo1, (unsigned long)mask, ok ? "match" : "MISMATCH");
	if (which) {
		want = expected_word(0, 0x100);
		data_cache_hit_writeback_invalidate((void *)VA_UNIT, 2 * PAGE);
		guarded(&o, call_load, va + 0x100, sr_base, want);
	} else {
		/* Physical 0x1000 read uncached and through the 1 MiB mapping. */
		data_cache_hit_writeback_invalidate((void *)0x80001000u, 64);
		want = *(volatile uint32_t *)0xA0001000u;
		data_cache_hit_writeback_invalidate((void *)(VA_BIG + 0x1000u), 64);
		guarded(&o, call_load, va + 0x1000, sr_base, want);
	}
	show_rec(which ? "T2 data via 4K mapping" : "T1 data via 1M mapping", &o);
	R.t[which] = (uint8_t)(o.verdict == P_FAULT ? P_FAULT : ok && o.verdict == P_PASS ? P_PASS : P_FAIL);
}

static void t_tlbp(void)
{
	uint32_t hit = tlb_probe(VA_UNIT), odd = tlb_probe(VA_UNIT + PAGE), none = tlb_probe(VA_MISS);
	rlog("probe: T3 tlbp even=%08lx odd=%08lx unmapped=%08lx; expected index %u twice, then bit 31\n",
		(unsigned long)hit, (unsigned long)odd, (unsigned long)none, NSLOT + 1);
	R.t[2] = hit == NSLOT + 1 && odd == NSLOT + 1 && (none & 0x80000000u) ? P_PASS : P_FAIL;
}

static void t_data(void)
{
	unsigned style, mode, lo, fill;
	for (style = 0; style < 2; ++style)
	for (mode = 0; mode < 2; ++mode)
	for (lo = 0; lo < 2; ++lo)
	for (fill = 0; fill < 2; ++fill) {
		outcome_t o;
		unsigned verdict;
		uint32_t flags = lo ? 0x1F : 0x1B, sr = mode ? sr_base & ~SR_WIDE : sr_base;
		map_unit(style, flags);
		if (!fill_frames(fill)) verdict = P_FILL;
		else {
			data_cache_hit_writeback_invalidate((void *)VA_UNIT, 2 * PAGE);
			guarded(&o, call_load, VA_UNIT + 0x104, sr, expected_word(0, 0x104));
			verdict = o.verdict;
			if (verdict == P_PASS) {
				guarded(&o, call_load, VA_UNIT + PAGE + 0x104, sr, expected_word(1, 0x104));
				verdict = o.verdict;
			}
		}
		if (verdict != P_PASS) {
			char tag[64];
			snprintf(tag, sizeof tag, "DATA style=%u mode=%u lo=%02lx fill=%u", style, mode, (unsigned long)flags, fill);
			if (verdict == P_FILL) rlog("probe: %s -> D frames differ from the image after the fill\n", tag);
			else show_rec(tag, &o);
		}
		R.data[style][mode][lo * 2 + fill] = (uint8_t)verdict;
	}
}

static void t_alias(void)
{
	outcome_t o;
	/* The 1 MiB entry of T1 (VA_BIG -> physical 0): call probe_ret at its alias. */
	uint32_t alias = VA_BIG + ((uint32_t)(uintptr_t)probe_ret & 0x1FFFFFu);
	tlb_set(NSLOT, 0, VA_BIG, 0x1FE000u, 0x1F, (0x100000u >> 6) | 0x1F);
	inst_cache_invalidate_all();
	guarded(&o, call_jalr, alias, sr_base, 0x5A5A);
	show_rec("T5 execute probe_ret through the 1 MiB mapping", &o);
	R.t[4] = (uint8_t)o.verdict;
}
/* Does a mapped fetch go through the TLB, or is VA & 0x1FFFFFFF taken as the
 * physical address (what a core that treats ckseg2 fetches as unmapped does)?
 * The mapped frame returns 1; the physical address VA & 0x1FFFFFFF holds code
 * that returns 2. Code: ori v0,zero,N would be 0x3402000N; jr ra; li as delay. */
#define JR_RA 0x03E00008u
#define LI_V0(n) (0x24020000u | (n))
static uint64_t call_many(uint64_t a);
static void t_alias_pa(unsigned which, unsigned kx, uint32_t va)
{
	extern char end[];
	outcome_t o;
	char tag[96];
	volatile uint32_t *marker = (volatile uint32_t *)0xA0200000u;
	volatile uint32_t *stub = UncachedAddr(frames[0]);
	if ((uintptr_t)end >= 0x80200000u || pa(frames[0]) == 0x200000u || pa(frames[1]) == 0x200000u) {
		rlog("probe: alias test skipped: image end %p reaches physical 0x200000\n", end);
		R.alias[which] = 2;
		return;
	}
	marker[0] = JR_RA; marker[1] = LI_V0(2);
	data_cache_hit_writeback_invalidate((void *)0x80200000u, 32);
	data_cache_hit_writeback_invalidate(frames[0], 2 * PAGE);
	stub[0] = JR_RA; stub[1] = LI_V0(1);
	tlb_set(NSLOT, 0, va, 0, (pa(frames[0]) >> 6) | 0x1B, (pa(frames[1]) >> 6) | 0x1B);
	inst_cache_invalidate_all();
	{
		uint32_t t0, t1;
		guarded(&o, call_jalr, va, kx ? sr_base : sr_base & ~SR_WIDE, 1);
		R.alias_value[which] = o.value;
		R.alias[which] = (uint8_t)(o.verdict == P_FAULT ? 3 : o.value == 1 ? 0 : o.value == 2 ? 1 : 2);
		t0 = C0_COUNT();
		guarded(&o, call_many, va, kx ? sr_base : sr_base & ~SR_WIDE, R.alias_value[which]);
		t1 = C0_COUNT();
		R.alias_ticks[which] = t1 - t0;
	}
	snprintf(tag, sizeof tag, "AL KX=%u fetch VA %08lx (mapped: 1, PA alias: 2)", kx, (unsigned long)va);
	show_rec(tag, &o);
}

/* Hit_Invalidate_I on a mapped address whose physical colour differs from its
 * virtual colour: a core that indexes the cache operation by the translated
 * address leaves the stale line in place. */
static uint8_t big[6 * PAGE] __attribute__((aligned(PAGE)));
static void t_icache(void)
{
	outcome_t a;
	unsigned k, round;
	volatile uint32_t *w;
	for (k = 0; k < 4 && (((pa(big) >> 12) + k) & 3u) != 1; ++k) { }
	w = UncachedAddr(big + k * PAGE);
	data_cache_hit_writeback_invalidate(big, sizeof big);
	tlb_set(NSLOT, 0, VA_UNIT, 0, (pa(big + k * PAGE) >> 6) | 0x1B, (pa(big + (k + 1) * PAGE) >> 6) | 0x1B);
	R.ic = 0;
	/* One frame, two stubs at the same virtual colour: run A (v0=1) so its line is
	 * cached, overwrite the frame in RDRAM with B (v0=2), invalidate by hit
	 * operation (round 0), by index range (1) or whole cache (2), run again. */
	for (round = 0; round < 3; ++round) {
		w[0] = JR_RA; w[1] = LI_V0(1);
		inst_cache_invalidate_all();
		guarded(&a, call_jalr, VA_UNIT, sr_base, 1);
		if (a.verdict != P_PASS) { R.ic = (uint8_t)(a.verdict == P_FAULT ? 3 : 2); R.ic_value[round] = (uint32_t)a.value; continue; }
		w[0] = JR_RA; w[1] = LI_V0(2);
		if (round == 0) inst_cache_hit_invalidate((void *)VA_UNIT, 32);
		else if (round == 1) inst_cache_index_invalidate((void *)VA_UNIT, 32);
		else inst_cache_invalidate_all();
		guarded(&a, call_jalr, VA_UNIT, sr_base, 2);
		R.ic_value[round] = (uint32_t)a.value;
		if (a.verdict == P_FAULT) R.ic = 3;
		else if (a.value != 2 && !R.ic) R.ic = a.value == 1 ? 1 : 2;
	}
	rlog("probe: IC frame colour %lu: after Hit_Invalidate_I %lx, Index_Invalidate_I range %lx, whole cache %lx (want 2; 1 is a stale line) -> %c\n",
		(unsigned long)(pa(big + k * PAGE) >> 12 & 3u), (unsigned long)R.ic_value[0], (unsigned long)R.ic_value[1],
		(unsigned long)R.ic_value[2], "PFFX"[R.ic & 3]);
}

static uint64_t call_many(uint64_t a)
{
	unsigned n;
	uint64_t v = 0;
	for (n = 0; n < 1000; ++n) v = probe_jalr((uint32_t)a);
	return v;
}

/* Time the same loop in kseg0 and through the identity-mapped kseg2 alias (as
 * the resident text is mapped). Count runs at half the CPU clock. */
static uint64_t call_spin(uint64_t a) { return ((uint64_t (*)(uint32_t))(uintptr_t)a)(1000000); }
/* EntryLo0 PFN width: write 0x3FFFFFC0 to an invalid entry and read it back
 * (24 PFN bits on a VR4300). The entry is restored to tlbcode's invalid value. */
static void t_pfn(void)
{
	tlb_t e;
	tlb_set(31, 0, 0x40000000u + 31 * 8192u, 0, 0x3FFFFFC0u, 0);
	tlb_get(31, &e);
	R.pfn = e.lo0 & 0x3FFFFFFFu;
	tlb_set(31, 0, 0x40000000u + 31 * 8192u, 0, 0, 0);
	rlog("probe: PFN EntryLo0 wrote 3fffffc0 read %08lx\n", (unsigned long)e.lo0);
}
static void t_speed(void)
{
	outcome_t o;
	uint32_t alias = 0xC0000000u + ((uint32_t)(uintptr_t)probe_spin & 0x1FFFFFFFu), t0, t1;
	tlb_set(NSLOT, 0, 0xC0000000u, 0x1FE000u, 0x1F, (0x100000u >> 6) | 0x1F);
	probe_spin(10);
	t0 = C0_COUNT(); guarded(&o, call_spin, (uint32_t)(uintptr_t)probe_spin, sr_base, 0x5A5A); t1 = C0_COUNT();
	R.ticks[0] = t1 - t0;
	inst_cache_invalidate_all();
	guarded(&o, call_spin, alias, sr_base, 0x5A5A);
	t0 = C0_COUNT(); guarded(&o, call_spin, alias, sr_base, 0x5A5A); t1 = C0_COUNT();
	R.ticks[1] = t1 - t0;
	R.ratio = (uint8_t)(R.ticks[0] ? (R.ticks[1] / R.ticks[0] * 16u > 255u ? 255u : R.ticks[1] * 16u / R.ticks[0]) : 255u);
	R.spd = o.verdict != P_PASS ? o.verdict : R.ticks[1] < 3 * R.ticks[0] ? P_PASS : P_FAIL;
	rlog("probe: SP kseg0 %lu ticks, kseg2 alias %08lx %lu ticks, ratio*16=%u -> %c\n", (unsigned long)R.ticks[0],
		(unsigned long)alias, (unsigned long)R.ticks[1], R.ratio, letter[R.spd]);
}

static void t_exec(void)
{
	unsigned style, mode, lo, cache, fill, stub;
	for (stub = 0; stub < 2; ++stub)
	for (style = 0; style < 2; ++style)
	for (mode = 0; mode < 2; ++mode)
	for (lo = 0; lo < 2; ++lo)
	for (cache = 0; cache < 2; ++cache)
	for (fill = 0; fill < 2; ++fill) {
		outcome_t o;
		unsigned verdict;
		uint32_t flags = lo ? 0x1F : 0x1B, sr = mode ? sr_base & ~SR_WIDE : sr_base;
		map_unit(style, flags);
		if (!fill_frames(fill)) verdict = P_FILL;
		else {
			inst_cache_invalidate_all();
			if (cache) inst_cache_hit_invalidate((void *)VA_UNIT, 2 * PAGE);
			guarded(&o, call_jalr, VA_UNIT + stub * 0x20, sr, stub ? 0xC0DEC0DEu : 0x5A5Au);
			verdict = o.verdict;
		}
		if (verdict != P_PASS) {
			char tag[80];
			snprintf(tag, sizeof tag, "EXEC %s style=%u mode=%u lo=%02lx cache=%u fill=%u", stub ? "LW" : "JR", style,
				mode, (unsigned long)flags, cache, fill);
			if (verdict == P_FILL) rlog("probe: %s -> D frames differ from the image after the fill\n", tag);
			else show_rec(tag, &o);
		}
		(stub ? R.lw : R.jr)[style][mode][lo * 4 + cache * 2 + fill] = (uint8_t)verdict;
	}
}

static uint64_t sext(uint32_t v) { return (uint64_t)(int64_t)(int32_t)v; }
/* Check one TLB exception against what the architecture requires. */
static int tlb_exception_ok(const outcome_t *o, unsigned vec, unsigned exc, uint64_t epc, uint64_t badv, int wide)
{
	unsigned got = (o->rec.cause >> 2) & 31u;
	uint32_t va32 = (uint32_t)badv;
	int ok = o->verdict == P_FAULT && o->rec.vec == vec && got == exc && o->rec.epc == epc && o->rec.badv == badv;
	/* Context.BadVPN2 holds VA 31:13; EntryHi gets the faulting VPN2; XContext holds VA 39:13 and R. */
	ok = ok && ((o->rec.ctx >> 4) & 0x7FFFFu) == ((va32 >> 13) & 0x7FFFFu);
	ok = ok && ((uint32_t)o->rec.hi & 0xFFFFE000u) == (va32 & 0xFFFFE000u);
	if (wide) ok = ok && ((o->rec.xctx >> 4) & 0x7FFFFFFu) == ((badv >> 13) & 0x7FFFFFFu) && ((o->rec.xctx >> 31) & 3u) == (badv >> 62);
	return ok;
}
static void record_miss(miss_t *m, const char *name, const outcome_t *o, unsigned vec, unsigned exc, uint64_t epc, uint64_t badv, int wide)
{
	char tag[96];
	m->ok = (uint8_t)(tlb_exception_ok(o, vec, exc, epc, badv, wide) ? P_PASS : P_FAIL);
	m->vec = (uint8_t)o->rec.vec; m->exc = (uint8_t)((o->rec.cause >> 2) & 31u);
	m->epc = (uint32_t)o->rec.epc; m->badv = (uint32_t)o->rec.badv;
	snprintf(tag, sizeof tag, "%s (want vec %u exc %u epc %016llx badv %016llx)", name, vec, exc,
		(unsigned long long)epc, (unsigned long long)badv);
	show_rec(tag, o);
}
static void t_miss(void)
{
	outcome_t o;
	uint64_t load_epc = sext((uint32_t)(uintptr_t)probe_load_insn);
	uint64_t store_epc = sext((uint32_t)(uintptr_t)probe_store_insn);
	uint64_t exl_epc = 0x12345678u;
	uint32_t narrow = sr_base & ~SR_WIDE;
	tlb_clear();
	/* M1: load miss, XTLB refill vector (KX=1, EXL=0). */
	guarded(&o, call_load, VA_MISS, sr_base, 0); record_miss(&R.miss[0], "M1 load miss KX=1", &o, 1, 2, load_epc, sext(VA_MISS), 1);
	/* M2: the same with KX=0: TLB refill vector. */
	guarded(&o, call_load, VA_MISS, narrow, 0); record_miss(&R.miss[1], "M2 load miss KX=0", &o, 0, 2, load_epc, sext(VA_MISS), 0);
	/* M3: store miss. */
	guarded(&o, call_store, VA_MISS, sr_base, 0); record_miss(&R.miss[2], "M3 store miss KX=1", &o, 1, 3, store_epc, sext(VA_MISS), 1);
	/* M4: instruction fetch miss: EPC and BadVAddr are the target. */
	guarded(&o, call_jalr, VA_MISS, sr_base, 0); record_miss(&R.miss[3], "M4 fetch miss KX=1", &o, 1, 2, sext(VA_MISS), sext(VA_MISS), 1);
	/* M5: V=0 entry: TLB invalid, general vector. */
	tlb_set(NSLOT, 0, VA_INV, 0, 0x19, 0x19);
	guarded(&o, call_load, VA_INV, sr_base, 0); record_miss(&R.miss[4], "M5 load of V=0 page", &o, 3, 2, load_epc, sext(VA_INV), 1);
	/* M6: store to a D=0 page: TLB modification. */
	tlb_set(NSLOT + 1, 0, VA_RO, 0, (pa(frames[0]) >> 6) | 0x1B, (pa(frames[1]) >> 6) | 0x1B);
	guarded(&o, call_store, VA_RO, sr_base, 0); record_miss(&R.miss[5], "M6 store to D=0 page", &o, 3, 1, store_epc, sext(VA_RO), 1);
	/* M7: load miss with EXL already set and EPC preset: general vector, EPC kept. */
	guarded(&o, call_exl, VA_MISS, sr_base, 0); record_miss(&R.miss[6], "M7 load miss with EXL set", &o, 3, 2, exl_epc, sext(VA_MISS), 1);
	(void)exl_epc;
	(void)probe_load_exl_insn;
}

/* tlbcode.c map_pair, verbatim. */
static void map_pair(unsigned slot, uint32_t va, uint32_t even, uint32_t odd)
{
	uint32_t hi = C0_ENTRYHI(), mask = C0_PAGEMASK();
	C0_WRITE_INDEX(slot);
	C0_WRITE_PAGEMASK(0);
	C0_WRITE_ENTRYHI(va);
	C0_WRITE_ENTRYLO0(even);
	C0_WRITE_ENTRYLO1(odd);
	C0_TLBWI();
	C0_WRITE_ENTRYHI(hi);
	C0_WRITE_PAGEMASK(mask);
	asm volatile("sync" ::: "memory");
}
/* plat/n64/code.c snapshot, verbatim. */
typedef struct { uint32_t hi, lo0, lo1, mask; } snap_t;
static void snapshot(snap_t entries[32])
{
	uint32_t hi, lo0, lo1, mask, index, i;
	disable_interrupts();
	__asm__ volatile("mfc0 %0,$10; mfc0 %1,$2; mfc0 %2,$3; mfc0 %3,$5; mfc0 %4,$0":"=r"(hi),"=r"(lo0),"=r"(lo1),"=r"(mask),"=r"(index));
	for (i = 0; i < 32; ++i) {
		__asm__ volatile("mtc0 %4,$0; nop; nop; tlbr; nop; nop; mfc0 %0,$10; mfc0 %1,$2; mfc0 %2,$3; mfc0 %3,$5":"=r"(entries[i].hi),"=r"(entries[i].lo0),"=r"(entries[i].lo1),"=r"(entries[i].mask):"r"(i):"memory");
	}
	__asm__ volatile("mtc0 %0,$10; mtc0 %1,$2; mtc0 %2,$3; mtc0 %3,$5; mtc0 %4,$0; nop; nop"::"r"(hi),"r"(lo0),"r"(lo1),"r"(mask),"r"(index):"memory");
	enable_interrupts();
}
static unsigned seq_run(int with_snapshot)
{
	outcome_t a, b;
	unsigned slot = 2, verdict;
	snap_t rows[32];
	uint32_t i, mapped = 0;
	/* tlbcode_load: DMA both pages into their frames, map the pair, invalidate
	 * the virtual range. */
	for (i = 0; i < 2; ++i) {
		data_cache_hit_writeback_invalidate(frames[i], PAGE);
		dma_wait_finished(dma_read_async(frames[i], rom_page + i * PAGE, PAGE));
	}
	disable_interrupts();
	map_pair(slot, VA_UNIT, (PhysicalAddr(frames[0]) >> 6) | 0x1B, (PhysicalAddr(frames[1]) >> 6) | 0x1B);
	inst_cache_hit_invalidate((void *)VA_UNIT, 2 * PAGE);
	asm volatile("sync" ::: "memory");
	enable_interrupts();
	if (with_snapshot) {
		/* sys_code_probe: the mapping must show in a TLB snapshot. */
		snapshot(rows);
		for (i = 0; i < 2; ++i) {
			uint32_t va = VA_UNIT + PAGE * i, j;
			for (j = 0; j < 32; ++j)
				if (!rows[j].mask && (rows[j].hi & ~8191u) == (va & ~8191u) && ((va & 4096u ? rows[j].lo1 : rows[j].lo0) & 2u)) { ++mapped; break; }
		}
		rlog("probe: SEQ snapshot mapped_pages=%lu expected=2\n", (unsigned long)mapped);
	}
	guarded(&a, call_jalr, VA_UNIT, sr_base, 0x5A5A);
	guarded(&b, call_jalr, VA_UNIT + 0x20, sr_base, 0xC0DEC0DEu);
	show_rec(with_snapshot ? "SEQ+snapshot unit entry JR" : "SEQ unit entry JR", &a);
	show_rec(with_snapshot ? "SEQ+snapshot unit entry LW" : "SEQ unit entry LW", &b);
	verdict = a.verdict != P_PASS ? a.verdict : b.verdict;
	if (verdict == P_PASS && with_snapshot && mapped != 2) verdict = P_FAIL;
	if (verdict != P_PASS) {
		const outcome_t *w = a.verdict != P_PASS ? &a : &b;
		R.seqf.vec = (uint8_t)w->rec.vec; R.seqf.exc = (uint8_t)((w->rec.cause >> 2) & 31u);
		R.seqf.epc = (uint32_t)w->rec.epc; R.seqf.badv = (uint32_t)w->rec.badv; R.seqf.ok = (uint8_t)verdict;
	}
	/* tlbcode_unload. */
	disable_interrupts();
	inst_cache_hit_invalidate((void *)VA_UNIT, 2 * PAGE);
	data_cache_hit_invalidate((void *)VA_UNIT, 2 * PAGE);
	map_pair(slot, 0x40000000u + slot * 8192u, 0, 0);
	enable_interrupts();
	return verdict;
}
static void t_seq(void)
{
	/* The tlbcode entrypoint's TLB block (wired = 2: 4 MiB of 1 MiB pages at 0xC0000000). */
	probe_boot_tlb(2);
	R.seq[0] = (uint8_t)seq_run(1);
	R.seq[1] = (uint8_t)seq_run(0);
}

/* ---- screen -------------------------------------------------------------- */
static const char glyph_names[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-/:";
static const uint16_t glyph_bits[] = {
	31599, 11415, 29671, 29647, 23497, 31183, 31215, 29257, 31727, 31695,
	11245, 27566, 31015, 27502, 31207, 31204, 31083, 23533, 29847, 4718,
	23469, 18727, 24429, 24445, 31599, 31716, 31609, 31661, 31183, 29842,
	23407, 23402, 23421, 23213, 23186, 29351, 448, 4772, 1040
};
#define G(a, b, c, d, e) ((a) << 12 | (b) << 9 | (c) << 6 | (d) << 3 | (e))
static uint16_t glyph(unsigned char c)
{
	const char *p;
	if (c >= 'a' && c <= 'z') c = (unsigned char)(c - 'a' + 'A');
	p = strchr(glyph_names, c);
	if (p && c) return glyph_bits[p - glyph_names];
	switch (c) {
	case ' ': return 0;
	case '_': return G(0, 0, 0, 0, 7);
	case '.': return G(0, 0, 0, 0, 2);
	case ',': return G(0, 0, 0, 2, 4);
	case '=': return G(0, 7, 0, 7, 0);
	case '+': return G(0, 2, 7, 2, 0);
	case '!': return G(2, 2, 2, 0, 2);
	case '(': return G(1, 2, 2, 2, 1);
	case ')': return G(4, 2, 2, 2, 4);
	default: return G(7, 1, 3, 0, 2);
	}
}
#undef G
static void text(uint16_t *fb, unsigned x, unsigned y, unsigned scale, uint16_t colour, const char *s)
{
	for (; *s; ++s, x += 4 * scale) {
		uint16_t bits = glyph((unsigned char)*s);
		unsigned r, c, sy, sx;
		for (r = 0; r < 5; ++r) for (c = 0; c < 3; ++c)
			if (bits & (1u << (14 - r * 3 - c)))
				for (sy = 0; sy < scale; ++sy) for (sx = 0; sx < scale; ++sx)
					fb[(y + r * scale + sy) * 320 + x + c * scale + sx] = colour;
	}
}
static void line(uint16_t *fb, unsigned *y, uint16_t colour, const char *fmt, ...) __attribute__((format(printf, 4, 5)));
static void line(uint16_t *fb, unsigned *y, uint16_t colour, const char *fmt, ...)
{
	char s[48];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(s, sizeof s, fmt, ap);
	va_end(ap);
	text(fb, 4, *y, 1, colour, s);
	*y += 6;
}
static void show_vi(uint16_t *fb)
{
	const vi_timing_preset_t *p = &VI_TIMING_NTSC;
	int tv = get_tv_type();
	if (tv == TV_PAL) p = &VI_TIMING_PAL; else if (tv == TV_MPAL) p = &VI_TIMING_MPAL;
	*VI_CTRL = 0; *VI_ORIGIN = PhysicalAddr(fb); *VI_WIDTH = 320; *VI_V_INTR = 2;
	*VI_BURST = p->vi_burst; *VI_V_TOTAL = p->vi_v_total; *VI_H_TOTAL = p->vi_h_total;
	*VI_H_TOTAL_LEAP = p->vi_h_total_leap; *VI_V_BURST = p->vi_v_burst;
	*VI_H_VIDEO = VI_H_VIDEO_SET(p->display.x0, p->display.x0 + p->display.width);
	*VI_V_VIDEO = VI_V_VIDEO_SET(p->display.y0, p->display.y0 + p->display.height);
	*VI_X_SCALE = (320 << 10) / p->display.width;
	*VI_Y_SCALE = (240 << 11) / p->display.height;
	if (p->display.x0 < 128 && *VI_X_SCALE == 0x200) *VI_X_SCALE = 0x201;
	__asm__ volatile("sync" ::: "memory");
	*VI_CTRL = VI_PIXEL_ADVANCE_DEFAULT | VI_AA_MODE_NONE | VI_CTRL_TYPE_16_BPP;
}

static void cells(char *out, const uint8_t *v, unsigned n)
{
	unsigned i;
	for (i = 0; i < n; ++i) out[i] = letter[v[i] & 3];
	out[n] = 0;
}

/* The record in the symbol: see tools/hwprobe/README.md and decode.py. */
typedef struct { uint8_t b[97]; unsigned n; } blob_t;
static void put8(blob_t *k, unsigned v) { if (k->n < sizeof k->b) k->b[k->n++] = (uint8_t)v; }
static void put32(blob_t *k, uint32_t v) { put8(k, v >> 24); put8(k, v >> 16); put8(k, v >> 8); put8(k, v); }
static void put24(blob_t *k, uint32_t v) { put8(k, v >> 16); put8(k, v >> 8); put8(k, v); }
static void putcells(blob_t *k, const uint8_t *v, unsigned n)
{
	unsigned i;
	for (i = 0; i < n; i += 4) {
		unsigned b = 0, j;
		for (j = 0; j < 4; ++j) b = b << 2 | (i + j < n ? v[i + j] & 3u : 0u);
		put8(k, b);
	}
}
static void putmiss(blob_t *k, const miss_t *m)
{
	put8(k, (unsigned)(m->ok & 1u) << 7 | (unsigned)(m->vec & 3u) << 5 | (m->exc & 31u));
	put24(k, m->epc);
	put32(k, m->badv);
}
static const char b45[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:";
static unsigned base45(char *out, const uint8_t *in, unsigned n)
{
	unsigned i, o = 0;
	for (i = 0; i < n; i += 2) {
		if (i + 1 < n) {
			unsigned v = in[i] * 256u + in[i + 1];
			out[o++] = b45[v % 45]; out[o++] = b45[v / 45 % 45]; out[o++] = b45[v / 2025];
		} else {
			out[o++] = b45[in[i] % 45]; out[o++] = b45[in[i] / 45];
		}
	}
	return o;
}
static unsigned failures(char *ids, unsigned room)
{
	unsigned n = 0, i, s, m, c;
	static const char *names[] = { "T1", "T2", "T3", "T4", "T5" };
	ids[0] = 0;
#define NOTE(name) do { if (strlen(ids) + strlen(name) + 2 < room) { strcat(ids, name); strcat(ids, " "); } ++n; } while (0)
	for (i = 0; i < 5; ++i) if (i != 3 && R.t[i] != P_PASS) NOTE(names[i]);
	for (s = 0; s < 2; ++s) for (m = 0; m < 2; ++m) {
		int bad = 0;
		for (c = 0; c < 4; ++c) bad |= R.data[s][m][c] != P_PASS;
		if (bad) NOTE("DT");
		bad = 0;
		for (c = 0; c < 8; ++c) bad |= R.jr[s][m][c] != P_PASS;
		if (bad) NOTE("JR");
		bad = 0;
		for (c = 0; c < 8; ++c) bad |= R.lw[s][m][c] != P_PASS;
		if (bad) NOTE("LW");
	}
	for (i = 0; i < 7; ++i) if (R.miss[i].ok != P_PASS) { char name[4] = { 'M', (char)('1' + i), 0 }; NOTE(name); }
	for (i = 0; i < 2; ++i) if (R.seq[i] != P_PASS) NOTE("SQ");
	for (i = 0; i < 3; ++i) if (R.alias[i] != 0) NOTE("AL");
	if (R.ic != 0) NOTE("IC");
	if (R.spd != P_PASS) NOTE("SP");
#undef NOTE
	return n;
}
static void render(void)
{
	uint16_t *fb = UncachedAddr(framebuffer);
	unsigned y = 4, i, s, m;
	char buf[44], ids[34], symbol[TR64_QR_CHARS + 1];
	blob_t k = { { 0 }, 0 };
	for (i = 0; i < 320 * 240; ++i) fb[i] = 0x1001u;
	text(fb, 4, y, 2, 0xf801u, "TR64 HWPROBE 1"); y += 12;
	if (R.done) {
		unsigned n = failures(ids, sizeof ids);
		if (n) { snprintf(buf, sizeof buf, "FAIL %.33s", ids); text(fb, 4, y, 2, 0xf801u, buf); }
		else text(fb, 4, y, 2, 0x07c1u, "ALL PASS");
	} else {
		snprintf(buf, sizeof buf, "RUNNING %.30s", R.what ? R.what : "");
		text(fb, 4, y, 2, 0xffc1u, buf);
	}
	y += 12;
	line(fb, &y, 0xafffu, "PRID %08lX CFG %08lX SR %08lX W%lu", (unsigned long)R.prid, (unsigned long)R.config,
		(unsigned long)sr_base, (unsigned long)R.wired);
	y = 46;
	{
		static const char *names[] = { "T1 TLB 1M RB", "T2 TLB 4K RB", "T3 TLBP", 0, "T5 1M ALIAS EXEC" };
		for (i = 0; i < 5; ++i) if (names[i]) line(fb, &y, 0xffffu, "%-18s %c", names[i], letter[R.t[i] & 3]);
	}
	for (s = 0; s < 2; ++s) for (m = 0; m < 2; ++m) {
		char a[9], b[9];
		cells(a, &R.data[s][m][0], 2); cells(b, &R.data[s][m][2], 2);
		line(fb, &y, 0xffffu, "DT %c %c 1B:%s 1F:%s", s ? 'B' : 'A', m ? '0' : '1', a, b);
	}
	for (i = 0; i < 2; ++i) for (s = 0; s < 2; ++s) for (m = 0; m < 2; ++m) {
		char a[9], b[9];
		const uint8_t *v = (i ? R.lw : R.jr)[s][m];
		cells(a, v, 4); cells(b, v + 4, 4);
		line(fb, &y, 0xffffu, "%s %c %c 1B:%s 1F:%s", i ? "LW" : "JR", s ? 'B' : 'A', m ? '0' : '1', a, b);
	}
	line(fb, &y, 0xffffu, "SQ LOAD+SNAP %c  LOAD %c", letter[R.seq[0] & 3], letter[R.seq[1] & 3]);
	line(fb, &y, 0xffffu, "AL KX1 %c KX0 %c KU %c IC %c", "PAFX"[R.alias[0] & 3], "PAFX"[R.alias[1] & 3], "PAFX"[R.alias[2] & 3], "PFFX"[R.ic & 3]);
	line(fb, &y, 0xffffu, "SP K0 %luK K2 %luK %c", (unsigned long)(R.ticks[0] / 1000), (unsigned long)(R.ticks[1] / 1000), letter[R.spd & 3]);
	for (i = 0; i < 7; ++i)
		line(fb, &y, 0xffffu, "M%u V%u E%02u %c", i + 1, R.miss[i].vec, R.miss[i].exc, letter[R.miss[i].ok & 3]);
	line(fb, &y, 0xafffu, "A MTC0 B DMTC0  1 KX1 0 KX0");
	line(fb, &y, 0xafffu, "CELLS C0DMA C0CPU C1DMA C1CPU");
	line(fb, &y, 0xafffu, "C1 = ICACHE HIT-INVALIDATE");
	line(fb, &y, 0xafffu, "P PASS F WRONG X FAULT D FILL");
	line(fb, &y, 0xafffu, "AL A = PHYSICAL ALIAS TAKEN");
	/* Symbol. */
	put8(&k, 1);
	put32(&k, R.prid); put32(&k, sr_base); put8(&k, R.wired);
	put8(&k, R.t[0] << 6 | R.t[1] << 4 | R.t[2] << 2 | R.t[4]);
	/* Data cells are one bit each (pass or not); the screen shows which letter. */
	{
		unsigned bits = 0;
		for (s = 0; s < 2; ++s) for (m = 0; m < 2; ++m) for (i = 0; i < 4; ++i) bits = bits << 1 | (R.data[s][m][i] != P_PASS);
		put8(&k, bits >> 8); put8(&k, bits);
	}
	for (s = 0; s < 2; ++s) for (m = 0; m < 2; ++m) putcells(&k, R.jr[s][m], 8);
	for (s = 0; s < 2; ++s) for (m = 0; m < 2; ++m) putcells(&k, R.lw[s][m], 8);
	put8(&k, R.seq[0] << 2 | R.seq[1]);
	put8(&k, R.alias[0] << 6 | R.alias[1] << 4 | R.ic << 2 | R.spd);
	put8(&k, R.ratio);
	put8(&k, R.alias[2]);
	for (i = 0; i < 7; ++i) putmiss(&k, &R.miss[i]);
	putmiss(&k, &R.seqf);
	memcpy(symbol, "TR64.P1.", 8);
	m = 8 + base45(symbol + 8, k.b, k.n);
	symbol[m++] = '.';
	symbol[m] = 0;
	tr64_qr_draw(fb, 320, 320 - TR64_QR_EDGE * 4, 240 - TR64_QR_EDGE * 4, 4, symbol, m, 0x0001u, 0xffffu);
	if (R.done) rlog("probe: symbol %s\n", symbol);
	show_vi(fb);
}

/* The reduced screen: only the deciding tests, large text, no symbol. */
static void render_short(unsigned failed)
{
	uint16_t *fb = UncachedAddr(framebuffer);
	static const char *where[3] = { "C0200000 KX1", "C0200000 KX0", "50000000 KX1" };
	unsigned y = 4, i;
	char s[48];
	for (i = 0; i < 320 * 240; ++i) fb[i] = 0x1001u;
	text(fb, 4, y, 2, 0xf801u, "TR64 FETCH PROBE 2"); y += 14;
	if (R.done) text(fb, 4, y, 2, failed ? 0xf801u : 0x07c1u, failed ? "NOT A VR4300 FETCH PATH" : "PASS AS VR4300");
	else { snprintf(s, sizeof s, "RUNNING %.28s", R.what ? R.what : ""); text(fb, 4, y, 2, 0xffc1u, s); }
	y += 16;
	snprintf(s, sizeof s, "PRID %08lX CFG %08lX", (unsigned long)R.prid, (unsigned long)R.config); text(fb, 4, y, 2, 0xafffu, s); y += 12;
	snprintf(s, sizeof s, "SR %08lX RDRAM0 %08lX", (unsigned long)sr_base, (unsigned long)R.rdram[0]); text(fb, 4, y, 2, 0xafffu, s); y += 12;
	snprintf(s, sizeof s, "RDRAMC %08lX", (unsigned long)R.rdram[1]); text(fb, 4, y, 2, 0xafffu, s); y += 16;
	text(fb, 4, y, 2, 0xffffu, "FETCH VA: V1 MAPPED V2 ALIAS"); y += 12;
	for (i = 0; i < 3; ++i) {
		snprintf(s, sizeof s, "%s V%llX T%lu %c", where[i], (unsigned long long)(R.alias_value[i] & 0xFFFFFFFFu),
			(unsigned long)R.alias_ticks[i], "PAFX"[R.alias[i] & 3]);
		text(fb, 4, y, 2, 0xffffu, s); y += 12;
	}
	text(fb, 4, y, 2, 0xafffu, "T = TICKS FOR 1000 CALLS"); y += 16;
	text(fb, 4, y, 2, 0xffffu, "ICACHE STUB AT SAME COLOUR"); y += 12;
	snprintf(s, sizeof s, "HIT %lX IDX %lX ALL %lX %c", (unsigned long)R.ic_value[0], (unsigned long)R.ic_value[1],
		(unsigned long)R.ic_value[2], "PFFX"[R.ic & 3]);
	text(fb, 4, y, 2, 0xffffu, s); y += 12;
	text(fb, 4, y, 2, 0xafffu, "WANT 2 2 2; 1 = STALE LINE"); y += 16;
	snprintf(s, sizeof s, "K0 %luK K2 %luK %c", (unsigned long)(R.ticks[0] / 1000), (unsigned long)(R.ticks[1] / 1000), letter[R.spd & 3]);
	text(fb, 4, y, 2, 0xffffu, s); y += 14;
	snprintf(s, sizeof s, "INFO TLBR PFN %08lX", (unsigned long)R.pfn);
	text(fb, 4, y, 2, 0xffffu, s); y += 12;
	text(fb, 4, y, 2, 0xafffu, "WROTE 3FFFFFC0 ARES READS 03FFFFC0");
	show_vi(fb);
}

int main(void)
{
	size_t size = 0;
	outcome_t o;
	debug_init_isviewer();
	disable_interrupts();
	sr_base = C0_STATUS() & ~C0_STATUS_IE;
	image_build(image);
	{
		/* Vectors: our recorder at 0x000, 0x080, 0x100 and 0x180, written uncached. */
		unsigned v;
		for (v = 0; v < 4; ++v) memcpy((void *)(0xA0000000u + v * 0x80u), &probe_vec_template[v * 4], 16);
		inst_cache_invalidate_all();
	}
	rom_page = rompak_search_ext(".hwprobe", &size);
	if (rom_page && size >= 2 * PAGE) {
		data_cache_hit_writeback_invalidate(rom_copy, sizeof rom_copy);
		dma_wait_finished(dma_read_async(rom_copy, rom_page, 2 * PAGE));
		rom_ok = !memcmp(rom_copy, image, 2 * PAGE);
	}
	rlog("probe: start sr=%08lx rom_page=%08lx size=%lu rom_matches_image=%d\n", (unsigned long)sr_base,
		(unsigned long)rom_page, (unsigned long)size, rom_ok);
	t_env();
	/* A deliberate fault proves the recorder before anything depends on it. */
	tlb_clear();
	guarded(&o, call_load, VA_MISS, sr_base, 0);
	show_rec("recorder self-test (load miss)", &o);
#ifdef HWPROBE_FULL
	step("T1 T2 T3");
	t_readback(0); t_readback(1); t_tlbp();
	step("T4 DATA"); t_data();
	step("T5 ALIAS"); t_alias();
	step("EXEC"); t_exec();
	step("ALIAS PA"); t_alias_pa(0, 1, 0xC0200000u); t_alias_pa(1, 0, 0xC0200000u); t_alias_pa(2, 1, 0x50000000u);
	step("ICACHE"); t_icache();
	step("SPEED"); t_speed();
	step("MISS"); t_miss();
	step("SEQ"); t_seq();
	R.done = 1;
	{
		char ids[34];
		unsigned n = failures(ids, sizeof ids);
		rlog("probe: verdict %s %s\n", n ? "FAIL" : "ALL PASS", ids);
		render();
		if (th_present()) {
			th_screenshot(framebuffer, 320, 240, 2);
			th_exit(n ? 1 : 0);
		}
	}
#else
	R.what = "FETCH KX1 C0200000"; render_short(0); t_alias_pa(0, 1, 0xC0200000u);
	R.what = "FETCH KX0 C0200000"; render_short(0); t_alias_pa(1, 0, 0xC0200000u);
	R.what = "FETCH KX1 50000000"; render_short(0); t_alias_pa(2, 1, 0x50000000u);
	R.what = "ICACHE"; render_short(0); t_icache();
	R.what = "SPEED"; render_short(0); t_speed();
	t_pfn();
	{
		unsigned n = 0, i;
		for (i = 0; i < 3; ++i) n += R.alias[i] != 0;
		n += R.ic != 0;
		n += R.spd != P_PASS;
		/* PFN width is informational: ares itself reads 03FFFFC0 (26-bit EntryLo). */
		rlog("probe: verdict %s\n", n ? "FAIL (not what a VR4300 does)" : "PASS as VR4300");
		R.done = 1;
		render_short(n);
		if (th_present()) {
			th_screenshot(framebuffer, 320, 240, 2);
			th_exit(n ? 1 : 0);
		}
	}
#endif
	for (;;) { }
}
