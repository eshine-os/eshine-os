/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
/* EL1 入口:guest 无自身向量表——一切异常皆陷 EL2(EL2 panic 兜底) */
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
    b       guest_main
