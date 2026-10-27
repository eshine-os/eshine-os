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

/* 站十八A:宿主相在途拍守门。recall=在途回业主标志(27 三分支置),
 * last=最后进场的 VM id(泵每次进场记录)——泵见 recall 即以 last
 * 为旋转起点,首圈回业主消费自己的拍。站十八D:per-CPU 数组(mpidr
 * 索引,两泵互不相扰)。 */
extern volatile unsigned long sched_vtimer_recall[2];
extern unsigned long sched_vtimer_last[2];

/* 站十八D:CPU 间握手——WFE 事件寄存器语义天然防丢醒(SEV 在泵
 * 检查与 WFE 之间到达即置 event,WFE 立即返回)。 */
void sched_poke_cpu1(void);
extern volatile unsigned long sched_cpu1_up;   /* 站十八D:CPU1 上线握手(main 有界等) */

/* 站十八D:CPU1 专泵(跑 vm2,无量子无轮转;vm.c) */
void pump_secondary(void) __attribute__((noreturn));

u32 sched_vtimer_sweep(void);     /* 到期扫描:表判 ctx,WOKE 过期者(多家);
                                    * 27 到点与每拍 tick 共用同一归属谓词 */

#endif /* ESHINE_SCHED_H */
