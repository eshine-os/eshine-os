/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
/* EL1 向量表:两组当前 EL 的 IRQ 槽共用平衡桩(guest 常驻 EL1h);
 * HVC/WFI/级联页表错皆陷 EL2,不落 guest 表。同步异常落到此表即停机。 */
.macro VENTRY target
    .align 7
    b       \target
.endm

.section .vectors, "ax"
.align 11
.global guest_vectors
guest_vectors:
    /* Current EL, SP_EL0 */
    VENTRY guest_park               /* sync */
    VENTRY guest_irq                /* irq */
    VENTRY guest_park               /* fiq */
    VENTRY guest_park               /* serror */
    /* Current EL, SP_ELx(常驻组) */
    VENTRY guest_park               /* sync */
    VENTRY guest_irq                /* irq */
    VENTRY guest_park               /* fiq */
    VENTRY guest_park               /* serror */
    /* Lower EL:guest 是 EL1 终态,此半区永不到达 */
    VENTRY guest_park
    VENTRY guest_park
    VENTRY guest_park
    VENTRY guest_park
    VENTRY guest_park
    VENTRY guest_park
    VENTRY guest_park
    VENTRY guest_park

guest_park:
    wfi
    b       guest_park

/* 平衡保存桩:guest 栈上压 x0-x30,bl C 处理后对称恢复,eret。
 * LR/SPSR_EL1 硬件管理,eret 直用;EL1 取 IRQ 时 SP 即当前栈,不换行。 */
guest_irq:
    stp     x29, x30, [sp, #-16]!
    stp     x27, x28, [sp, #-16]!
    stp     x25, x26, [sp, #-16]!
    stp     x23, x24, [sp, #-16]!
    stp     x21, x22, [sp, #-16]!
    stp     x19, x20, [sp, #-16]!
    stp     x17, x18, [sp, #-16]!
    stp     x15, x16, [sp, #-16]!
    stp     x13, x14, [sp, #-16]!
    stp     x11, x12, [sp, #-16]!
    stp     x9,  x10, [sp, #-16]!
    stp     x7,  x8,  [sp, #-16]!
    stp     x5,  x6,  [sp, #-16]!
    stp     x3,  x4,  [sp, #-16]!
    stp     x1,  x2,  [sp, #-16]!
    str     x0,  [sp, #-16]!
    bl      guest_irq_c
    ldr     x0,  [sp], #16
    ldp     x1,  x2,  [sp], #16
    ldp     x3,  x4,  [sp], #16
    ldp     x5,  x6,  [sp], #16
    ldp     x7,  x8,  [sp], #16
    ldp     x9,  x10, [sp], #16
    ldp     x11, x12, [sp], #16
    ldp     x13, x14, [sp], #16
    ldp     x15, x16, [sp], #16
    ldp     x17, x18, [sp], #16
    ldp     x19, x20, [sp], #16
    ldp     x21, x22, [sp], #16
    ldp     x23, x24, [sp], #16
    ldp     x25, x26, [sp], #16
    ldp     x27, x28, [sp], #16
    ldp     x29, x30, [sp], #16
    eret
