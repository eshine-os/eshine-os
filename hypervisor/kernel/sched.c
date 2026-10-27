/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
/* 调度状态面:抢占标志 + RX 所有权 + 抢占计数。
 * pump 本体在 vm.c(它持有 vm_table 与善后路径);配额 slice_left 内聚在
 * struct vcpu(guest 态 tick 上下文只握 cur_vcpu,免 VM 反查)。 */
#include "kernel/include/sched.h"

volatile unsigned long sched_need_resched;   /* asm 桩出口检查(置帧转写) */
unsigned long sched_preempts;                 /* 抢占计数(vmstat 判据) */
struct vcpu *sched_console_owner;              /* UART RX 所有权(NULL=host) */
volatile unsigned long sched_vtimer_recall[2]; /* 站十八A/D:在途回业主标志[cpu] */
unsigned long sched_vtimer_last[2];            /* 站十八A/D:最后进场 VM id[cpu] */

volatile unsigned long sched_cpu1_up;   /* 站十八D:CPU1 上线握手(main 有界等) */

void sched_poke_cpu1(void)
{
    __asm__ volatile("sev");       /* WFE 事件寄存器:天然防丢醒 */
}
