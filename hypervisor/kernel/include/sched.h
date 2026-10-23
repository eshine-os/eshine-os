/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#ifndef ESHINE_SCHED_H
#define ESHINE_SCHED_H

#include <stdbool.h>

#include "kernel/include/types.h"

struct vcpu;

/* 调度站常量:量子=1 tick(1s,QEMU 演示节奏)。
 * console owner = 持有 UART RX 的 vcpu 指针;NULL = 宿主 shell 独占。 */
#define SCHED_QUANTUM_TICKS 1UL

/* pump 选中 VM 时重置配额(直接字段写:vm->vcpus[0].slice_left=QUANTUM
 * + sched_need_resched=0);guest 态 tick 的配额递减在 el2_irq_guest,
 * 耗尽置 sched_need_resched,出口由 el2_trap_guest 帧转写切走。 */

/* shell 泵(vm.c:持有 vm_table 与善后路径):给每个 RUNNING 且非
 * waiting 的 VM 跑一个量子,按原因码迁移。返回 true=跑过 guest,
 * false=无可跑(全 idle/WAIT)。 */
bool sched_pump(void);

extern volatile unsigned long sched_need_resched;  /* asm 桩出口检查 */
extern unsigned long sched_preempts;               /* 抢占计数(vmstat 判据) */
extern struct vcpu *sched_console_owner;           /* UART RX 所有权(NULL=host) */

struct vcpu *sched_waiter(void);   /* 找 WAIT 态 vcpu(单等待者;无=NULL) */

#endif /* ESHINE_SCHED_H */
