/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
/* 调度状态面:抢占标志 + RX 所有权 + 抢占计数。
 * pump 本体在 vm.c(它持有 vm_table 与善后路径);配额 slice_left 内聚在
 * struct vcpu(guest 态 tick 上下文只握 cur_vcpu,免 VM 反查)。 */
#include "kernel/include/sched.h"

volatile unsigned long sched_need_resched;   /* asm 桩出口检查(置帧转写) */
unsigned long sched_preempts;                 /* 抢占计数(vmstat 判据) */
u32 sched_console_owner = SCHED_OWNER_HOST;   /* UART RX 所有权 */
