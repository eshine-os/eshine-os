/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "drivers/include/gicv3.h"
#include "kernel/include/console.h"
#include "kernel/include/util.h"
#include "kernel/include/sched.h"
#include "kernel/include/types.h"

/* 站十八D 次核世界;站十九D 参数化——CPU1/CPU2 由 CPU0 经 PSCI
 * CPU_ON(QEMU 内部截获,无需 EL3;探针实证 SMC conduit)唤起,
 * 入口 = start.S 的 secondary_entry(EL2,自配栈后跳入本函数),
 * x0 = context_id = 本核号(PSCI 契约面捎带,免二次探 MPIDR)。
 * 起立序 = 异常安全边界:向量先行 -> GIC 本核自配(帧号 = 本核) ->
 * 自报 -> 解 IRQ(本核 27 与 WFE/SEV 邮箱是生命线)-> 专泵(本核
 * 归属的 (vm,vcpu),跑到出)。 */
extern char el2_vector_table[];      /* vectors.S(.global,2KB 对齐) */

void secondary_main(u32 cpu)
{
    /* 站十九D:三核一钟——QEMU PSCI 上电时给每核各设 CNTVOFF(以各自
     * 上电时刻为原点),三原点三钟;SMP 客的时序/超时全按钟差错拍
     * (实测内核钟快跑 ~9 倍、bring-up 缓行 180s 级)。EL2 有权归一:
     * 本核 CNTVOFF 清零,与 pcpu0 同看全局计数器(站 15"共享 0"纪律
     * 的三核版)。 */
    __asm__ volatile("msr cntvoff_el2, xzr");
    __asm__ volatile("isb");
    __asm__ volatile("msr vbar_el2, %0" :: "r"((u64)&el2_vector_table));
    __asm__ volatile("isb");
    gicv3_secondary_init(cpu);
    sched_cpu_up[cpu] = 1UL;          /* 先握手再自报:CPU0 出提示符前等我 */
    console_puts("cpu");
    util_putdec(cpu);
    console_puts(": online (psci cpu_on)\n");
    __asm__ volatile("msr daifclr, #2");   /* 解 IRQ:27/邮箱生命线 */
    pump_cpu(cpu);                        /* noreturn:各跑自家 (vm,vcpu) */
}
