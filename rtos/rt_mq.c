/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "rtos.h"

/* 站三十七:POSIX mqueue——命名优先级消息队列(参考实现 posix_queue
 * 机制白描的站内形状:msg 型 ops+优先级;PNODE/事件位=后弧)。
 * 阻塞=站 17 pend 机器第 N 笔复用(满挂发队/空挂取队,单窗 DAIF
 * 形+成功出口缴械律,与 rt_queue/sem 同款——零新等待逻辑)。
 * 高优先先出,同优先 FIFO(插入点=首个严格更低优先者之前)。
 * mqd=内核全局表位;fork 子天然可见同 mqd(POSIX 继承近亲,
 * 无每进程描述符表=后弧记边界)。 */

#define MQ_NAME 16

struct rt_mq {
    u32 used;
    u32 users;                     /* close 计数(全关即销毁候选) */
    char name[MQ_NAME];
    u32 count;
    u64 msg[RT_MQ_DEPTH];
    u8 prio[RT_MQ_DEPTH];
    rt_task_t *rx_h, *rx_t;        /* 空取等待者 */
    rt_task_t *tx_h, *tx_t;        /* 满发等待者 */
};

static struct rt_mq mqs[RT_MQ_MAX];

static struct rt_mq *mq_of(long mqd)
{
    if (mqd < 1 || mqd > RT_MQ_MAX || !mqs[mqd - 1].used)
        return NULL;
    return &mqs[mqd - 1];
}

static void mq_copy_str(char *dst, const char *src)
{
    int i;

    for (i = 0; i < MQ_NAME - 1 && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

static int mq_streq(const char *a, const char *b)
{
    int i;

    for (i = 0; i < MQ_NAME; i++) {
        if (a[i] != b[i])
            return 1;
        if (a[i] == '\0')           /* NUL 即收:NUL 后的槽残值
                                      * 不参比(首轮 unlink 即此雷) */
            return 0;
    }
    return 0;
}

long rt_mq_open(const char *path)
{
    u32 i;

    if (!path)
        return -22;
    for (i = 0; i < RT_MQ_MAX; i++) {
        if (mqs[i].used && mq_streq(mqs[i].name, path) == 0) {
            mqs[i].users++;
            return (long)i + 1;
        }
    }
    for (i = 0; i < RT_MQ_MAX; i++) {
        if (!mqs[i].used) {
            mqs[i].used = 1;
            mqs[i].users = 1;
            mq_copy_str(mqs[i].name, path);
            mqs[i].count = 0;
            mqs[i].rx_h = mqs[i].rx_t = NULL;
            mqs[i].tx_h = mqs[i].tx_t = NULL;
            return (long)i + 1;
        }
    }
    return -12;                     /* -ENOMEM:表满 */
}

/* 优先级插入:高先出,同优先 FIFO(插到首个严格更低者之前) */
static void mq_insert(struct rt_mq *q, u64 val, u32 prio)
{
    u32 pos = q->count;

    while (pos > 0 && q->prio[pos - 1] < prio)
        pos--;
    {
        u32 j;

        for (j = q->count; j > pos; j--) {
            q->msg[j] = q->msg[j - 1];
            q->prio[j] = q->prio[j - 1];
        }
    }
    q->msg[pos] = val;
    q->prio[pos] = (u8)prio;
    q->count++;
}

long rt_mq_send(long mqd, u64 val, u32 prio)
{
    struct rt_mq *q = mq_of(mqd);
    u64 deadline = 0;

    if (!q)
        return -22;
    for (;;) {
        int got = 0;
        u64 d = rt_daif_save();

        if (q->count < RT_MQ_DEPTH) {
            mq_insert(q, val, prio);
            got = 1;
        } else {
            rt_cur->state = RT_BLOCK;
            rt_cur->timed_out = 0;
            rt_cur->pend_deadline = deadline;   /* 0=无限等 */
            rt_cur->pend_qh = &q->tx_h;
            rt_cur->pend_qt = &q->tx_t;
            rt_cur->pend_owner = NULL;
            rt_wq_push(&q->tx_h, &q->tx_t, rt_cur);
            rdy_remove(rt_cur);
        }
        rt_daif_load(d);
        if (got) {
            rt_handoff(&q->rx_h, &q->rx_t);
            rt_pend_disarm(rt_cur);
            return 0;
        }
        rt_sched();
        if (rt_cur->timed_out) {
            rt_pend_disarm(rt_cur);
            return -110;
        }
    }
}

long rt_mq_receive(long mqd, u64 *val, u64 ticks)
{
    struct rt_mq *q = mq_of(mqd);
    u64 deadline = (ticks == RT_WAIT_FOREVER) ? 0 : rt_ticks + ticks;

    if (!q)
        return -22;
    for (;;) {
        int got = 0;
        u64 d = rt_daif_save();

        if (q->count > 0) {
            u32 i;

            if (val)
                *val = q->msg[0];
            for (i = 1; i < q->count; i++) {
                q->msg[i - 1] = q->msg[i];
                q->prio[i - 1] = q->prio[i];
            }
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
            rt_handoff(&q->tx_h, &q->tx_t);
            rt_pend_disarm(rt_cur);
            return 0;
        }
        if (ticks == 0)
            return -110;            /* no-wait 单次复核即判负 */
        rt_sched();
        if (rt_cur->timed_out) {
            rt_pend_disarm(rt_cur);
            return -110;
        }
    }
}

long rt_mq_close(long mqd)
{
    struct rt_mq *q = mq_of(mqd);

    if (!q)
        return -22;
    if (q->users)
        q->users--;
    /* POSIX 律:close 只降描述符面;队列本体活到 unlink(后开
     * 者(若已建)仍可见)。唯一销毁口=rt_mq_unlink。 */
    return 0;
}

long rt_mq_unlink(const char *path)
{
    u32 i;

    for (i = 0; i < RT_MQ_MAX; i++)
        if (mqs[i].used && mq_streq(mqs[i].name, path) == 0) {
            mqs[i].used = 0;
            return 0;
        }
    return -2;                       /* -ENOENT */
}
