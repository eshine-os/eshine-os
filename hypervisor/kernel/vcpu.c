/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "arch/arm64/include/mm.h"
#include "kernel/include/vcpu.h"

static struct vcpu *cur_vcpu;

/* EL2-from-EL1 异常落点专用栈:异常硬件会强切 SPSel=1 → SP_EL2,
 * 而 EL2 自身 C 代码跑在 SP_EL0 上,SP_EL2 必须显式给真栈,
 * 否则处理器把 callee-saved 压进未初始化 bank(见 docs/devlog)。 */
static u64 excp_stack[512] __attribute__((aligned(16)));

/* HCR_EL2: RW(64-bit EL1) | VM(stage-2 使能);DC=0,MEMATTR 0xF 即 Normal WB */
#define HCR_GUEST 0x80000001UL

/* boot 时一次安装 SP_EL2 异常栈(见 start.S 的 bank 归一注释) */
void vcpu_init(void)
{
    __asm__ volatile("msr spsel, #1");
    __asm__ volatile("mov sp, %0" :: "r"((u64)&excp_stack[512]));
    __asm__ volatile("msr spsel, #0");
}

void vcpu_run(struct vcpu *v, u64 vttbr)
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
    __asm__ volatile("msr sp_el1, %0"   :: "r"(v->entry_sp));
    __asm__ volatile("msr spsr_el2, %0" :: "r"(v->spsr));
    __asm__ volatile("msr elr_el2, %0"  :: "r"(v->entry_pc));
    __asm__ volatile("tlbi vmalls12e1is; dsb sy; isb");

    vcpu_eret_enter(v);
    /* 正常返回 = guest 已 HVC exit(vcpu_exit 已清 VM 状态) */
}

void vcpu_exit(void)
{
    __asm__ volatile("msr hcr_el2, xzr");
    __asm__ volatile("msr vttbr_el2, xzr");
    __asm__ volatile("tlbi vmalls12e1is; dsb sy; isb");
    vcpu_return_host(cur_vcpu);
}
