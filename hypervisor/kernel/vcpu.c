/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "arch/arm64/include/mm.h"
#include "kernel/include/vcpu.h"

static struct vcpu *cur_vcpu;

/* HCR_EL2: RW(64-bit EL1) | VM(stage-2 使能);DC=0,MEMATTR 0xF 即 Normal WB */
#define HCR_GUEST 0x80000001UL

void vcpu_run(struct vcpu *v)
{
    u64 sctlr, vttbr;

    cur_vcpu = v;

    /* EL1 stage-1 关闭(裸 guest 走纯 stage-2) */
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    sctlr &= ~1UL;
    __asm__ volatile("msr sctlr_el1, %0" :: "r"(sctlr));

    __asm__ volatile("msr vtcr_el2, %0" :: "r"(VTCR_VALUE));
    vttbr = (u64)stage2_pgd();              /* EL2 无 MMU ⇒ 指针即物理地址, VMID=0 */
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
