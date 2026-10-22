/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "arch/arm64/include/mm.h"
#include "kernel/include/vcpu.h"

struct vcpu *cur_vcpu;               /* asm 桩引用,不再 static */

/* EL2-from-EL1 异常落点专用栈(硬件强制 SPSel=1 -> SP_EL2)。
 * 非 static:el1_trap 每次入口把它归位到栈顶 —— handler 链不退栈,
 * eret/retire 都不还 SP_EL2,不归位则每次陷阱净泄一截,累计碾穿栈底。 */
u64 excp_stack[512] __attribute__((aligned(16)));

/* HCR_EL2: RW(64-bit EL1) | VM(stage-2 使能) */
#define HCR_GUEST 0x80000001UL

void vcpu_init(void)
{
    __asm__ volatile("msr spsel, #1");
    __asm__ volatile("mov sp, %0" :: "r"((u64)&excp_stack[512]));
    __asm__ volatile("msr spsel, #0");
}

u64 vcpu_run(struct vcpu *v, u64 vttbr)
{
    u64 sctlr;

    cur_vcpu = v;

    /* EL1 stage-1 关闭(裸 guest 走纯 stage-2) */
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    sctlr &= ~1UL;
    __asm__ volatile("msr sctlr_el1, %0" :: "r"(sctlr));

    __asm__ volatile("msr vtcr_el2, %0" :: "r"(VTCR_VALUE));
    __asm__ volatile("msr vttbr_el2, %0" :: "r"(vttbr));
    __asm__ volatile("msr hcr_el2, %0" :: "r"(HCR_GUEST));
    __asm__ volatile("tlbi vmalls12e1is; dsb sy; isb");

    vcpu_enter_guest(v);            /* sp_el1/elr/spsr 由帧加载,不再单设 */
    return v->exit_reason;          /* 正常返回 = guest 已 retire */
}

void vcpu_exit(void)
{
    __asm__ volatile("msr hcr_el2, xzr");
    __asm__ volatile("msr vttbr_el2, xzr");
    __asm__ volatile("tlbi vmalls12e1is; dsb sy; isb");
    vcpu_return_host(cur_vcpu);
}
