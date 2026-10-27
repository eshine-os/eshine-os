/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "drivers/include/gicv3.h"
#include "kernel/include/console.h"
#include "kernel/include/sched.h"
#include "kernel/include/types.h"

/* 站十八D:次核世界。CPU1 由 CPU0 经 PSCI CPU_ON(QEMU 内部截获,
 * 无需 EL3;探针实证 SMC conduit)唤起,入口 = start.S 的
 * secondary_entry(EL2,自配栈后跳入本函数)。
 * 起立序 = 异常安全边界:向量先行 -> GIC 本核自配 -> 自报 -> 解 IRQ
 * (本核 27 与 WFE/SEV 邮箱是生命线)-> 专泵(vm2 单 VM,跑到出)。 */
extern char el2_vector_table[];      /* vectors.S(.global,2KB 对齐) */

void secondary_main(void)
{
    __asm__ volatile("msr vbar_el2, %0" :: "r"((u64)&el2_vector_table));
    __asm__ volatile("isb");
    gicv3_secondary_init();
    sched_cpu1_up = 1UL;               /* 先握手再自报:CPU0 出提示符前等我 */
    console_puts("cpu1: online (psci cpu_on)\n");
    __asm__ volatile("msr daifclr, #2");   /* 解 IRQ:27/邮箱生命线 */
    pump_secondary();                      /* noreturn */
}
