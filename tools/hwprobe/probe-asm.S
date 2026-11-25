# SPDX-License-Identifier: MIT
# Hardware probe support: guarded calls, exception vector recorder, faulting
# instruction sites, and the tlbcode entrypoint's TLB initialisation verbatim.
	.set noreorder
	.set noat
	.text

# uint64_t probe_call(uint64_t (*fn)(uint64_t), uint64_t arg)
# Runs fn(arg); if it takes an exception the vector recorder below resumes
# at probe_recover, which returns 0xDEADFA17 from here with the callee-saved
# registers restored.
	.align 5
	.globl probe_call
	.ent probe_call
probe_call:
	la $t0,probe_jb
	sd $s0,0($t0)
	sd $s1,8($t0)
	sd $s2,16($t0)
	sd $s3,24($t0)
	sd $s4,32($t0)
	sd $s5,40($t0)
	sd $s6,48($t0)
	sd $s7,56($t0)
	sd $gp,64($t0)
	sd $sp,72($t0)
	sd $fp,80($t0)
	sd $ra,88($t0)
	move $t9,$a0
	jalr $t9
	move $a0,$a1
	la $t0,probe_jb
	ld $ra,88($t0)
	jr $ra
	nop
	.end probe_call

	.align 5
	.globl probe_recover
	.ent probe_recover
probe_recover:
	la $t0,probe_jb
	ld $s0,0($t0)
	ld $s1,8($t0)
	ld $s2,16($t0)
	ld $s3,24($t0)
	ld $s4,32($t0)
	ld $s5,40($t0)
	ld $s6,48($t0)
	ld $s7,56($t0)
	ld $gp,64($t0)
	ld $sp,72($t0)
	ld $fp,80($t0)
	ld $ra,88($t0)
	lui $v0,0xDEAD
	ori $v0,$v0,0xFA17
	jr $ra
	nop
	.end probe_recover

# uint64_t probe_jalr(uint32_t entry): jalr with t9 = entry (the callee may
# address its own page through t9), as the code unit call sites do.
	.align 5
	.globl probe_jalr
	.ent probe_jalr
probe_jalr:
	addiu $sp,$sp,-16
	sd $ra,0($sp)
	move $t9,$a0
	jalr $t9
	nop
	ld $ra,0($sp)
	jr $ra
	addiu $sp,$sp,16
	.end probe_jalr

# uint64_t probe_ret(void): the function the 1 MiB alias test calls through
# the kseg2 mapping of its own physical address.
	.align 5
	.globl probe_ret
	.ent probe_ret
probe_ret:
	ori $v0,$zero,0x5A5A
	jr $ra
	nop
	.end probe_ret

# uint64_t probe_spin(uint32_t n): n iterations of a 3-instruction loop, then
# 0x5A5A. Timed in kseg0 and through the identity-mapped kseg2 alias.
	.align 5
	.globl probe_spin
	.ent probe_spin
probe_spin:
1:
	bnez $a0,1b
	addiu $a0,$a0,-1
	ori $v0,$zero,0x5A5A
	jr $ra
	nop
	.end probe_spin


	.align 5
	.globl probe_load
	.ent probe_load
probe_load:
	.globl probe_load_insn
probe_load_insn:
	lw $v0,0($a0)
	jr $ra
	nop
	.end probe_load

	.align 5
	.globl probe_store
	.ent probe_store
probe_store:
	.globl probe_store_insn
probe_store_insn:
	sw $zero,0($a0)
	jr $ra
	li $v0,1
	.end probe_store

# uint64_t probe_load_exl(uint32_t addr, uint64_t epc_sentinel): load with
# Status.EXL already set and EPC preset to a sentinel.
	.align 5
	.globl probe_load_exl
	.ent probe_load_exl
probe_load_exl:
	mfc0 $t0,$12
	nop
	ori $t1,$t0,2
	dmtc0 $a1,$14
	nop
	nop
	mtc0 $t1,$12
	nop
	nop
	.globl probe_load_exl_insn
probe_load_exl_insn:
	lw $v0,0($a0)
	mtc0 $t0,$12
	nop
	nop
	jr $ra
	nop
	.end probe_load_exl

# The four vector entries (0x000 TLB refill, 0x080 XTLB refill, 0x100 cache
# fault, 0x180 general): each 16 bytes, copied verbatim to 0x80000000 + n*0x80.
	.align 4
	.globl probe_vec_template
probe_vec_template:
	.macro vec n
	lui $k0,%hi(probe_common)
	addiu $k0,$k0,%lo(probe_common)
	jr $k0
	ori $k1,$zero,\n
	.endm
	vec 0
	vec 1
	vec 2
	vec 3

# Record the CP0 state of the exception, then resume at probe_recover.
# probe_rec layout: see probe.c rec_t.
	.align 5
	.globl probe_common
	.ent probe_common
probe_common:
	lui $k0,%hi(probe_rec)
	addiu $k0,$k0,%lo(probe_rec)
	sw $k1,0($k0)
	mfc0 $k1,$13
	nop
	sw $k1,4($k0)
	mfc0 $k1,$12
	nop
	sw $k1,8($k0)
	dmfc0 $k1,$14
	nop
	sd $k1,16($k0)
	dmfc0 $k1,$8
	nop
	sd $k1,24($k0)
	dmfc0 $k1,$4
	nop
	sd $k1,32($k0)
	dmfc0 $k1,$20
	nop
	sd $k1,40($k0)
	dmfc0 $k1,$10
	nop
	sd $k1,48($k0)
	lw $k1,56($k0)
	nop
	addiu $k1,$k1,1
	sw $k1,56($k0)
	lui $k1,%hi(probe_recover)
	addiu $k1,$k1,%lo(probe_recover)
	mtc0 $k1,$14
	nop
	nop
	eret
	nop
	.end probe_common

# void probe_boot_tlb(uint32_t wired): the TLB block of entrypoint.S under
# N64_TLBCODE_BOOT, instruction for instruction (src/entrypoint.S 32-78), with
# t4 = wired in place of la t4,__tlbcode_wired.
	.align 5
	.globl probe_boot_tlb
	.ent probe_boot_tlb
probe_boot_tlb:
	move $t4,$a0
	li $t0,31
	li $t1,0x4003E000
	mtc0 $zero,$5
	mtc0 $zero,$2
	mtc0 $zero,$3
1:
	mtc0 $t0,$0
	mtc0 $t1,$10
	nop
	nop
	tlbwi
	nop
	addiu $t1,$t1,-8192
	bnez $t0,1b
	addiu $t0,$t0,-1
	li $t0,0
	li $t1,0xC0000000
	li $t2,0x1F
	li $t3,0x001FE000
	mtc0 $t3,$5
	mtc0 $t4,$6
2:
	mtc0 $t0,$0
	mtc0 $t1,$10
	mtc0 $t2,$2
	li $t3,0x4000
	addu $t3,$t2,$t3
	mtc0 $t3,$3
	nop
	nop
	tlbwi
	nop
	addiu $t0,$t0,1
	li $t3,0x200000
	addu $t1,$t1,$t3
	li $t3,0x8000
	addu $t2,$t2,$t3
	bne $t0,$t4,2b
	nop
	mtc0 $zero,$5
	mtc0 $zero,$10
	nop
	nop
	jr $ra
	nop
	.end probe_boot_tlb
