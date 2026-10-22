/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
/* EL2 IRQ 入口:帧建在 SP_EL2 栈上(异常入 EL2 硬件强制 SPSel=1),
 * 对称恢复 —— 非 eret 弃帧路径,无栈泄漏(stop 3 教训的镜像面)。
 * ELR/SPSR_EL2 不动,C 分发返回后直接 eret。 */
.section .text
.global el2_trap
el2_trap:
    stp     x29, x30, [sp, #-256]!     /* 帧头(x29@0 x30@8)+ 预留 256B */
    stp     x0, x1,  [sp, #16]
    stp     x2, x3,  [sp, #32]
    stp     x4, x5,  [sp, #48]
    stp     x6, x7,  [sp, #64]
    stp     x8, x9,  [sp, #80]
    stp     x10, x11, [sp, #96]
    stp     x12, x13, [sp, #112]
    stp     x14, x15, [sp, #128]
    stp     x16, x17, [sp, #144]
    stp     x18, x19, [sp, #160]
    stp     x20, x21, [sp, #176]
    stp     x22, x23, [sp, #192]
    stp     x24, x25, [sp, #208]
    stp     x26, x27, [sp, #224]
    str     x28, [sp, #240]
    bl      el2_irq                    /* C 分发:claim → handler → eoi */
    ldp     x0, x1,  [sp, #16]
    ldp     x2, x3,  [sp, #32]
    ldp     x4, x5,  [sp, #48]
    ldp     x6, x7,  [sp, #64]
    ldp     x8, x9,  [sp, #80]
    ldp     x10, x11, [sp, #96]
    ldp     x12, x13, [sp, #112]
    ldp     x14, x15, [sp, #128]
    ldp     x16, x17, [sp, #144]
    ldp     x18, x19, [sp, #160]
    ldp     x20, x21, [sp, #176]
    ldp     x22, x23, [sp, #192]
    ldp     x24, x25, [sp, #208]
    ldp     x26, x27, [sp, #224]
    ldr     x28, [sp, #240]
    ldp     x29, x30, [sp], #256
    eret
