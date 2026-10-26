/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "rtos.h"
#include "miniuart.h"
#include "gic.h"
#include "timer.h"
#include "virtio.h"
#include "fdt.h"
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

rt_task_t *rt_irq_dispatch(void)
{
    u64 iar = gic_claim();

    if (iar == GUEST_INTID_VTIMER) {
        u64 period = vtimer_freq() / RT_HZ;
        u64 cval;

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
    if (iar == GUEST_INTID_VIRTIO) {
        virtio_isr();
    }
    if (iar != GUEST_INTID_SPURIOUS)
        gic_eoir(iar);
    /* 出口判决:要切(轮转/更高优先就绪)或当前已非 RUN(阻塞/睡/死)
     * 则摘队头;被抢占的原任务排回队尾。 */
    if (rt_need_switch() || rt_cur->state != RT_RUN) {
        rt_task_t *next = rt_pick_ready();

        if (next) {
            next->state = RT_RUN;
            if (rt_cur->state == RT_RUN)
                rt_ready_push(rt_cur);
            return next;
        }
    }
    return NULL;
}

static void heartbeat_task(void *arg)
{
    (void)arg;
    for (;;) {
        rt_sleep_ticks(50);          /* 0.5s 一行:任务级交错的可见判据 */
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

    rt_idle_loop();                   /* noreturn:idle 即引导语境 */
}
