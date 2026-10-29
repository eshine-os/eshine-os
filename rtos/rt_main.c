/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "rtos.h"
#include "miniuart.h"
#include "gic.h"
#include "timer.h"
#include "virtio.h"
#include "fdt.h"
#include "hvc.h"
#include "kernel/include/version.h"

/* eshine-rtos bring-up:与裸机客同一套设备自证链(同一 ABI 头),
 * 再起调度世界。IRQ 解掩(gic_guest_enable)在 RTOS 世界就绪之后——
 * 早到的 RX 中断也要有 rt_cur 可依。CPACR 由 guest 自设(站十五
 * 哲学:FPEN 归 guest,宿主不碰)。 */

static void vtimer_arm_periodic(void)
{
    u64 period = vtimer_freq() / RT_HZ;

    __asm__ volatile("msr cntv_cval_el0, %0"
                     :: "r"(vtimer_now() + period));
    __asm__ volatile("msr cntv_ctl_el0, %0" :: "r"(1UL));
}

/* 站二十:次核上线(PSCI CPU_ON,SMC conduit 走 EL2 EC 0x17 转发,
 * 19D 机制)。x1=客视角 MPIDR(VMPIDR=0x80000000|1),x2=入口,
 * x3=context_id=核号(次核入口 asm 拿它写 TPIDR_EL1)。 */
static u64 psci_rtos_cpu_on(u64 target, void *entry, u64 ctx)
{
    register u64 x0 __asm__("x0") = 0xc4000003UL;
    register u64 x1 __asm__("x1") = target;
    register u64 x2 __asm__("x2") = (u64)entry;
    register u64 x3 __asm__("x3") = ctx;

    __asm__ volatile("hvc #0"      /* 站二十:SMC 会被 QEMU 机器级
                                    * PSCI 截获(19D 实证:TCG conduit
                                    * 优先),hvc #0 才落我们的
                                    * EC 0x16 imm=0 PSCI 转发分支 */
                     : "+r"(x0)
                     : "r"(x1), "r"(x2), "r"(x3)
                     : "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11",
                       "x12", "x13", "x14", "x15", "x16", "x17", "memory");
    return x0;
}

/* 站二十:次核世界(EL2 置帧后 poke,本核自此活)。引导语境就地
 * 转正 idle1(池外伪任务,栈=链接器 .cpu1stack,入口 asm 已驻);
 * GIC 本核 ICV 面 + 自臂 vtimer + FPEN 自设,与主核引导同律。 */
void rtos_secondary_main(u32 cpu)
{
    rt_task_t *idle = rt_idle1_task();

    (void)cpu;                       /* tpidr 已在入口 asm 登记为 1 */
    rt_cur = idle;
    idle->entry = (void (*)(void *))rt_idle_loop;
    idle->prio = RT_NPRIO - 1;
    idle->state = RT_RUN;
    idle->cpu = 1;
    idle->name[0] = 'c'; idle->name[1] = 'p'; idle->name[2] = 'u';
    idle->name[3] = '1'; idle->name[4] = ' '; idle->name[5] = 'i';
    idle->name[6] = 'd'; idle->name[7] = 'l'; idle->name[8] = 'e';
    idle->name[9] = '\0';
    idle->slice_left = RT_SLICE;
    __asm__ volatile("msr cpacr_el1, %0" :: "r"(3UL << 20));
    __asm__ volatile("isb");
    gic_secondary_frame_init(1);    /* 站二十:帧 1 GICR 面(WAKER/
                                      * SGI0+PPI27 使能——ipidemo rx=0
                                      * 定谳:次核不开自家帧,SGI 取走
                                      * 被 isenabler 门滤掉) */
    gic_guest_enable();              /* 本核 ICV 面(PMR/IGRPEN/解掩) */
    vtimer_arm_periodic();           /* 本核 tick 起拍:RT_HZ 同律 */
    rt_puts_u64("cpu1: online (rtos psci) gicr1_en=",
                (u64)vgicr_read(0x20000UL + GICR_SGI + GICR_ISENABLER0),
                "\n");              /* 站二十:帧 1 使能回读自证
                                      * (SGI0|PPI27 应见 0x08000001) */
    rt_idle_loop();                  /* noreturn:idle1 即本核引导语境 */
}

rt_task_t *rt_irq_dispatch(void)
{
    u64 iar = gic_claim();

    if (iar == GUEST_INTID_VTIMER) {
        u64 period = vtimer_freq() / RT_HZ;
        u64 cval;

        /* 站二十:全局 tick 记账单写者化(核 0 独记,核 1 只读
         * ——宿主三核一钟同律,防双计互踩)。本核重臂照旧。 */
        if (rt_cpu_id() == 0)
            rt_ticks++;
        __asm__ volatile("mrs %0, cntv_cval_el0" : "=r"(cval));
        /* 周期重臂:上拍到期值为基(+period),零漂移。泵的每拍扫描
         * 醒来注入路径可能已把 ctl 熄火(站七形态),一并点回。 */
        __asm__ volatile("msr cntv_cval_el0, %0" :: "r"(cval + period));
        __asm__ volatile("msr cntv_ctl_el0, %0" :: "r"(1UL));
        rt_tick_bookkeep();
    }
    if (iar == GUEST_INTID_UART) {
        rt_uart_drain_isr();
        rt_wake_rx();
    }
    if (iar == GUEST_INTID_IPI) {
        rt_resched_ipi();            /* 站二十:跨核唤醒落本核——
                                      * 重调度判 + 收拍计数(demo 面) */
    }
    if (iar == GUEST_INTID_VIRTIO) {
        virtio_isr();
    }
    if (iar != GUEST_INTID_SPURIOUS)
        gic_eoir(iar);
    /* 出口判决:要切(轮转/更高优先就绪)或当前已非 RUN(阻塞/睡/死)
     * 则摘队头;被抢占的原任务排回队尾。
     * 站19 [41] 第二次定谳:醒客一律回流令牌(rt_ready_requeue,不碰
     * 状态)。醒客帧有两种冻结位——wfi 循环体内(被本出口从中切走的
     * 驻留者)与 .Lcoop_ret(正常让位)。前者醒来落在循环里,靠自拣
     * 兑付令牌才能 *返回调用者*(否则醒令被吞,state 已被改成 RUN,
     * wake_sleepers/wake_all 从此永久跳过——旧代码全靠僵尸条目碰巧
     * 在队才没必现,即 [41] 闪断的真身);后者醒来直接回调用者,残
     * 留令牌由各阻塞位点的 rdy_remove 清除,不残留不占位。 */
    if (rt_need_switch() || rt_cur->state != RT_RUN) {
        rt_task_t *next = rt_pick_ready();

        if (next) {
            rt_ready_requeue(next);
            next->state = RT_RUN;
            if (next != rt_cur && rt_cur->state == RT_RUN)
                rt_preempt_requeue(rt_cur);  /* 被抢占者恒单份回队 */
            return next;
        }
    }
    return NULL;
}

static void heartbeat_task(void *arg)
{
    (void)arg;
    for (;;) {
        rt_sleep_ticks(50);          /* 0.5s 一拍:任务级交错的可见判据 */
        if (hvc_fg())                /* 站20:前台门控(HVC#5 只读问询)
                                      * ——vmrun 后台期静默,宿主/他客
                                      * 串口免骚扰(用户令);vmconsole 0
                                      * 前台化即恢复可见 */
            rt_puts_u64("rtos: alive tick=", rt_ticks, "\n");
    }
}

void rt_idle_loop(void)
{
    for (;;)
        rt_sched();                   /* 就绪者上场;空则内含 WFI 让位 */
}

void guest_main(void)
{
    rt_task_t *idle = rt_idle_task();

    __asm__ volatile("msr tpidr_el1, %0" :: "r"(0UL));  /* 站二十:本核
                                              * 索引登记(簿记宏/rt_cur 宏;
                                              * 次核入口自写 1) */
    uart_pl011_init();                /* 站13:身份证+开机(先开机才有输出) */
    rt_puts("eshine-rtos " ESHINE_VERSION_STR " @EL1 via stage-2 (vuart @trap)\n");
    /* RTOS 世界先立(引导语境转正 idle):任何潜在 IRQ 到达前,rt_cur 必须有效 */
    rt_cur = idle;
    idle->entry = (void (*)(void *))rt_idle_loop;   /* 非 NULL:ps 可见 */
    idle->prio = RT_NPRIO - 1;
    idle->state = RT_RUN;
    idle->name[0] = 'i'; idle->name[1] = 'd'; idle->name[2] = 'l';
    idle->name[3] = 'e'; idle->name[4] = '\0';
    idle->slice_left = RT_SLICE;
    gic_dist_init();                  /* 站12:vGIC 真启动式 */
    virtio_blk_init();                /* 站10:probe+DRIVER_OK */
    fdt_check();                      /* 站11:三节点自查 */
    __asm__ volatile("msr cpacr_el1, %0" :: "r"(3UL << 20));  /* FPEN 自设 */
    __asm__ volatile("isb");

    /* 任务世界:壳与心跳上池 */
    rt_ticks = 0;
    rt_task_create("shell", rt_shell_task, NULL, 5);
    rt_task_create("heartbeat", heartbeat_task, NULL, 10);

    vtimer_arm_periodic();            /* 100Hz tick 起拍 */
    gic_guest_enable();              /* 解 IRQ:此后 tick/RX 走真中断 */

    rt_puts("uart irq on\n");         /* 与裸机同 needle(RXIM 已使能) */
    rt_puts("virtio blk on\n");
    rt_puts("rtos: up (eshine-rtos " ESHINE_VERSION_STR ") 32-prio bitmap, tick 100Hz\n");

    /* 站二十:次核上线(世界已立:调度器/队/锁/SGI 机制全就绪才可)。
     * EL2 置帧+poke,cpu3 专泵扫到即跑;拒绝只报不炸(单核退化仍活)。 */
    if (psci_rtos_cpu_on(0x80000001UL, rtos_secondary_entry, 1) != 0)
        rt_puts("smp: cpu1 cpu_on denied\n");

    rt_idle_loop();                   /* noreturn:idle 即引导语境 */
}
