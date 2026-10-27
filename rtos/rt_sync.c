/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "rtos.h"

/* 同步原语(第十七站):每对象 FIFO 等待队列,参考RTOS 公开设计为基准的
 * 最小形态。契约:take 是重检环(醒后自己复核条件),give 让渡资源并
 * 唤醒队头——"唤醒=允许重试,非授予";被醒者优先级更高则立即让位。
 *
 * 临界段:count/owner/wq 的检查-修改与队列链接都在 DAIF 短临界段内
 * (rt_daif_save/rt_daif_load,ISR 语境 restore 不泄掩码)——tick 抢
 * 切能把另一个任务插进两条指令之间:check+dec 不封窗会被双取到负数;
 * 队列双写者交错会挤丢任务。rt_sched()/WFI 永不进临界段。 */

static void wq_push(rt_task_t **head, rt_task_t **tail, rt_task_t *t)
{
    t->wq_next = NULL;
    if (!*head)
        *head = t;
    else
        (*tail)->wq_next = t;
    *tail = t;
}

static rt_task_t *wq_pop(rt_task_t **head, rt_task_t **tail)
{
    rt_task_t *t = *head;

    if (!t)
        return NULL;
    *head = t->wq_next;
    if (!*head)
        *tail = NULL;
    t->wq_next = NULL;
    return t;
}

/* 挂等:BLOCK 入对象队列再让位;醒来在调用点后继续(take 重检环)。
 * 站19:挂等前清自拣令牌——否则 rt_sched 摘到自己即"醒",重检环被
 * 令牌无限短路,每次假醒再 wq_push 会把等待队列推成自环。
 * 站19B 起导出:rt_ipc.c 的队列收发同机器(事件组自带注册表不用此) */
void rt_pend(rt_task_t **head, rt_task_t **tail)
{
    u64 d = rt_daif_save();

    rt_cur->state = RT_BLOCK;
    wq_push(head, tail, rt_cur);
    rdy_remove(rt_cur);
    rt_daif_load(d);
    rt_sched();
}

/* 唤醒队头;更高优先就位即让(统一帧:rt_sched 调用点即续跑点)。
 * 站19B 起导出:rt_ipc.c 队列同机器 */
void rt_handoff(rt_task_t **head, rt_task_t **tail)
{
    rt_task_t *w;
    u64 d = rt_daif_save();

    w = wq_pop(head, tail);
    rt_daif_load(d);
    if (w) {
        rt_ready_push(w);
        if (w->prio < rt_cur->prio)
            rt_sched();
    }
}

void rt_sem_init(rt_sem_t *s, long init)
{
    s->count = init;
    s->wq_head = NULL;
    s->wq_tail = NULL;
}

long rt_sem_take(rt_sem_t *s)
{
    for (;;) {
        int got = 0;
        u64 d = rt_daif_save();

        if (s->count > 0) {
            s->count--;
            got = 1;
        }
        rt_daif_load(d);
        if (got)
            return 0;
        rt_pend(&s->wq_head, &s->wq_tail);
    }
}

long rt_sem_give(rt_sem_t *s)
{
    u64 d = rt_daif_save();

    s->count++;                     /* 无条件记数;被醒者重检环里自己取 */
    rt_daif_load(d);
    rt_handoff(&s->wq_head, &s->wq_tail);
    return 0;
}

void rt_mutex_init(rt_mutex_t *m)
{
    m->owner = NULL;
    m->nest = 0;
    m->wq_head = NULL;
    m->wq_tail = NULL;
}

long rt_mutex_take(rt_mutex_t *m)
{
    for (;;) {
        int got = 0;
        u64 d = rt_daif_save();

        if (m->owner == NULL) {
            m->owner = rt_cur;
            m->nest = 1;
            got = 1;
        } else if (m->owner == rt_cur) {   /* 递归:同 owner 重入计数 */
            m->nest++;
            got = 1;
        }
        rt_daif_load(d);
        if (got)
            return 0;
        /* 站十八B:单级优先级继承——挂等前把 owner 提到我的优先级
         * (取最强,重复 boost 不降级);owner 在就绪链上需摘出重挂
         * (位图位随 prio 变)。锁链不传递(参考RTOS 裁剪,记残余)。 */
        if (m->owner->prio > rt_cur->prio) {
            rt_task_t *o = m->owner;
            u32 np = rt_cur->prio;

            if (o->state == RT_READY)
                rdy_remove(o);
            o->prio = np;
            if (o->state == RT_READY)
                rt_ready_push(o);
        }
        rt_pend(&m->wq_head, &m->wq_tail);
    }
}

long rt_mutex_give(rt_mutex_t *m)
{
    rt_task_t *w;
    u64 d = rt_daif_save();

    if (m->owner != rt_cur) {
        rt_daif_load(d);
        return -1;                   /* 非拥有者:礼貌拒绝(判据用) */
    }
    if (--m->nest > 0) {
        rt_daif_load(d);
        return 0;
    }
    /* 站十八B:还继承——先回 base,再按剩余挂等者取最强重升(多等待
     * 者公平);本人 READY(被切走的 RR 语境)则摘出重挂。 */
    {
        rt_task_t *w = m->wq_head;
        u32 np = rt_cur->base_prio;
        u64 d2 = rt_daif_save();

        while (w) {
            if (w->prio < np)
                np = w->prio;
            w = w->wq_next;
        }
        if (np != rt_cur->prio) {
            if (rt_cur->state == RT_READY)
                rdy_remove(rt_cur);
            rt_cur->prio = np;
            if (rt_cur->state == RT_READY)
                rt_ready_push(rt_cur);
        }
        rt_daif_load(d2);
    }
    m->owner = NULL;                 /* 归零让渡;被醒者重检环里重占 */
    w = wq_pop(&m->wq_head, &m->wq_tail);
    rt_daif_load(d);
    if (w) {
        rt_ready_push(w);
        if (w->prio < rt_cur->prio)
            rt_sched();
    }
    return 0;
}
