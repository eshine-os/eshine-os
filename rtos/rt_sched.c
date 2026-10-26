/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "rtos.h"

/* 位图调度器:32 级优先级,bit p = 就绪位;摘最低位(ctz)= 最高
 * 优先(0 最高)。就绪队列每级一条 FIFO 尾链(非环,摘除简单)。
 * 被 RR 轮转/抢占切走的任务排回本级队尾。 */

static rt_task_t *rdy_head[RT_NPRIO];
static rt_task_t *rdy_tail[RT_NPRIO];
static u32 rdy_map;
static int need_switch;

/* DAIF 短临界段(第十七站):rdy 队是任务上下文与 ISR 出口共用的底座。
 * 唯一交错源是 tick 抢切——两个写者(任务侧入队/handoff 与 ISR 侧醒
 * 睡客/RR 重排)跨指令交错,尾链/位图会把任务挤下链永久丢失(站十六
 * 潜伏竞态,本站收敛)。ISR 语境自带 I=1,save/restore 原样归还。 */
u64 rt_daif_save(void)
{
    u64 d;

    __asm__ volatile("mrs %0, daif\n\t"
                     "msr daifset, #2" : "=r"(d) :: "memory");
    return d;
}

void rt_daif_load(u64 d)
{
    __asm__ volatile("msr daif, %0" :: "r"(d) : "memory");
}

static void rdy_enqueue(rt_task_t *t)
{
    u32 p = t->prio;
    u64 d = rt_daif_save();

    t->next = NULL;
    if (!rdy_head[p])
        rdy_head[p] = t;
    else
        rdy_tail[p]->next = t;
    rdy_tail[p] = t;
    rdy_map |= 1u << p;
    rt_daif_load(d);
}

void rt_ready_push(rt_task_t *t)
{
    t->state = RT_READY;
    rdy_enqueue(t);
}

rt_task_t *rt_pick_ready(void)
{
    u32 p;
    rt_task_t *t;
    u64 d;

    if (!rdy_map)
        return NULL;
    p = (u32)__builtin_ctz(rdy_map);
    d = rt_daif_save();
    t = rdy_head[p];
    rdy_head[p] = t->next;
    t->next = NULL;
    if (!rdy_head[p]) {
        rdy_tail[p] = NULL;
        rdy_map &= ~(1u << p);
    }
    rt_daif_load(d);
    return t;
}

/* 就绪最高位是否比 cur 高(数字更小即优先级更高) */
static int higher_ready_than(rt_task_t *cur)
{
    if (!rdy_map)
        return 0;
    return (u32)__builtin_ctz(rdy_map) < cur->prio;
}

void rt_sched(void)
{
    rt_task_t *next;

    for (;;) {
        next = rt_pick_ready();
        if (!next) {
            /* 空转语境:无一就绪——WFI 让位(泵判 WAIT)。不 WFI 的话
             * 壳 BLOCK 后会热自旋;醒来重扫,IRQ 出口可能已直接切走
             * 我们(冻结在本帧,无需回头)。 */
            __asm__ volatile("wfi");
            continue;
        }
        if (next == rt_cur) {       /* 摘了自己又选回自己 */
            rt_ready_push(next);
            return;
        }
        next->state = RT_RUN;
        if (rt_cur->state == RT_RUN)/* 被切走的排回队尾(RR 语义) */
            rt_ready_push(rt_cur);
        rt_switch_to(next);         /* noreturn:醒来已是新语境 */
        return;
    }
}

void rt_wake_rx(void)
{
    rt_task_wake_all(RT_RX_BIT);    /* 扫池唤醒:命中等待位的 BLOCK->READY */
}

void rt_tick_bookkeep(void)
{
    rt_task_t *t;

    need_switch = 0;
    rt_task_wake_sleepers();        /* 到期睡客 -> READY */
    t = rt_cur;
    if (t->state == RT_RUN && t->slice_left > 0) {
        t->slice_left--;
        if (t->slice_left == 0) {
            t->slice_left = RT_SLICE;
            if (rdy_head[t->prio]) { /* 同级有人:轮转让位 */
                rt_ready_push(t);
                t->state = RT_READY;
                need_switch = 1;
            }
        }
    }
    if (higher_ready_than(t))
        need_switch = 1;
}

int rt_need_switch(void)
{
    return need_switch;
}
