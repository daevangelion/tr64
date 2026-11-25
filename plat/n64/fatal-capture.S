# SPDX-License-Identifier: MIT
# Capture32GPRs then SR/CAUSE/PC per fatal.c, no unused HI/LO/FPR storage.
.section .text.sys_fatal_capture,"ax",@progbits
.align 2
.set noreorder
.set noat
.globl sys_fatal_capture
.ent sys_fatal_capture
sys_fatal_capture:
 sd $0,0($a0)
 sd $1,8($a0)
 sd $2,16($a0)
 sd $3,24($a0)
 sd $4,32($a0)
 sd $5,40($a0)
 sd $6,48($a0)
 sd $7,56($a0)
 sd $8,64($a0)
 sd $9,72($a0)
 sd $10,80($a0)
 sd $11,88($a0)
 sd $12,96($a0)
 sd $13,104($a0)
 sd $14,112($a0)
 sd $15,120($a0)
 sd $16,128($a0)
 sd $17,136($a0)
 sd $18,144($a0)
 sd $19,152($a0)
 sd $20,160($a0)
 sd $21,168($a0)
 sd $22,176($a0)
 sd $23,184($a0)
 sd $24,192($a0)
 sd $25,200($a0)
 sd $26,208($a0)
 sd $27,216($a0)
 sd $28,224($a0)
 sd $29,232($a0)
 sd $30,240($a0)
 sd $31,248($a0)
 mfc0 $k0,$12
 nop
 sw $k0,256($a0)
 mfc0 $k0,$13
 nop
 sw $k0,260($a0)
 sw $ra,264($a0)
 jr $ra
 nop
.end sys_fatal_capture

# Exception-entry stash. sys_fatal_install copies the four 16-byte entries of
# sys_vec_template over the SDK's vector stubs (0x80000000 + n*0x80); each loads
# the vector number n into k1 and jumps to sys_vec_common, which lives in .data
# (kseg0: reachable without the TLB). It records the first two exceptions that
# are not interrupts, system calls or coprocessor-unusable (the SDK takes those
# in normal operation) into sys_vec_stash, then does what the SDK stub did: jump
# to inthandler. Layout (fatal.c vec_stash_t): count 0, vector 4, slot n at
# 16 + 64n { vec 0, cause 4, sr 8, epc 12, badvaddr 16, context 24, xcontext 32,
# entryhi 40, sp 48, ra 52 }. k0 and k1 belong to the exception code.
.section .rodata.sys_vec_template,"a",@progbits
.align 4
.globl sys_vec_template
sys_vec_template:
.macro vec n
 lui $k0,%hi(sys_vec_common)
 addiu $k0,$k0,%lo(sys_vec_common)
 jr $k0
 ori $k1,$zero,\n
.endm
 vec 0
 vec 1
 vec 2
 vec 3

.section .data.sys_vec_common,"aw",@progbits
.align 5
.globl sys_vec_common
sys_vec_common:
 mfc0 $k0,$13
 nop
 andi $k0,$k0,0x7c
 beqz $k0,2f
 addiu $k0,$k0,-0x20
 beqz $k0,2f
 addiu $k0,$k0,-0x0c
 beqz $k0,2f
 lui $k0,%hi(sys_vec_stash)
 addiu $k0,$k0,%lo(sys_vec_stash)
 sw $k1,4($k0)
 lw $k1,0($k0)
 nop
 addiu $k1,$k1,1
 sw $k1,0($k0)
 sltiu $k1,$k1,3
 beqz $k1,2f
 lw $k1,0($k0)
 nop
 addiu $k1,$k1,-1
 sll $k1,$k1,6
 addu $k1,$k1,$k0
 addiu $k1,$k1,16
 lw $k0,4($k0)
 nop
 sw $k0,0($k1)
 mfc0 $k0,$13
 nop
 sw $k0,4($k1)
 mfc0 $k0,$12
 nop
 sw $k0,8($k1)
 mfc0 $k0,$14
 nop
 sw $k0,12($k1)
 dmfc0 $k0,$8
 nop
 sd $k0,16($k1)
 dmfc0 $k0,$4
 nop
 sd $k0,24($k1)
 dmfc0 $k0,$20
 nop
 sd $k0,32($k1)
 dmfc0 $k0,$10
 nop
 sd $k0,40($k1)
 sw $sp,48($k1)
 sw $ra,52($k1)
2:
 lui $k0,%hi(inthandler)
 addiu $k0,$k0,%lo(inthandler)
 jr $k0
 nop
