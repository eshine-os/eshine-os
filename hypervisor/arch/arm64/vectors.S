/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
/* 16 槽向量表:current-EL sync/irq + lower-EL sync/irq 先实,余 panic 占位(spec §3) */
.macro VENTRY target
    .align 7
    b       \target
.endm

.section .vectors, "ax"
.align 11
.global el2_vector_table
el2_vector_table:
    /* Current EL, SP_EL0 */
    VENTRY el2_sync               /* sync */
    VENTRY el2_trap                /* irq */
    VENTRY el2_unexpected          /* fiq */
    VENTRY el2_unexpected          /* serror */
    /* Current EL, SP_ELx */
    VENTRY el2_sync                /* sync */
    VENTRY el2_trap                /* irq */
    VENTRY el2_unexpected          /* fiq */
    VENTRY el2_unexpected          /* serror */
    /* Lower EL, AArch64 */
    VENTRY el1_trap               /* sync:asm 桩保存 guest 帧后进 C */
    VENTRY el2_trap_guest         /* irq:guest 态物理中断,出口可抢占(帧转写) */
    VENTRY el2_unexpected
    VENTRY el1_serror             /* serror:AMO 路由的物理 SError(站十四) */
    /* Lower EL, AArch32 */
    VENTRY el2_unexpected
    VENTRY el2_unexpected
    VENTRY el2_unexpected
    VENTRY el2_unexpected
