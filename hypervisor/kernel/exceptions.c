/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "drivers/include/gicv3.h"
#include "drivers/include/timer.h"
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

/* EL2 IRQ:el2_trap(asm)保存被中断现场后 bl 进来。
 * 26=宿主 CNTHP tick(EOIR+DIR);VTIMER=guest CNTV(B 相注入,EOIR 只降优先级,
 * 物理线由 guest 虚 EOI 经 HW-LR 硬件联动撤销);未知 intid 记账不 panic
 * (中断是数据,同 fault 隔离哲学)。spurious(1023)不动 EOIR。 */
void el2_irq(void)
{
    u64 intid = gicv3_claim();

    if (intid == GIC_PPI_HPTIMER) {
        timer_handler();
        gicv3_eoi_dir(intid);
    } else if (intid == GIC_PPI_VTIMER) {
        gicv3_eoi_drop(intid);      /* B 相在此行前插入 vgic_inject_vtimer() */
    } else if (intid != IAR_SPURIOUS) {
        gicv3_eoi_dir(intid);
    }
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
    } else {                                /* guest fault:诊断 -> VM 隔离死 */
        u64 far;

        v->exit_reason = VCPU_FAULT;
        __asm__ volatile("mrs %0, far_el2" : "=r"(far));
        console_puts("fault: ESR=0x");
        console_puthex(esr);
        console_puts(" FAR=0x");
        console_puthex(far);
        console_puts(" pc=0x");
        console_puthex(v->pc);
        console_puts(" sp=0x");
        console_puthex(v->sp);
        console_puts(" x0=0x");
        console_puthex(v->gprs[0]);
        console_puts(" x1=0x");
        console_puthex(v->gprs[1]);
        console_puts(" x2=0x");
        console_puthex(v->gprs[2]);
        console_puts(" x3=0x");
        console_puthex(v->gprs[3]);
        console_puts("\n");
        vcpu_exit();
    }
}
