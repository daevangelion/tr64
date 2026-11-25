/* SPDX-License-Identifier: MIT */
/* First application stack use is below 4 MiB, including constructors.
 * The SDK boot entry tail-calls this wrapper before main or any C code. */
.set noreorder
.section .boot
.globl __wrap___do_global_ctors
__wrap___do_global_ctors:
    li $sp, 0x803ffff0
    lw $t0, %gp_rel(__boot_memsize)($gp)
    sw $t0, %gp_rel(sys_hardware_rdram)($gp)
    li $t0, 0x00400000
    sw $t0, %gp_rel(__boot_memsize)($gp)
    la $t9, sys_boot_ctors
    jr $t9
    nop
