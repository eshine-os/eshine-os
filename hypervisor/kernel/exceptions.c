/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "drivers/include/gicv3.h"
#include "drivers/include/timer.h"
#include "kernel/include/console.h"
#include "kernel/include/mmio.h"
#include "kernel/include/sched.h"
#include "kernel/include/types.h"
#include "kernel/include/vcpu.h"
#include "kernel/include/vm.h"           /* vm_of_vcpu(探针) */

void el2_sync(void)
{
    u64 esr, far;

    __asm__ volatile("mrs %0, esr_el2" : "=r"(esr));
    __asm__ volatile("mrs %0, far_el2" : "=r"(far));
    console_puts("\n[EXC] unexpected synchronous exception at EL2\n");
    console_puts("  ESR_EL2 = 0x");
    console_puthex(esr);
    console_puts("\n  FAR_EL2 = 0x");
    console_puthex(far);
    console_puts("\nhalting (WFI)\n");
    for (;;)
        __asm__ volatile("wfi");
}

/* EL2 IRQ:el2_trap(asm)保存被中断现场后 bl 进来。
 * 26=宿主 CNTHP tick(EOIR+DIR);VTIMER=guest CNTV(B 相注入,EOIR 只降优先级,
 * 物理线由 guest 虚 EOI 经 HW-LR 硬件联动撤销);未知 intid 记账不 panic
 * (中断是数据,同 fault 隔离哲学)。spurious(1023)不动 EOIR。 */
/* 宿主态 tick:只服务 timer,不消耗量子(shell 命令即原子段)。
 * 27 分支为"陪睡中的前台 WAIT VM"到点(单等待者):置 WOKE 交泵。 */
void el2_irq_host(void)
{
    u64 intid = gicv3_claim();

    if (intid == GIC_PPI_HPTIMER) {
        timer_handler();
        /* HVC 风暴的宿主段也是 guest 活负载:计入其量子。
         * PREEMPT 的扳机在 HVC 出口(resume_or_preempt)。 */
        if (cur_vcpu && cur_vcpu->slice_left > 0)
            cur_vcpu->slice_left--;
        sched_vtimer_sweep();               /* 每拍表扫:多等待者到期判定 */
        gicv3_eoi_dir(intid);
    } else if (intid == GIC_PPI_VTIMER) {
        /* 站十八A三分支:真唤醒了睡客才关线(睡客没人替它落线);
         * RUNNING 轮转客的未认领在途拍不碰物理线——HW LR pending 由
         * 物理线驱动,关了即蒸发([53] 闪断根因:站16 注释宣称"关线只
         * 属睡客路径"但代码未落,封窗的陷阱量子机器又随站16 退役):
         * EOI 只降优先级线保挂,置回业主标志,泵下一圈以它为旋转起点
         * 进场消费(线挂期间重复 27 由本分支廉价吸收;泵由 shell 每圈
         * 必调,无死循环路径);无睡客无在途=无主陈旧挂起,熄线收尸
         * (老行为)。 */
        if (sched_vtimer_sweep())
            vgic_wake_waiter();
        else if (vtimer_inflight()) {
            gicv3_eoi_drop(GIC_PPI_VTIMER);
            sched_vtimer_recall = 1UL;
        } else
            vgic_wake_waiter();
    } else if (intid != IAR_SPURIOUS) {
        gicv3_eoi_dir(intid);
    }
}

/* guest 运行态物理中断(lower-EL 槽):宿主栈帧建在 excp_stack。
 * tick 服务 timer 同时递减量子,耗尽置 PREEMPT 由桩出口帧转写切走;
 * 27 是该 guest 自己的 vtimer 到点:置 WOKE+关线,注入留给下次泵到它。 */
void el2_irq_guest(void)
{
    u64 intid = gicv3_claim();

    if (intid == GIC_PPI_HPTIMER) {
        timer_handler();
        if (cur_vcpu->slice_left > 0)
            cur_vcpu->slice_left--;
        if (cur_vcpu->slice_left == 0) {
            cur_vcpu->exit_reason = VCPU_PREEMPT;   /* 桩出口转写后弃帧 */
            sched_need_resched = 1;
        }
        sched_vtimer_sweep();               /* 每拍表扫:多等待者到期判定 */
        gicv3_eoi_dir(intid);
    } else if (intid == GIC_PPI_VTIMER) {
        /* 站十四:此路 guest 在跑,其 timer_handler 是轮询式
         * (读 CNTV ISTATUS 才认账)——关线/熄火会让它读到 0 而
         * IRQ_NONE 丢 tick,Linux 实测 jiffies 冻死、整靴停在首个
         * msleep。正确姿势:不许动 CNTV(guest 的 handler 自会
         * IMASK 落线+重装下一拍),ctx 也不熄(泵切出时保存的才是
         * 真相);关线+熄火只属于睡客路径(el2_irq_host:WAIT 中
         * 没人替它落线)。电平重挂窗口=guest 进 handler 的几条
         * 指令,陷阱间恒前进,自限。EOI-drop 照做:claim 抬起的
         * 物理 RPR 不还,后续 26/27 全哑(物理 active 位不撤无妨,
         * active+pending 照常可投)。 */
        gicv3_eoi_drop(GIC_PPI_VTIMER);
        vgic_inject_vtimer(vgic_ppi_prio(&vm_of_vcpu(cur_vcpu)->vgic,
                                         GIC_PPI_VTIMER));
    } else if (intid != IAR_SPURIOUS) {
        gicv3_eoi_dir(intid);
    }
}

void el2_unexpected(void);

/* lower-EL 物理 SError(AMO 路由,第十四站):guest 侧不可修复的异步错,
 * 现场全打 + 隔离死——诊断期首要取证口。 */
void el1_serror(void)
{
    u64 esr, far, elr;

    __asm__ volatile("mrs %0, esr_el2" : "=r"(esr));
    __asm__ volatile("mrs %0, far_el2" : "=r"(far));
    __asm__ volatile("mrs %0, elr_el2" : "=r"(elr));
    console_puts("[SERROR-from-EL1] ESR=");
    console_puthex(esr);
    console_puts(" FAR=");
    console_puthex(far);
    console_puts(" ELR=");
    console_puthex(elr);
    console_puts("\n");
    if (cur_vcpu) {
        guest_fault(cur_vcpu, esr);   /* 隔离死:同 fault 哲学 */
    }
    el2_unexpected();
}

void el2_unexpected(void)
{
    u64 esr;

    __asm__ volatile("mrs %0, esr_el2" : "=r"(esr));
    console_puts("\n[EXC] unexpected exception (fiq/serror/lower-EL slot)\n");
    console_puts("  ESR_EL2 = 0x");
    console_puthex(esr);
    console_puts("\nhalting (WFI)\n");
    for (;;)
        __asm__ volatile("wfi");
}

/* lower-EL AArch64 sync:asm 桩(el1_trap)保存 guest 帧后调入,x0=vcpu。
 * HVC #1/#2/#4 控制类分派(第六站:#0 putc/#3 getc 退役,IO 面走
 * vuart MMIO 陷阱仿真;未知 imm 默认拒绝=guest fault 隔离死)。
 * EC 0x24 = data abort -> mmio_dispatch(仿真窗口);非 HVC/非窗口 =
 * guest fault -> 诊断+VM 隔离死亡。帧内改值即改 guest 现场,
 * trap_resume 按帧续跑。 */
/* HVC 出口的集中调度点(参考实现形态:guest trap 退出即查):量子尽则
 * PREEMPT 弃帧走 vcpu_exit(帧已在 el1_trap 存好,无需转写)。
 * getc 轮询风暴下 tick 多落宿主态,slice 递减在 el2_irq_host,
 * PREEMPT 的扳机就位在这里——两处合起来量子才真正闭合。 */
void resume_or_preempt(struct vcpu *v)
{
    if (v->slice_left == 0UL) {
        v->exit_reason = VCPU_PREEMPT;
        sched_preempts++;
        vcpu_exit();
    }
    trap_resume(v);
}

/* guest fault 诊断与隔离死:读陷阱帧展示野访问现场(第三站),
 * 第六站补 HPFAR_EL2——stage-2 fault 的 IPA 权威来源,亦是活探针
 * (仿真窗口未命中时的地面真值)。 */
void guest_fault(struct vcpu *v, u64 esr) __attribute__((noreturn));
void guest_fault(struct vcpu *v, u64 esr)
{
    u64 far, hpfar;

    v->exit_reason = VCPU_FAULT;
    __asm__ volatile("mrs %0, far_el2" : "=r"(far));
    __asm__ volatile("mrs %0, hpfar_el2" : "=r"(hpfar));
    console_puts("fault: ESR=0x");
    console_puthex(esr);
    console_puts(" FAR=0x");
    console_puthex(far);
    console_puts(" HPFAR=0x");
    console_puthex(hpfar);
    console_puts(" pc=0x");
    console_puthex(v->pc);
    console_puts(" sp=0x");
    console_puthex(v->sp);
    console_puts(" x0=0x");
    console_puthex(v->gprs[0]);
    console_puts(" x1=0x");
    console_puthex(v->gprs[1]);
    console_puts(" x2=0x");
    console_puthex(v->gprs[2]);
    console_puts(" x3=0x");
    console_puthex(v->gprs[3]);
    console_puts("\n");
    vcpu_exit();
}

void el1_sync(struct vcpu *v) __attribute__((noreturn));
void el1_sync(struct vcpu *v)
{
    u64 esr, ec, imm;

    __asm__ volatile("mrs %0, esr_el2" : "=r"(esr));
    ec = esr >> 26;
    imm = esr & 0xFFFF;

    if (ec == 0x16 && imm == 1) {           /* HVC #1 exit */
        v->exit_reason = VCPU_EXIT;
        vcpu_exit();
    } else if (ec == 0x16 && imm == 2) {    /* HVC #2 yield */
        v->exit_reason = VCPU_YIELD;
        vcpu_exit();
    } else if (ec == 0x16 && imm == 4) {    /* HVC #4 host:前台交还 RX */
        if (cur_vcpu == sched_console_owner) {
            sched_console_owner = NULL;
            console_puts("console -> host\n");
        }
        resume_or_preempt(v);
    } else if (ec == 0x24) {               /* data abort(EL1):仿真窗口分发 */
        mmio_dispatch(v, esr);             /* 命中 EMULATED 设备则仿真续跑,
                                           * 无窗/无现场回落 guest_fault */
    } else if (ec == 0x01 && (imm & 1) == 0) {  /* HCR.TWI:guest WFI -> 等待态 */
        v->exit_reason = VCPU_WAIT;
        vcpu_exit();
    } else {                                /* guest fault:诊断 -> VM 隔离死 */
        guest_fault(v, esr);
    }
}
