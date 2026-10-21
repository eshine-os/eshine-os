/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "kernel/include/console.h"
#include "kernel/include/types.h"

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
