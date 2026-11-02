/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "drivers/include/gicv3.h"
#include "drivers/include/timer.h"
#include "drivers/include/vuart.h"       /* 站22A:泊位冲刷(TWI 停泊即冲 fg/bg 半行) */
#include "kernel/include/console.h"
#include "kernel/include/mmio.h"
#include "kernel/include/sched.h"
#include "kernel/include/types.h"
#include "kernel/include/util.h"        /* memset:PSCI CPU_ON 帧清零(站20 起在用,
                                        * 隐式声明警告站22A 补声明) */
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
        trail_add(11, timer_ticks(), cur_vcpu ? 1 : 0);
        /* HVC 风暴的宿主段也是 guest 活负载:计入其量子。
         * PREEMPT 的扳机在 HVC 出口(resume_or_preempt)。 */
        if (cur_vcpu && cur_vcpu->slice_left > 0)
            cur_vcpu->slice_left--;
        sched_vtimer_sweep();               /* 每拍表扫:多等待者到期判定 */
        gicv3_eoi_dir(intid);
    } else if (intid == GIC_PPI_VTIMER) {
        trail_add(15, timer_ticks(), cur_vcpu ? 1 : 0);
        /* 站十八A三分支:真唤醒了睡客才关线(睡客没人替它落线);
         * RUNNING 轮转客的未认领在途拍不碰物理线——HW LR pending 由
         * 物理线驱动,关了即蒸发([53] 闪断根因:站16 注释宣称"关线只
         * 属睡客路径"但代码未落,封窗的陷阱量子机器又随站16 退役):
         * EOI 只降优先级线保挂,置回业主标志,泵下一圈以它为旋转起点
         * 进场消费(线挂期间重复 27 由本分支廉价吸收;泵由 shell 每圈
         * 必调,无死循环路径);无睡客无在途=无主陈旧挂起,熄线收尸
         * (老行为)。 */
        if (sched_vtimer_sweep()) {
            trail_add(6, 1, 0);
            vgic_wake_waiter();
        } else if (vtimer_inflight()) {
            trail_add(7, 0, 0);
            gicv3_eoi_drop(GIC_PPI_VTIMER);
            sched_vtimer_recall[cpu_id()] = 1UL;
        } else {
            trail_add(8, 0, 0);
            /* 站19D 收口:无主挂起不再就地收尸。收尸写 CNTV_CTL=0
             * 是 per-pCPU 的:此窗 sweep 只认停泊者,ws 仍在 0 的
             * 在跑客(WFI 已入、簿记未竟)会被误杀线,出栈存得
             * ctl=0 按 RX 归类停泊,sweep 永不再匹配(判据要
             * TIMER),一个 tick 即永眠——[53]/[41] 闪断族在 SMP
             * 客上的放大面,vm2 单核靴实测冻死于首拍窗。改为只
             * 降线保挂:线挂期间重复 27 由本分支廉价吸收,停泊
             * 落定后 26 每拍扫描(smp.c 已补)接管再发现,有界
             * (≤1s)自愈;真无主(客已死/停)时下一位进场的
             * WOKE 熄火把线合拢。
             * 站28 vtimer 专站修真(第四类,本站定谳):上列三分支
             * 漏了一种活火——可跑客的 vtimer 在宿主片隙(量轮换
             * 片隙/壳命令窗)到点:sweep 只认泊者、inflight 只认
             * LR 在途,双不中即落此"无主"收尸(仅 eoi_drop):
             * 物理 27 被 claim 成永 active 且零注入,线恒高、
             * active 挡再投递,滴答永死——"回复在而 tick 冻结"
             * 的宿主真身(trail ev15→ev8 后 CPU0 27 绝迹;cval
             * 冻结≈40s 落后、pend+act 双置、vLR 陈尸四证齐)。
             * 第四类判据=本核 sched_vtimer_last 业主可跑且 PPI27
             * 使能:照 el2_irq_guest 律注入,并回写业主帧 lr 槽
             * (泵下一圈进场装回;无此写则入场恢复以旧帧覆掉本次
             * 注入=站16"活不过切换"律),guest 消费后虚 EOI 经
             * HW-LR 联动撤物理 active,闭环。 */
            struct vcpu *ov = vm_vtimer_runnable_owner(cpu_id());
            struct eshine_vm *ovm = ov ? vm_of_vcpu(ov) : NULL;

            gicv3_eoi_drop(GIC_PPI_VTIMER);
            if (ovm && vgic_ppi_enabled(&ovm->vgic, GIC_PPI_VTIMER)) {
                vgic_inject_vtimer(vgic_ppi_prio(&ovm->vgic,
                                                 GIC_PPI_VTIMER));
                __asm__ volatile("mrs %0, ich_lr0_el2"
                                 : "=r"(ov->lr_vtimer));
                trail_add(16, 1, 0);
            }
        }
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
        trail_add(13, timer_ticks(), cur_vcpu ? 1 : 0);
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
         * active+pending 照常可投)。
         * 站28 vtimer 专站第二脸(跨 VM 串台窗,站16"串台消费"
         * 律的在途拍面):27 在"别人跑"的片里到点(同核轮转多客,
         * 如 vm1 spinner 与 vm0.v0 共 CPU0)——旧代码无条件注硬
         * LR0,虚 27 被送进**跑者**的 GIC 面:跑者无 27 处理面
         * 永不认领,HW-LR 挂起又由恒高物理线驱动,跑者被喂成
         * 认领-EOI-再挂无限风暴(TCG 满速磨穿,泵"跑到出"楔死
         * 宿主壳=hb 门"回复<2+串行撕裂"脸,-d int 定谳);认领
         * 窗内业主进场则硬 LR 已被跑者帧保存劫走,物 理 active
         * 再无虚 EOI 撤销=业主滴答永死。修真:跑者==业主照旧硬
         * 注(即时消费);跑者≠业主且业主可跑——**只写业主帧 lr
         * 槽**(同源 vgic_make_vtimer_lr,不碰跑者硬 LR),泵下
         * 一圈进场装回、业主消费、虚 EOI 撤物理 active 闭环;
         * 业主泊者(可跑判空)不注入——每拍扫描的 cval 再发现/
         * WOKE 注入照走(站25C 兜底),物理 active 同由其虚 EOI
         * 合拢。 */
        {
            struct vcpu *ov = vm_vtimer_runnable_owner(cpu_id());

            gicv3_eoi_drop(GIC_PPI_VTIMER);
            trail_add(14, timer_ticks(), cur_vcpu ? 1 : 0);
            if (ov == cur_vcpu) {
                vgic_inject_vtimer(vgic_ppi_prio(&vm_of_vcpu(cur_vcpu)->vgic,
                                                 GIC_PPI_VTIMER));
            } else if (ov) {
                ov->lr_vtimer = vgic_make_vtimer_lr(
                    vgic_ppi_prio(&vm_of_vcpu(ov)->vgic, GIC_PPI_VTIMER));
                trail_add(17, 1, 0);
            }
        }
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
    } else if (ec == 0x16 && imm == 5) {    /* HVC #5 fg 查询(站20,心跳
                                             * 门控的宿主侧只读面):x0=1
                                             * 本 vCPU 是 console owner。
                                             * 用户令:vmrun 后台期心跳
                                             * 印字骚扰宿主/他客串口,
                                             * 客自问前台才印(交互噪音
                                             * 归零,门禁并发判据窗内
                                             * vmconsole 0 照见) */
        v->gprs[0] = (v == sched_console_owner) ? 1UL : 0UL;
        resume_or_preempt(v);
    } else if (ec == 0x17 || (ec == 0x16 && imm == 0)) {
                                          /* SMC(0x17)/HVC#0(0x16):PSCI 转发
                                           * (站19D)——guest 视角固件即我们;
                                           * 探针实证 QEMU 机器级截获 EL1 的
                                           * SMC(TCG conduit 优先),故树走
                                           * method=hvc;0x17 分支留真硬件形。
                                           * 完成型调用:ELR 已在下条,x0 回写
                                           * 帧后续跑 */
        u64 fn = v->gprs[0];

        if (fn == 0x84000000UL) {
            v->gprs[0] = 0x00010001UL;     /* PSCI_VERSION:1.1 */
        } else if (fn == 0xc4000003UL) {  /* CPU_ON:站二十泛化——任意客的
                                           * 次核上线(拆站 19D 的 vm2 硬绑):
                                           * 目标帧号 = x1 低字节(客视角
                                           * MPIDR=VMPIDR 0x80000000|idx,
                                           * aff0 即帧号),poke 按映射核 */
            struct eshine_vm *tvm = vm_of_vcpu(v);
            u32 vi = (u32)(v->gprs[1] & 0xffUL);
            struct vcpu *tv = (vi < ESHINE_MAX_VCPUS) ?
                              &tvm->vcpus[vi] : NULL;

            if (vi == 0UL || tv == NULL ||      /* 核 0 恒在线,不许再 on */
                tvm->state != VM_RUNNING || tv->wait_state != 1UL) {
                v->gprs[0] = 0xfffffff6UL;    /* DENIED */
            } else {
                memset(tv->gprs, 0, sizeof(tv->gprs));
                tv->pc = v->gprs[2];           /* entry_point */
                tv->sp = 0UL;                  /* 入口自配(Linux 读 park
                                              * 服务页;RTOS 次核入口从
                                              * 链接符号取栈,同不依赖) */
                tv->pstate = 0x3C5UL;          /* EL1h + DAIF 全掩 */
                tv->gprs[0] = v->gprs[3];      /* context_id 捎带 */
                tv->exit_reason = VCPU_EXIT;
                tv->vtimer_ctl = 0UL;
                tv->vtimer_cval = 0UL;
                tv->sp_el0 = 0UL;
                tv->wait_reason = 0UL;
                tv->wait_state = 0UL;          /* 出泊:映射核专泵即扫即跑 */
                v->gprs[0] = 0UL;              /* PSCI_SUCCESS */
                sched_poke_cpu(vm_cpu_map[tvm->vmid][vi]);
            }
        } else if (fn == 0x84000002UL) {  /* 站55 CPU_OFF:调用者帧泊停
                                           * (次核下线,v0=wait 无源泊,SGI
                                           * 到发可再醒=记档,真消费者
                                           * 召唤再硬化);核 0 恒在拒 */
            if (v->idx == 0UL) {
                v->gprs[0] = 0xfffffffdUL;    /* DENIED */
                resume_or_preempt(v);
            }
            v->wait_state = 1UL;
            v->wait_reason = 0UL;
            v->exit_reason = VCPU_WAIT;
            vcpu_exit();
        } else if (fn == 0x84000004UL) {  /* 站55 AFFINITY_INFO:x1=客视角
                                           * MPIDR(0x80000000|idx)→0=ON */
            struct eshine_vm *avm = vm_of_vcpu(v);
            u32 vi = (u32)(v->gprs[1] & 0xffUL);

            if (vi < vm_vcpu_count(avm->vmid))
                v->gprs[0] = 0UL;          /* ON */
            else
                v->gprs[0] = 0xfffffffdUL; /* DENIED(无此帧) */
        } else if (fn == 0x84000008UL) {  /* 站55 SYSTEM_OFF:干净关机
                                           * (VM_DEAD 无 fault 行) */
            console_puts("vm");
            console_putc((char)('0' + vm_of_vcpu(v)->vmid));
            console_puts(": system off (psci)\n");
            v->exit_reason = VCPU_EXIT;
            vcpu_exit();
        } else if (fn == 0x84000009UL) {  /* 站55 SYSTEM_RESET:teardown+
                                           * 重建+重启(泵出口复位链) */
            u32 rid = vm_of_vcpu(v)->vmid;

            vm_request_reset(rid);
            console_puts("vm");
            console_putc((char)('0' + rid));
            console_puts(": system reset (psci)\n");
            v->exit_reason = VCPU_EXIT;
            vcpu_exit();
        } else if (fn == 0x84000010UL) {  /* PSCI_FEATURES:全不掲,按位
                                           * 探测降级(cpuidle 各态即哑) */
            v->gprs[0] = ~0UL;
        } else if (fn == 0xc4000001UL) {  /* CPU_SUSPEND:standby 空转语义 */
            v->gprs[0] = 0UL;               /* power_level>0 未建,残余记册 */
        } else {                            /* CPU_OFF/MIGRATE/SYSTEM_*:不支持 */
            v->gprs[0] = ~0UL;
        }
        resume_or_preempt(v);
    } else if (ec == 0x18) {               /* 系统寄存器陷阱(站19D):唯一
                                           * 在用例=ICC_SGI1R_EL1 写(Linux
                                           * SMP 引导期 IPI;sysreg(3,0,
                                           * 12,11,5),ISS 场位按基准参考实现
                                           * traps.c 实证布局解码)——EL1
                                           * 直写无 vGIC 硬件面,截获转
                                           * vgic 挂起面,泵走 LR2 注入。 */
        u32 iss = (u32)esr;
        u32 rt = (iss >> 5) & 0x1Fu;
        u32 op0 = (iss >> 20) & 0xFu;
        u32 op1 = (iss >> 14) & 0x7u;
        u32 op2 = (iss >> 17) & 0x7u;
        u32 crn = (iss >> 10) & 0xFu;
        u32 crm = (iss >> 1) & 0xFu;

        if ((iss & 1u) == 0u && op0 == 3u && op1 == 0u &&
            crn == 12u && crm == 11u && op2 == 5u) {
            struct eshine_vm *vm = vm_of_vcpu(v);
            u32 hits = vgic_sgi1r_write(&vm->vgic,
                                        rt == 31u ? 0UL : v->gprs[rt]);
            u32 f;

            for (f = 0; f < ESHINE_MAX_VCPUS; f++) {
                if ((hits >> f) & 1u) {
                    struct vcpu *tv = &vm->vcpus[f];

                    if (tv->wait_state == 1UL) {
                        tv->wait_state = 0UL;   /* SGI 即中断:直通唤醒 */
                        tv->wait_reason = 0UL;
                    }
                    /* 站二十:poke 按命中帧的映射核(泛化站19D 的
                     * 双发——vm0.vcpu1 在 CPU3、vm2 帧在 CPU1/2,按
                     * vm_cpu_map 精确 poke;SEV 广播本性不变,函数名
                     * 是意图标注) */
                    sched_poke_cpu(vm_cpu_map[vm->vmid][f]);
                }
            }
            v->pc += 4UL;                    /* MSR 型写:代 retire 续跑 */
            resume_or_preempt(v);
        }
        /* 站53:MDSCR_EL1(sysreg 2,0,c1,c3,2)——MDCR_EL2.TDA 陷阱下
         * 的 per-VM 代访:读还本 VM 槽/写存本 VM 槽(debug 族跨 VM
         * 隔离核心件);debug 族其余(crn 0-2:DBGIDR/DBGBCR/BVR/
         * WVR/WCR/OSLAR...)+PMU 族(op0=3,op1=3,crn 9/14,TPM 陷阱)
         * =RAZ/WI(读 0/写弃:Linux hw_breakpoint 探测见 0=优雅
         * 降级,计数互漏根除)。 */
        if (op0 == 2u && op1 == 0u && crn == 1u && crm == 3u && op2 == 2u) {
            if ((iss & 1u) || rt == 31u) {   /* 读:还本 VM 槽(xzr 不回) */
                if (rt != 31u)
                    v->gprs[rt] = v->mdscr;
            } else {
                v->mdscr = v->gprs[rt];      /* 写:存本 VM 槽 */
            }
            v->pc += 4UL;
            resume_or_preempt(v);
        }
        if ((op0 == 2u && op1 == 0u && crn <= 2u) ||
            (op0 == 3u && op1 == 3u && (crn == 9u || crn == 14u))) {
            if ((iss & 1u) && rt != 31u)
                v->gprs[rt] = 0UL;           /* RAZ:读 0(WI 写弃) */
            v->pc += 4UL;
            resume_or_preempt(v);
        }
        guest_fault(v, esr);                 /* 其余系统寄存器:默认拒绝 */
    } else if (ec == 0x24) {               /* data abort(EL1):仿真窗口分发 */
        mmio_dispatch(v, esr);             /* 命中 EMULATED 设备则仿真续跑,
                                           * 无窗/无现场回落 guest_fault */
    } else if (ec == 0x01 && (imm & 1) == 0) {  /* HCR.TWI:guest WFI -> 等待态 */
        /* 站22A([54] 定谳):fg 行缓冲的泊位冲刷——客停泊且 RX 环
         * 空才冲半行,键回显的即时性由此保(µs 级,人类敲键 100ms
         * 尺度无感)。爆发感知:环内还有字节=客即刻会醒续收(逐字节
         * 中断消费,每注一发弹一个),此刻冲=把回显拆成碎片逐泊吐出
         * (probe-tear 第二轮实测定谳)——跳过,行完整后才见天日;
         * 泵再注环空后的最后一泊冲全行。bg 半行同律(原死亡/切换
         * 冲刷语义不变,此为常态路径)。空 tx 时 emit 首行早退。
         *
         * 站22A 第二雷(门禁 A 轮自抓):per-VM tx 缓冲被该 VM 全部
         * vCPU 共享——本帧泊位冲刷会把**同 VM 他帧在途行**拦腰切
         * 断("[vm0] rtos> c[vm0] pu1: online"实测定谳:vcpu0 的提示
         * 符半行 + vcpu1(cpu3 泵)正追加的 online 行,冲刷落在
         * 'c' 与 'pu1' 之间)。全 VM 静默判:任一同侪 vCPU 未泊位
         * =可能有行在途,跳过;同侪都泊位才冲(单帧 VM 无同侪,
         * 恒冲——vm1/vm2-fg 交互面零变化)。 */
        struct eshine_vm *pvm = vm_of_vcpu(v);
        u32 vn = vm_vcpu_count(pvm->vmid);
        u32 vi;
        int peers_parked = 1;

        for (vi = 0; vi < vn; vi++) {
            if (&pvm->vcpus[vi] != v && pvm->vcpus[vi].wait_state != 1UL) {
                peers_parked = 0;   /* 同侪在跑:行或在途,让它的 '\n' 收口 */
                break;
            }
        }
        /* 站23C:泊位冲刷延迟化——不再即时冲,改武装 20ms 死线
         * (flush_at=CNTPOCT+延迟),泵入口扫描(vuart_flush_sweep)
         * 到期才冲。chardev 分块缝(µs-ms 级)被吸收:块内续收的
         * 行完整后才见天日,回显不再碎([54] 残余收编);人类敲键
         * 间隙 100ms+ → 照常 ≤20ms 见回显。时间基=vm0 RTOS 100Hz
         * tick 恒拍 cpu0 泵,无事件死区(boot 序 vm0 恒最先起)。
         * CNTPOCT 自由读=零宿主时钟手术(站22 残余的可行性关键)。 */
        if (peers_parked && !vuart_rx_has_data(&pvm->vu)) {
            u64 now, freq;

            /* 评审修真:武装入 console_lock——与泵扫描的判-清
             * (vm.c)互斥;无锁清零可吞并发武装(读旧死线→他核
             * 泊位武装→清零),该行的吸收窗失效即 [54] 复发面。
             * 站24 修真:只首次武装(flush_at==0),已武装(含已
             * 过期)不推迟——10ms 拍客(RTOS tick)与 20ms 窗恰
             * 2:1 相位锁死:每泊重臂+20ms,fire 必撞下下拍唤醒
             * 边界(未泊跳过),再泊又先重臂后扫描=永久推迟(尾
             * 字符滞环,[19] 5/5 复现;站二十三一直绿全靠心跳打
             * 印首字符顺手冲掉滞留字节=A 块静默心跳后活锁裸露)。
             * 过期死线保留,下一泊定瞬间 fire 即收。 */
            console_lock();
            if (pvm->vu.flush_at == 0) {
                __asm__ volatile("mrs %0, cntpct_el0" : "=r"(now));
                __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
                pvm->vu.flush_at = now + freq / 50UL;   /* 20ms */
            }
            console_unlock();
        }
        v->exit_reason = VCPU_WAIT;
        vcpu_exit();
    } else {                                /* guest fault:诊断 -> VM 隔离死 */
        guest_fault(v, esr);
    }
}
