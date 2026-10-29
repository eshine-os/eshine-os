/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
/* 调度状态面:抢占标志 + RX 所有权 + 抢占计数。
 * pump 本体在 vm.c(它持有 vm_table 与善后路径);配额 slice_left 内聚在
 * struct vcpu(guest 态 tick 上下文只握 cur_vcpu,免 VM 反查)。 */
#include "kernel/include/sched.h"

volatile unsigned long sched_need_resched;   /* asm 桩出口检查(置帧转写) */
unsigned long sched_preempts;                 /* 抢占计数(vmstat 判据) */
struct vcpu *sched_console_owner;              /* UART RX 所有权(NULL=host) */
volatile unsigned long sched_vtimer_recall[4]; /* 站十八A/D:在途回业主标志[cpu];站19D 三核+站20 四核 */
unsigned long sched_vtimer_last[4];            /* 站十九D:(vm<<4|vi) 打包;站20 四核 */

volatile unsigned long sched_cpu_up[4]; /* 站19D 三核+站20 四核上线握手(main 有界等;[0] 恒 1) */

void sched_poke_cpu1(void)
{
    __asm__ volatile("sev");       /* WFE 事件寄存器:天然防丢醒(SEV 广播,
                                   * 各泵自判归属;站19D poke_cpu2 同理同体) */
}

void sched_poke_cpu2(void)
{
    __asm__ volatile("sev");
}

void sched_poke_cpu3(void)
{
    __asm__ volatile("sev");       /* 站20:CPU3 专泵(vm0.vcpu1)同体同律 */
}

/* 站20:按 (vm,vi) 的映射核 poke。SEV 是广播(各 WFE 泵自判归属),
 * 函数名只是意图标注;0 号核宿主泵永在轮询无 WFE,免 poke。 */
void sched_poke_cpu(u32 cpu)
{
    if (cpu == 1UL)
        sched_poke_cpu1();
    else if (cpu == 2UL)
        sched_poke_cpu2();
    else if (cpu == 3UL)
        sched_poke_cpu3();
}
