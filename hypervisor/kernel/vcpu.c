/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "arch/arm64/include/mm.h"
#include "drivers/include/gicv3.h"
#include "kernel/include/vcpu.h"

struct vcpu *cur_vcpus[3];            /* 站十八D per-CPU;站19D 三核;宏 cur_vcpu 保持旧名 */

/* EL2-from-EL1 异常落点专用栈(硬件强制 SPSel=1 -> SP_EL2)。
 * 站十八D:per-CPU,每核一页(512*8);asm 桩按 mpidr.aff0 索引,
 * 归位到本核栈顶 —— handler 链不退栈,eret/retire 都不还 SP_EL2,
 * 不归位则每次陷阱净泄一截,累计碾穿栈底。 */
u64 excp_stacks[3][512] __attribute__((aligned(16)));

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
    __asm__ volatile("msr cptr_el2, xzr");   /* 第十六站:TFP 归零防御——
                                              * EL2 侧 stp/ldp q 不许被陷阱
                                              * (复位本为 0,显式立誓) */
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
    /* CPACR_EL1 归 guest(站十五):站十四曾在此强写 3<<22,实为越权
     * 之二——FPEN 真位是 [21:20](sysreg.h:EL1EN=bit20/EL0EN=bit21),
     * 整寄存器写反而把内核 __cpu_setup 布好的 FPEN 清零:首次抢占重
     * 入后,用户态 FP 恢复路径 fpsimd_load_state 首条 SIMD 即 EC=0x07
     * 陷阱→"Unhandled exception" 恐慌(Linux PID1 上机实测)。内核在
     * __cpu_setup 自己设 3<<20,EL1/EL2 陷阱不牵涉我们;宿主零 FP
     * (-mgeneral-regs-only),跨陷阱不碰 FP 现状。同哲学:SP_EL0 归
     * guest、vtimer 归 guest,CPACR 也归 guest。已知限制:SIMD per-VM
     * ctx 不保存,多 FP guest 并跑会串台(当前唯一 FP 用户是 Linux 槽)。 */
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

/* SIMD per-VM 帧存取(第十六站):16 对 stp/ldp + FPCR/FPSR。
 * 只在泵边调用(guest 已切出,现场属帧主);宿主代码零 FP,帧在
 * 硬件里的短暂驻留不被宿主染指。帧首 16B 对齐(vcpu.h q[] 契约)。 */
void fpsimd_save(struct vcpu *v)
{
    u64 *p = v->q;
    u64 fpcr, fpsr;

    __asm__ volatile(
        "stp q0,  q1,  [%0], #32\n\t"
        "stp q2,  q3,  [%0], #32\n\t"
        "stp q4,  q5,  [%0], #32\n\t"
        "stp q6,  q7,  [%0], #32\n\t"
        "stp q8,  q9,  [%0], #32\n\t"
        "stp q10, q11, [%0], #32\n\t"
        "stp q12, q13, [%0], #32\n\t"
        "stp q14, q15, [%0], #32\n\t"
        "stp q16, q17, [%0], #32\n\t"
        "stp q18, q19, [%0], #32\n\t"
        "stp q20, q21, [%0], #32\n\t"
        "stp q22, q23, [%0], #32\n\t"
        "stp q24, q25, [%0], #32\n\t"
        "stp q26, q27, [%0], #32\n\t"
        "stp q28, q29, [%0], #32\n\t"
        "stp q30, q31, [%0], #32\n\t"
        "mrs %1, fpcr\n\t"
        "mrs %2, fpsr\n\t"
        : "+r"(p), "=r"(fpcr), "=r"(fpsr) :: "memory");
    v->fpcr = fpcr;
    v->fpsr = fpsr;
}

void fpsimd_load(struct vcpu *v)
{
    u64 *p = v->q;

    __asm__ volatile(
        "ldp q0,  q1,  [%0], #32\n\t"
        "ldp q2,  q3,  [%0], #32\n\t"
        "ldp q4,  q5,  [%0], #32\n\t"
        "ldp q6,  q7,  [%0], #32\n\t"
        "ldp q8,  q9,  [%0], #32\n\t"
        "ldp q10, q11, [%0], #32\n\t"
        "ldp q12, q13, [%0], #32\n\t"
        "ldp q14, q15, [%0], #32\n\t"
        "ldp q16, q17, [%0], #32\n\t"
        "ldp q18, q19, [%0], #32\n\t"
        "ldp q20, q21, [%0], #32\n\t"
        "ldp q22, q23, [%0], #32\n\t"
        "ldp q24, q25, [%0], #32\n\t"
        "ldp q26, q27, [%0], #32\n\t"
        "ldp q28, q29, [%0], #32\n\t"
        "ldp q30, q31, [%0], #32\n\t"
        "msr fpcr, %1\n\t"
        "msr fpsr, %2\n\t"
        : "+r"(p)
        : "r"(v->fpcr), "r"(v->fpsr) : "memory");
}
