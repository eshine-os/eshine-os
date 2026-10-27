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

    /* 站十八D:非 boot 核门——防"全体进进口"形态下次核重跑 CPU0
     * 的 boot(bss 再清/栈互踩)。实测 QEMU virt 次核上电即 PSCI
     * off,此门是防御性收敛。 */
    mrs     x0, mpidr_el1    /* EL2 可读同值;mpidr_el2 名字 armv8-a GAS 不认 */
    and     x0, x0, #0xff
    cbnz    x0, 1f

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

    /* SP bank 规范(站十四修正):宿主 C 世界驻 SP_EL2(SPSel=1,与复位
     * 同值);SP_EL0 全 EL 共享,归还 guest——arm64 内核在 EL1 以它存
     * current(enter_from_kernel_mode 直读),宿主若借作栈,每次 eret
     * 都把 .stack 指针灌给 guest,首个碰 current 的点即塌(Linux 实测
     * 6M/秒向量自陷风暴;裸机客不读 SP_EL0,十三站无感)。 */
    msr     spsel, #1
    ldr     x0, =__stack_top
    mov     sp, x0

    b       main
1:  wfi
    b       1b

/* 站十八D:次核入口(PSCI CPU_ON 的 entry_point;QEMU 上电即置
 * EL2,与 CPU0 复位同级;x0=context_id)。栈/向量/泵逐步就位。 */
.global secondary_entry
secondary_entry:
    mrs     x0, CurrentEL
    and     x0, x0, #0xc
    cmp     x0, #0x8
    bne     1b                          /* 理论不到:停车 */
    msr     daifset, #0xf
    msr     spsel, #1                   /* 宿主 C 世界驻 SP_EL2(站十四) */
    ldr     x0, =secondary_stack_top
    mov     sp, x0
    b       secondary_main
