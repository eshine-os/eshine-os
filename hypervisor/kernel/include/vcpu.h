/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#ifndef ESHINE_VCPU_H
#define ESHINE_VCPU_H

#include "kernel/include/types.h"

/* vcpu_entry.S 依赖此布局: entry_pc@0 entry_sp@8 spsr@16 host_ctx@24(+32) */
struct vcpu {
    u64 entry_pc;                    /* ERET 目标(guest IPA) */
    u64 entry_sp;                    /* SP_EL1 */
    u64 spsr;                        /* SPSR_EL2: EL1h + DAIF 掩码 */
    struct { u64 sp, lr; } host_ctx; /* ERET 前的 EL2 帧,exit 超调用恢复 */
};

void vcpu_init(void);                /* boot:安装 SP_EL2 异常栈 */
void vcpu_run(struct vcpu *v);       /* 正常返回 == guest 已 exit */
void vcpu_exit(void) __attribute__((noreturn));

/* vcpu_entry.S */
void vcpu_eret_enter(struct vcpu *v);
void vcpu_return_host(struct vcpu *v) __attribute__((noreturn));

#endif /* ESHINE_VCPU_H */
