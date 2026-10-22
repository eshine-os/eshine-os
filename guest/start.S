/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
/* EL1 入口:装好自己的向量表(HVC/WFI/页表错仍陷 EL2;IRQ 到 guest 槽) */
.section .text.boot
.global _start

_start:
    ldr     x0, =__bss_start
    ldr     x1, =__bss_end
2:  cmp     x0, x1
    b.hs    3f
    str     xzr, [x0], #8
    b       2b
3:  ldr     x0, =__stack_top
    mov     sp, x0
    ldr     x0, =guest_vectors
    msr     vbar_el1, x0
    isb
    b       guest_main
