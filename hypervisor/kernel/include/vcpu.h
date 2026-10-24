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
};

/* 退回宿主的原因码(vcpu_run 返回值) */
#define VCPU_EXIT   0UL         /* HVC #1:guest 主动退出 -> VM 层 TEARDOWN->DEAD */
#define VCPU_YIELD  1UL         /* HVC #2:主动让出 -> VM 层 RUNNING->PAUSED */
#define VCPU_FAULT  2UL         /* 非 HVC 异常 -> 诊断后 TEARDOWN->DEAD */
#define VCPU_WAIT   3UL         /* HCR.TWI 下 guest WFI -> 泵中让位,vtimer 唤醒 */
#define VCPU_PREEMPT 4UL        /* tick 量子耗尽 -> 出口帧转写切走,PAUSED 续跑同路 */

void vcpu_init(void);
u64  vcpu_run(struct vcpu *v, u64 vttbr);   /* 正常返回值 = exit_reason */
void vcpu_exit(void) __attribute__((noreturn));

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

extern struct vcpu *cur_vcpu;   /* asm 桩引用;单活跃 vCPU(无 SMP) */

#endif /* ESHINE_VCPU_H */
