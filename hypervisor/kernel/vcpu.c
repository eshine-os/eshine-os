/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "arch/arm64/include/mm.h"
#include "drivers/include/gicv3.h"
#include "kernel/include/vcpu.h"

struct vcpu *cur_vcpu;               /* asm 桩引用,不再 static */

/* EL2-from-EL1 异常落点专用栈(硬件强制 SPSel=1 -> SP_EL2)。
 * 非 static:el1_trap 每次入口把它归位到栈顶 —— handler 链不退栈,
 * eret/retire 都不还 SP_EL2,不归位则每次陷阱净泄一截,累计碾穿栈底。 */
u64 excp_stack[512] __attribute__((aligned(16)));

/* HCR_EL2(guest): RW(64-bit EL1) | VM(stage-2) | IMO(ICV 重定向+IRQ 路由)
 * | AMO(物理 SError 路由 EL2——第十四站:Linux 的 S1 走表访问在 QEMU 侧
 * 报物理 SError,曾因 AMO=0 直送 EL1 成风暴;现收编宿主侧诊断/隔离)
 * | TWI(WFI 陷阱化 -> VCPU_WAIT);IMO 位见 gicv3.h HCR_EL2_HOST 注 */
#define HCR_GUEST 0x80002031UL

void vcpu_init(void)
{
    /* 站十四后无事可做:宿主 C 世界已驻 SP_EL2(start.S),异常入 EL2
     * 硬件强制 SPSel=1 且各陷阱桩自置 excp_stack 顶;旧的"预载 SP_EL2
     * 供异常进入"在宿主同驻 SP_EL2 后反而会碾掉 C 栈,撤销。
     * excp_stack 声明保留(陷阱桩仍以其为 EL2 异常栈)。 */
}

u64 vcpu_run(struct vcpu *v, u64 vttbr)
{

    cur_vcpu = v;

    /* 第十四站修正:进场不再碰 stage-1——Linux 自启 MMU,曾在此被
     * 每次进场强清 M 位而翻车(裸机时代防御,对 MMU 客是破坏)。
     * stage-1 全权归 guest;宿主只在 VM 出生时清一次(见 vm_start,
     * 防上一位 guest 的陈旧页表随槽位复用泄给新客)。 */
    __asm__ volatile("msr vtcr_el2, %0" :: "r"(VTCR_VALUE));
    __asm__ volatile("msr vttbr_el2, %0" :: "r"(vttbr));
    /* FPEN=11:guest 的 EL0/EL1 SIMD 访问不陷阱(第十四站:Linux 内核用
     * NEON,裸机 guest 全部 -mgeneral-regs-only 从未触发 EC=0x21)。
     * CPACR_EL1 不管 EL2;宿主零 FP(-mgeneral-regs-only)——永久放行
     * 无副作用。已知限制:SIMD per-VM ctx 不保存,多 FP guest 并跑会
     * 串台(当前唯一 FP 使用者是 Linux 槽;专站补)。 */
    __asm__ volatile("msr cpacr_el1, %0" :: "r"(3UL << 22));   /* FPEN=11(bits[23:22]) */
    __asm__ volatile("msr hcr_el2, %0" :: "r"(HCR_GUEST));
    __asm__ volatile("tlbi vmalls12e1is; dsb sy; isb");

    vcpu_enter_guest(v);            /* sp_el1/elr/spsr 由帧加载,不再单设 */
    return v->exit_reason;          /* 正常返回 = guest 已 retire */
}

void vcpu_exit(void)
{
    __asm__ volatile("msr hcr_el2, %0" :: "r"(HCR_EL2_HOST));   /* 宿主态可收 IRQ */
    __asm__ volatile("msr vttbr_el2, xzr");
    __asm__ volatile("tlbi vmalls12e1is; dsb sy; isb");
    __asm__ volatile("msr daifclr, #2");   /* 异常进入自带掩码;回宿主语境必须解 IRQ,
                                            * 否则 shell 永远收不到 tick(活体心跳定位) */
    vcpu_return_host(cur_vcpu);
}
