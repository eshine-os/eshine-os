/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "rtos.h"

/* IPC(第十九站B):事件组 + 消息队列,站十七等待队列机器的延伸。
 * 契约同源:挂等=BLOCK 让位,唤醒=允许重试非授予(醒者自复核)。
 *
 * 事件组不走通用 wq 链——醒谁要看条件(AND/OR),任务链上挂不下
 * want/mode,故用每事件一张条件注册表(池 ≤8,槽即容量):wait 注册
 * 后挂起,set 扫表只摘满足者。残窗同站十七:复核-挂等之间被 tick
 * 插进 set 理论上可丢醒,单核因果(demo 里 setter 只有 waiter 让位才
 * 跑)使其不可达,记残余。
 * 消息队列用站十七原样机器:满发挂 tx 队、空收取 rx 队,唤醒=允许
 * 重试。 */

void rt_event_init(rt_event_t *e)
{
    unsigned int i;

    e->flags = 0;
    e->nw = 0;
    for (i = 0; i < RT_POOL_MAX; i++) {
        e->wait[i].t = NULL;
        e->wait[i].want = 0;
        e->wait[i].and_mode = 0;
    }
}

int rt_event_wait(rt_event_t *e, u32 want, int and_mode, u32 *got)
{
    for (;;) {
        int met = 0;
        u32 f = 0;
        u64 d = rt_daif_save();

        if (want == 0) {                /* 空条件:永远不满足,拒收 */
            rt_daif_load(d);
            return -1;
        }
        f = e->flags & want;
        met = and_mode ? (f == want) : (f != 0u);
        if (met) {
            e->flags &= ~f;             /* 命中即耗(参考RTOS 公开语义) */
            rt_daif_load(d);
            if (got)
                *got = f;
            return 0;
        }
        /* 未满足:注册条件后让位(同 rt_pend 的挂等面,只是队是注册表) */
        e->wait[e->nw].t = rt_cur;
        e->wait[e->nw].want = want;
        e->wait[e->nw].and_mode = and_mode;
        e->nw++;
        rt_cur->state = RT_BLOCK;
        rdy_remove(rt_cur);             /* 站19:清自拣残令牌再挂 */
        rt_daif_load(d);
        rt_sched();
        /* 被摇醒(允许重试):摘注册,回环复核——命中即耗,未中再挂 */
        d = rt_daif_save();
        {
            unsigned int i;

            for (i = 0; i < e->nw; i++) {
                if (e->wait[i].t == rt_cur) {
                    e->wait[i] = e->wait[e->nw - 1];
                    e->nw--;
                    break;
                }
            }
        }
        rt_daif_load(d);
    }
}

void rt_event_set(rt_event_t *e, u32 bits)
{
    unsigned int i;
    int woke_higher = 0;
    u32 best = 0xffffffffu;
    u64 d = rt_daif_save();

    e->flags |= bits;
    for (i = 0; i < e->nw; ) {
        struct rt_event_waiter *w = &e->wait[i];
        u32 f = e->flags & w->want;
        int met = w->and_mode ? (f == w->want) : (f != 0u);

        if (met) {
            /* 只醒不耗:位由醒者复核环自耗(先到先得,参考RTOS 语义)。
             * setter 代耗会让复核扑空,醒者永远重挂(手验抓出)。 */
            w->t->state = RT_READY;
            rt_ready_push(w->t);
            if (w->t->prio < rt_cur->prio)
                woke_higher = 1;
            if (w->t->prio < best)
                best = w->t->prio;
            e->wait[i] = e->wait[e->nw - 1];
            e->nw--;
        } else {
            i++;
        }
    }
    rt_daif_load(d);
    if (woke_higher && best < rt_cur->prio)
        rt_sched();                     /* 站17 律:更高就位即让 */
}

void rt_queue_init(rt_queue_t *q, u32 cap)
{
    q->head = 0;
    q->count = 0;
    q->cap = cap == 0 ? 1u : (cap > RT_QCAP ? RT_QCAP : cap);
    q->rx_h = q->rx_t = NULL;
    q->tx_h = q->tx_t = NULL;
}

/* 站21A:队列超时族(单窗形态同 sem/mutex,评审核 gap 修正)。
 * 满发挂 tx 队/空收取 rx 队;wake 语义不变:send 成功摇 rx,recv
 * 成功摇 tx。no-wait(0)单次复核即判负。 */
int rt_queue_send_to(rt_queue_t *q, u64 msg, u64 ticks)
{
    u64 deadline = (ticks == RT_WAIT_FOREVER) ? 0 : rt_ticks + ticks;

    for (;;) {
        int got = 0;
        u64 d = rt_daif_save();

        if (q->count < q->cap) {
            q->buf[(q->head + q->count) % q->cap] = msg;
            q->count++;
            got = 1;
        } else if (ticks != 0) {
            rt_cur->state = RT_BLOCK;
            rt_cur->timed_out = 0;
            rt_cur->pend_deadline = deadline;
            rt_cur->pend_qh = &q->tx_h;
            rt_cur->pend_qt = &q->tx_t;
            rt_cur->pend_owner = NULL;
            rt_wq_push(&q->tx_h, &q->tx_t, rt_cur);
            rdy_remove(rt_cur);
        }
        rt_daif_load(d);
        if (got) {
            rt_handoff(&q->rx_h, &q->rx_t);   /* 有货:摇空取队头 */
            rt_pend_disarm(rt_cur);           /* 站21 评审:成功出口缴械 */
            return 0;
        }
        if (ticks == 0)
            return RT_TIMEOUT;
        rt_sched();
        if (rt_cur->timed_out) {
            rt_pend_disarm(rt_cur);
            return RT_TIMEOUT;
        }
    }
}

int rt_queue_recv_to(rt_queue_t *q, u64 *msg, u64 ticks)
{
    u64 deadline = (ticks == RT_WAIT_FOREVER) ? 0 : rt_ticks + ticks;

    for (;;) {
        int got = 0;
        u64 d = rt_daif_save();

        if (q->count > 0) {
            if (msg)
                *msg = q->buf[q->head];
            q->head = (q->head + 1) % q->cap;
            q->count--;
            got = 1;
        } else if (ticks != 0) {
            rt_cur->state = RT_BLOCK;
            rt_cur->timed_out = 0;
            rt_cur->pend_deadline = deadline;
            rt_cur->pend_qh = &q->rx_h;
            rt_cur->pend_qt = &q->rx_t;
            rt_cur->pend_owner = NULL;
            rt_wq_push(&q->rx_h, &q->rx_t, rt_cur);
            rdy_remove(rt_cur);
        }
        rt_daif_load(d);
        if (got) {
            rt_handoff(&q->tx_h, &q->tx_t);   /* 有空位:摇满发队头 */
            rt_pend_disarm(rt_cur);           /* 站21 评审:成功出口缴械 */
            return 0;
        }
        if (ticks == 0)
            return RT_TIMEOUT;
        rt_sched();
        if (rt_cur->timed_out) {
            rt_pend_disarm(rt_cur);
            return RT_TIMEOUT;
        }
    }
}

int rt_queue_send(rt_queue_t *q, u64 msg)
{
    return rt_queue_send_to(q, msg, RT_WAIT_FOREVER);
}

int rt_queue_recv(rt_queue_t *q, u64 *msg)
{
    return rt_queue_recv_to(q, msg, RT_WAIT_FOREVER);
}
