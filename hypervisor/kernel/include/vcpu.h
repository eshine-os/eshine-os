/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#ifndef ESHINE_VCPU_H
#define ESHINE_VCPU_H

#include "kernel/include/types.h"

/* 陷阱帧:guest 完整现场。偏移即 asm 契约(vcpu_entry.S/irq_entry.S):
 * gprs[31]@0(x0@0...x30@240) sp@248 pc@256 pstate@264
 * host_ctx@272(x19@272...x28@344,sp@352,lr@360) exit_reason@368
 * wait_state@376 slice_left@384 wait_reason@392
 * vtimer_ctl@400 vtimer_cval@408(第五-八站尾扩,asm 不触及)
 * sp_el0@416(第十四站:SP_EL0 全 EL 共享,EL1 内核以它存 current,
 * 宿主 EL2 栈改驻 SP_EL2 后仍须逐陷阱存还,多 VM 共享硬件最后一份) */
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
    u64 wait_state;             /* 0=不在等 1=WAIT(WFI 陷阱) 2=WOKE(唤醒到点) */
    u64 slice_left;             /* 剩余量子(tick 计);耗尽 -> 出口帧转写切走 */
    u64 wait_reason;            /* 等待源:0=无 1=TIMER 2=RX(第六站两源等待) */
    u64 vtimer_ctl;             /* 第七站 per-VM vtimer ctx:cortex-a57 无 CNTV
                                 * 陷阱位(guest 直写物理),泵切出存/切入还 */
    u64 vtimer_cval;
    u64 sp_el0;                 /* guest SP_EL0(arm64 内核 current;第十四站) */
    u64 mdscr;                  /* 站53:per-VM MDSCR_EL1(MDCR_EL2.TDA 陷阱,
                                 * EC 0x18 代访;debug 族跨 VM 隔离) */
    u64 tpidr_el1;              /* 站68C-8:guest 核号随帧走。物理 TPIDR_EL1
                                 * EL2 从不存还=核间残留互染(实测 core0 语境
                                 * 读到 1:蹦床槽按 core1 算=SP_EL1 臂错核蹦
                                 * 床=幽灵链出生;簿记 rt_cur_arr 同按错核索
                                 * 引)。逐陷阱存还,每次 eret 强制还原。 */
    /* SIMD per-VM 帧(第十六站):Q0-Q31 + FPCR/FPSR。宿主零 FP
     * (-mgeneral-regs-only),泵在 vcpu_run 前装/后存(站七 vtimer
     * 同形),guest FP 现场跨客切换无损。16B 对齐(stp q 对要求);
     * asm 陷阱桩不触及此区(帧尾扩,先例 vtimer_ctl/cval/sp_el0)。
     * v8.0(a57)无 SVE,单份无银行,帧即完备。 */
    u64 q[64] __attribute__((aligned(16)));
    u64 fpcr;
    u64 fpsr;
    /* 站十六:全 EL 共享一份的物理面继续收编——异构双客(RTOS 裸 EL1 vs
     * Linux 全 stage-1)首次互换,泄漏全数现形:
     * VBAR_EL1(vm1 引导换向量表,vm0 注入中断跳错门);
     * CPACR_EL1(vm0 设 FPEN,他客若清零则 FP 断腿);
     * SCTLR/TTBR0/TCR(M=1 的 Linux 与 M=0 的 RTOS 互换,向量取指经
     * 他客 stage-1 表翻到垃圾 IPA 指令中止死);
     * LR0/VMCR(GIC 虚拟面:泵为 vm0 注入唤醒复用同颗 LR0,覆写 vm2
     * 未认领的 tick,Linux 丢拍冻死;VMCR=ICV PMR/GRPEN 底座同);
     * trap_left(掩蔽 IRQ 引导客的量子扳机之二)。 */
    u64 vbar;
    u64 cpacr;
    u64 sctlr;
    u64 ttbr0;
    u64 tcr;
    u64 lr_vtimer;               /* ICH_LR0 快照(泵边装存) */
    u64 vmcr;                    /* ICH_VMCR 快照(泵边装存) */
    u64 idx;                     /* 站19D:VM 内 vCPU 序(0/1;VMPIDR 低
                                 * 位与 vgic 帧身份);帧尾,asm 不触及 */
};

/* 站68C-8:asm 偏移契约(tpidr_el1 逐陷阱存还,与 sp@248/sp_el0@416 同律);
 * 站68C-13:idx 还原钳位(核号权威=帧内自带,段内污染活不过一量子) */
_Static_assert(__builtin_offsetof(struct vcpu, tpidr_el1) == 432,
               "vcpu.tpidr_el1 must be @432 for vcpu_entry.S");
_Static_assert(__builtin_offsetof(struct vcpu, idx) == 1032,
               "vcpu.idx must be @1032 for vcpu_entry.S clamp");

/* 退回宿主的原因码(vcpu_run 返回值) */
#define VCPU_EXIT   0UL         /* HVC #1:guest 主动退出 -> VM 层 TEARDOWN->DEAD */
#define VCPU_YIELD  1UL         /* HVC #2:主动让出 -> VM 层 RUNNING->PAUSED */
#define VCPU_FAULT  2UL         /* 非 HVC 异常 -> 诊断后 TEARDOWN->DEAD */
#define VCPU_WAIT   3UL         /* HCR.TWI 下 guest WFI -> 泵中让位,vtimer 唤醒 */
#define VCPU_PREEMPT 4UL        /* tick 量子耗尽 -> 出口帧转写切走,PAUSED 续跑同路 */

void vcpu_init(void);
u64  vcpu_run(struct vcpu *v, u64 vttbr);   /* 正常返回值 = exit_reason */
void vcpu_exit(void) __attribute__((noreturn));
void vcpu_flush_all_tables(void);   /* 站19D:生命周期口熄表(槽复用防泄) */

/* SIMD per-VM 帧存取(第十六站);泵边调用,guest 已切出,现场属帧主 */
void fpsimd_save(struct vcpu *v);
void fpsimd_load(struct vcpu *v);

/* trap 出口调度点与 guest fault 诊断(el1_sync 与 mmio_dispatch 共用) */
void resume_or_preempt(struct vcpu *v) __attribute__((noreturn));
void guest_fault(struct vcpu *v, u64 esr) __attribute__((noreturn));

/* 等待源(第六站):WAIT 入口由泵判定,WOKE 后泵按此走分支 */
#define VCPU_WAIT_TIMER 1UL     /* vtimer 武装着睡:唤醒走注 LR + vtimer 链 */
#define VCPU_WAIT_RX    2UL     /* 设备中断等待(串口 RX/virtio 完成等非
                                 * TIMER 源):事件就绪经泵注入即醒 */

/* vcpu_entry.S */
void vcpu_enter_guest(struct vcpu *v);   /* 正常"返回"= guest 已 retire */
void trap_resume(struct vcpu *v) __attribute__((noreturn));
void vcpu_return_host(struct vcpu *v) __attribute__((noreturn));

/* 站十八D per-CPU(0/1);站19D 扩至三核(0/1/2)。cur_vcpus[] 按
 * mpidr.aff0 索引;旧名 cur_vcpu 保留为宏,存量 C 引用零改动。
 * asm 桩直用数组符号(MPIDR 索引),见 vcpu_entry.S/irq_entry.S。 */
extern struct vcpu *cur_vcpus[4];         /* 站19D 三核+站20 四核 */
extern u64 excp_stacks[4][512];

static inline u32 cpu_id(void)
{
    u64 m;

    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(m));
    return (u32)(m & 0xffUL);
}

#define cur_vcpu (cur_vcpus[cpu_id()])

#endif /* ESHINE_VCPU_H */
