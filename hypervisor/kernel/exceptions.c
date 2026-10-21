/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "kernel/include/console.h"
#include "kernel/include/types.h"
#include "kernel/include/vcpu.h"

void el2_sync(void)
{
    u64 esr, far;

    __asm__ volatile("mrs %0, esr_el2" : "=r"(esr));
    __asm__ volatile("mrs %0, far_el2" : "=r"(far));
    console_puts("\n[EXC] unexpected synchronous exception at EL2\n");
    console_puts("  ESR_EL2 = 0x");
    console_puthex(esr);
    console_puts("\n  FAR_EL2 = 0x");
    console_puthex(far);
    console_puts("\nhalting (WFI)\n");
    for (;;)
        __asm__ volatile("wfi");
}

void el2_irq(void)
{
    /* 本周无 GIC 配置,IRQ 不应发生;wfi 防忙转(spec §3) */
    for (;;)
        __asm__ volatile("wfi");
}

void el2_unexpected(void)
{
    u64 esr;

    __asm__ volatile("mrs %0, esr_el2" : "=r"(esr));
    console_puts("\n[EXC] unexpected exception (fiq/serror/lower-EL slot)\n");
    console_puts("  ESR_EL2 = 0x");
    console_puthex(esr);
    console_puts("\nhalting (WFI)\n");
    for (;;)
        __asm__ volatile("wfi");
}

/* 置 guest 返回值 x0 并 ERET 回 guest(异常帧由 ELR/SPSR 天然保存) */
static void eret_set_x0(u64 rv)
{
    __asm__ volatile("mov x0, %0\neret" :: "r"(rv) : "memory");
    __builtin_unreachable();
}

/* lower-EL AArch64 sync:向量表 b 进入,x0 即 guest x0(HVC ABI: x0 in/out,
 * x1 起易失)。仅认 HVC #0(putc)/#1(exit),其余默认拒绝。 */
void el1_sync(u64 x0)
{
    u64 esr, ec, imm;

    __asm__ volatile("mrs %0, esr_el2" : "=r"(esr));
    ec = esr >> 26;
    imm = esr & 0xFFFF;

    if (ec == 0x16 && imm == 0) {           /* HVC #0 putc */
        console_putc((char)x0);
        eret_set_x0(0);
    } else if (ec == 0x16 && imm == 1) {    /* HVC #1 exit */
        vcpu_exit();                        /* noreturn */
    } else {
        console_puts("\n[EXC] unexpected lower-EL exception (non-HVC)\n");
        console_puts("  ESR_EL2 = 0x");
        console_puthex(esr);
        console_puts("\nhalting (WFI)\n");
        for (;;)
            __asm__ volatile("wfi");
    }
}
