/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#ifndef ESHINE_VCPU_H
#define ESHINE_VCPU_H

#include "kernel/include/types.h"

/* vcpu_entry.S 依赖此布局: entry_pc@0 entry_sp@8 spsr@16 host_ctx@24。
 * host_ctx 是 ERET 前的完整宿主现场:x19-x28(ABI callee-saved)+ sp + lr —
 * guest 会覆盖一切可覆盖寄存器,ret 回宿主前必须整块还回去
 * (否则调用方缓存在 callee-saved 的值变成 EL2 异常链残留)。 */
struct vcpu {
    u64 entry_pc;                    /* ERET 目标(guest IPA) */
    u64 entry_sp;                    /* SP_EL1 */
    u64 spsr;                        /* SPSR_EL2: EL1h + DAIF 掩码 */
    struct {
        u64 x19, x20, x21, x22, x23, x24, x25, x26, x27, x28;   /* @24-72 */
        u64 sp;                      /* @104 */
        u64 lr;                      /* @112 */
    } host_ctx;
};

void vcpu_init(void);                /* boot:安装 SP_EL2 异常栈 */
void vcpu_run(struct vcpu *v, u64 vttbr);  /* 正常返回 == guest 已 exit */
void vcpu_exit(void) __attribute__((noreturn));

/* vcpu_entry.S */
void vcpu_eret_enter(struct vcpu *v);
void vcpu_return_host(struct vcpu *v) __attribute__((noreturn));

#endif /* ESHINE_VCPU_H */
