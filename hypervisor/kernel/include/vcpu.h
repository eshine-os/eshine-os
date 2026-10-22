/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#ifndef ESHINE_VCPU_H
#define ESHINE_VCPU_H

#include "kernel/include/types.h"

/* 陷阱帧:guest 完整现场。偏移即 asm 契约(vcpu_entry.S):
 * gprs[31]@0(x0@0...x30@240) sp@248 pc@256 pstate@264
 * host_ctx@272(x19@272...x28@344,sp@352,lr@360) exit_reason@368 */
struct vcpu {
    u64 gprs[31];              /* x0-x30:陷阱入口全量保存,EL2 可自由用寄存器 */
    u64 sp;                     /* SP_EL1 */
    u64 pc;                     /* ELR_EL2 */
    u64 pstate;                 /* SPSR_EL2 */
    struct {                    /* 宿主现场(第二站已建,偏移随帧前移) */
        u64 x19, x20, x21, x22, x23, x24, x25, x26, x27, x28;
        u64 sp;
        u64 lr;
    } host_ctx;
    u64 exit_reason;
};

/* 退回宿主的原因码(vcpu_run 返回值) */
#define VCPU_EXIT   0UL         /* HVC #1:guest 主动退出 -> VM 层 TEARDOWN->DEAD */
#define VCPU_YIELD  1UL         /* HVC #2:主动让出 -> VM 层 RUNNING->PAUSED */
#define VCPU_FAULT  2UL         /* 非 HVC 异常 -> 诊断后 TEARDOWN->DEAD */

void vcpu_init(void);
u64  vcpu_run(struct vcpu *v, u64 vttbr);   /* 正常返回值 = exit_reason */
void vcpu_exit(void) __attribute__((noreturn));

/* vcpu_entry.S */
void vcpu_enter_guest(struct vcpu *v);   /* 正常"返回"= guest 已 retire */
void trap_resume(struct vcpu *v) __attribute__((noreturn));
void vcpu_return_host(struct vcpu *v) __attribute__((noreturn));

extern struct vcpu *cur_vcpu;   /* asm 桩引用;单活跃 vCPU(无 SMP) */

#endif /* ESHINE_VCPU_H */
