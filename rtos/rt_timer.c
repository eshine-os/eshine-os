/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "rtos.h"
#include "timer.h"         /* 站40:cntvct/cntfrq 时间面 */

/* 软定时器(站二十二B):站二十一超时扫描的自然延伸——绝对截止账本
 * 与核 0 单写者环同源,只是对象从"任务睡/挂等到期"扩到"任意回调
 * 到点"。哲学与任务池同:静态指针登记,无堆无递归。
 *
 * ==== 回调语境契约(评审核 blocker/major 三案折入,与 参考RTOS 反转
 *      警示:参考RTOS ktimer 回调跑在专用 timer 任务语境、可阻塞——
 *      本设计语境反转,直译 参考RTOS 用法即灾难) ====
 * 回调跑在核 0 tick-ISR(I=1),被 tick 打断的 rt_cur 不属于回调:
 * - 允许:rt_puts / rt_puts_u64(puts_lk 持锁即屏本核,ISR 不可能在
 *   持锁任务上再入——自旋锁契约);rt_ready_push(限唤醒 SLEEP 态,
 *   抢占决策由 IRQ 出口 need_switch 段延后);rt_timer_stop(含
 *   自停,幂等)。
 * - 禁止:一切会挂起 rt_cur 的调用(rt_sleep_ticks/sem/mutex/queue
 *   之 take/event_wait)——rt_cur 是被打断的任务,挂起它会在 ISR
 *   帧内 rt_sched 换帧,gic_eoir 永不执行,INTID_VTIMER 永久
 *   active,核 0 中断面整体死;rt_sem_give/rt_event_set 的尾段
 *   `w->prio < rt_cur->prio 则 rt_sched()` 同病(等待者常比被打断
 *   者优先,触发几乎必中);rt_timer_start(入池扫描与回调迭代同环,
 *   重入次序未定义)。
 *
 * ==== 扫描契约(评审核 blocker:重臂/回调次序照 spec 字面实现会产
 *      每 tick 风暴,此处钉死) ====
 * 按池槽序扫描;到期先做重臂/注销决策、后进回调:
 * - 周期:deadline += period(零漂移,vtimer 同律;迟到按每 tick
 *   一发自然追账);- 单发:deadline=0 且注销,再进回调(结构体
 *   寿命须活过回调本体——单发回调执行时 deadline 已清 0)。
 * 回调内 stop(自己)只会把已重臂的 deadline 清零,次序自洽。
 *
 * ==== 登记契约(评审核:核语境/生命周期/可观测) ====
 * - start/stop 仅核 0 语境调用(与单写者扫描同核序贯,免窗免锁;
 *   跨核支持需池自旋锁,记残余);同指针重复 start=改期不双登记;
 *   stop 幂等(置 0+注销)。
 * - rt_timer_t 须常驻存储(静态/全局):武装期退场=tp[] 悬垂,
 *   扫描跳垃圾 fn。
 * - start 返 0 成功/-1 池满(评审核:void 无返码="礼貌拒绝"不可
 *   观测);ticks==0 拒 -1(deadline=rt_ticks+0 下一拍即发,语义
 *   含混,不如拒)。 */

static rt_timer_t *tp[RT_TMAX];

void rt_timer_init(rt_timer_t *t, void (*fn)(void *), void *arg)
{
    t->deadline = 0;
    t->period = 0;
    t->fn = fn;
    t->arg = arg;
}

int rt_timer_start(rt_timer_t *t, u64 ticks, int periodic)
{
    unsigned int i;

    if (!t->fn || ticks == 0)
        return -1;
    t->deadline = rt_ticks + ticks;
    t->period = periodic ? ticks : 0;
    for (i = 0; i < RT_TMAX; i++) {
        if (tp[i] == t)
            return 0;                 /* 已在池:改期即得(重入=改期) */
    }
    for (i = 0; i < RT_TMAX; i++) {
        if (!tp[i]) {
            tp[i] = t;
            return 0;
        }
    }
    return -1;                        /* 池满:可观测拒绝 */
}

void rt_timer_stop(rt_timer_t *t)
{
    unsigned int i;

    t->deadline = 0;
    t->period = 0;
    for (i = 0; i < RT_TMAX; i++) {
        if (tp[i] == t)
            tp[i] = NULL;
    }
}

/* 核 0 单写者:tick-ISR 语境。到期先做重臂/注销决策再进回调——
 * 回调内 stop(自己)只会把已重臂的 deadline 清零,次序自洽。 */
void rt_timer_tick(void)
{
    unsigned int i;

    /* 站28 vtimer 专站取证守卫:tp[] 曾被池形数据成块错投
     * (far=0x340/0x345 双案,rt_timer_tick 遍历即崩)。合法定时器
     * 指针必为 pool 槽对齐;坏值入表后 ≤1 拍(10ms)自报,
     * 带 \n 即时冲刷——不依赖外仪,海森伯免疫 */
    static u8 guard_hit;

    if (!guard_hit) {
        for (i = 0; i < RT_TMAX; i++) {
            u64 v = (u64)tp[i];

            u64 base = (u64)rt_pool_base();

            if (v != 0 &&
                (v < base ||
                 v >= base + (u64)RT_POOL_MAX * sizeof(rt_task_t) ||
                 (v - base) % sizeof(rt_task_t) != 0)) {
                rt_puts_u64("tp-corrupt tick=", rt_ticks, " idx=");
                rt_puts_u64("", i, " val=");
                rt_puts_hex("", v, " cur=");
                rt_puts(rt_cur->name);
                rt_puts("\n");
                guard_hit = 1;
                break;
            }
        }
    }
    for (i = 0; i < RT_TMAX; i++) {
        rt_timer_t *t = tp[i];

        if (!t || !t->deadline)
            continue;
        if ((long long)rt_ticks - (long long)t->deadline >= 0) {
            if (t->period)
                t->deadline += t->period;   /* 周期:零漂移重臂 */
            else {
                t->deadline = 0;            /* 单发:发即注销(回调执行时
                                              * deadline 已 0——结构体须
                                              * 活过回调本体) */
                tp[i] = NULL;
            }
            t->fn(t->arg);
        }
    }
}

/* ---- 站四十:时间面(clock_gettime/gettimeofday 的 ns 源) ----
 * monotonic=cntvct/freq(站 21 时基直读,无陷阱);realtime=
 * BOOT_EPOCH(2026-11-01 常量;RTC/drvfw=后弧)+mono。freq 一次
 * 读存静态(常数面,CNTFRQ 不可变)。 */
static u64 time_freq;

static u64 time_hz(void)
{
    if (!time_freq)
        time_freq = vtimer_freq();
    return time_freq;
}

u64 rt_time_mono_ns(void)
{
    u64 hz = time_hz();

    return hz ? vtimer_now() * 1000000000ULL / hz : 0;
}

u64 rt_time_real_ns(void)
{
    return RT_BOOT_EPOCH_NS + rt_time_mono_ns();
}
