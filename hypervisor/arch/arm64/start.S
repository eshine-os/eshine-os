/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
.section .text.boot
.global _start

/* QEMU -M virt,virtualization=on -kernel: 入口在 EL2 */
_start:
    mrs     x0, CurrentEL
    and     x0, x0, #0xc
    cmp     x0, #0x8                    /* EL2 ? */
    bne     1f                          /* 非 EL2:原地 wfi 停车 */

    msr     daifset, #0xf               /* 关全部中断(本周轮询模型) */

    /* 清 BSS */
    ldr     x0, =__bss_start
    ldr     x1, =__bss_end
2:  cmp     x0, x1
    b.hs    3f
    str     xzr, [x0], #8
    b       2b
3:
    /* 装载异常向量表 */
    ldr     x0, =el2_vector_table
    msr     vbar_el2, x0
    isb

    /* 归一 SP bank:QEMU 直启 EL2 复位 SPSel=1(栈写进了 SP_EL2)。
     * 规范:宿主 C 世界跑 SP_EL0(SPSel=0);SP_EL2 专用作异常进入栈
     * (硬件:异常入 EL2 强制 SPSel=1),boot 后由 vcpu_init() 装载。 */
    msr     spsel, #0
    ldr     x0, =__stack_top
    mov     sp, x0

    b       main
1:  wfi
    b       1b
