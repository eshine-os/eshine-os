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

/* lower-EL AArch64 sync:asm 桩(el1_trap)保存 guest 帧后调入,x0=vcpu。
 * HVC #0/#1/#2 分派;非 HVC = guest fault -> 诊断+VM 隔离死亡(Task 3)。
 * 帧内改值即改 guest 现场,trap_resume 按帧续跑。 */
void el1_sync(struct vcpu *v) __attribute__((noreturn));
void el1_sync(struct vcpu *v)
{
    u64 esr, ec, imm;

    __asm__ volatile("mrs %0, esr_el2" : "=r"(esr));
    ec = esr >> 26;
    imm = esr & 0xFFFF;

    if (ec == 0x16 && imm == 0) {           /* HVC #0 putc:帧内 x0 in/out */
        console_putc((char)v->gprs[0]);
        v->gprs[0] = 0;
        trap_resume(v);
    } else if (ec == 0x16 && imm == 1) {    /* HVC #1 exit */
        v->exit_reason = VCPU_EXIT;
        vcpu_exit();
    } else if (ec == 0x16 && imm == 2) {    /* HVC #2 yield */
        v->exit_reason = VCPU_YIELD;
        vcpu_exit();
    } else {                                /* guest fault:Task 3 带诊断 */
        v->exit_reason = VCPU_FAULT;
        vcpu_exit();
    }
}
