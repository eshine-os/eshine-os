/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "drivers/include/gicv3.h"
#include "drivers/include/timer.h"
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
    /* 站19D 收口:补 PPI26 宿主拍(计划本项,首版"专泵无量子不配"
     * 少配了兜底面)。26 在次级不只做量子——el2_irq_host 的 26 分支
     * 携带每拍到期扫描(sched_vtimer_sweep,per-CPU 判自家帧):睡客
     * 的 vtimer 在途拍蒸发后(27 被关线/ctx 熄火,再无物理响),唯一
     * 的再发现途径就是本扫描;无 26 = 单次蒸发即永眠(vm2 SMP 后
     * alternatives stop_machine 冻死 20s 内核时处,实测)。真机侧
     * 同源:WFE 睡泵也靠 26 拍醒。CNTHP 是本核硬件,timer_init 天然
     * per-CPU。 */
    timer_init();
    {   /* 站20:三核并发自报必须行原子(console_puts 每次调用各持锁,
         * 分段印在三核并发下会行内互串,-smp 3 门禁 [3] 实测抓出);
         * 本地拼整行一次 puts——锁区间即整行,天然原子 */
        char rep[32];
        unsigned int r = 0, i;
        u32 v = cpu;

        rep[r++] = 'c'; rep[r++] = 'p'; rep[r++] = 'u';
        for (i = 0; i < 3 && (v >> (8 * (2 - i))) == 0; i++)
            ;                                   /* 前导零跳过 */
        for (; i < 3; i++)
            rep[r++] = (char)('0' + ((v >> (8 * (2 - i))) & 0xFu));
        for (i = 0; i < 23; i++)            /* ": online (psci cpu_on)\n"
                                          * 整串 23 字符(旧 18 截断雷) */
            rep[r++] = ": online (psci cpu_on)\n"[i];
        rep[r] = '\0';
        console_puts(rep);
    }
    sched_cpu_up[cpu] = 1UL;          /* 站20:自报后再握手——main 的
                                      * 有界等覆盖整行,CPU0 提示符不
                                      * 会抢在自报前出([3] 三核实测) */
    __asm__ volatile("msr daifclr, #2");   /* 解 IRQ:27/邮箱生命线 */
    pump_cpu(cpu);                        /* noreturn:各跑自家 (vm,vcpu) */
}
