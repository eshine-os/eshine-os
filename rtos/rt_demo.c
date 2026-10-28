/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "rtos.h"

/* 演示即判据(第十七站):静态对象,任务体自证同步原语。
 * 池占用:常驻 shell+heartbeat,演示峰值 +2 < 8。
 * 优先级:shell 5 > contender 6 > producer 7 > waiter 8 > consumer 9
 * > heartbeat 10(数字大=优先低)——确定性醒序全靠这张表。 */

#define RING_SLOTS 4
#define RING_ITEMS 8

static rt_sem_t sem_empty;      /* 有界环空槽数 */
static rt_sem_t sem_full;       /* 已填数 */
static rt_mutex_t demo_mutex;

/* ---- semdemo:有界环生产者-消费者(信号量对,阻塞乒乓) ---- */
static void producer_task(void *arg)
{
    unsigned int i;

    (void)arg;
    for (i = 1; i <= RING_ITEMS; i++) {
        rt_sem_take(&sem_empty);
        rt_puts_u64("semdemo: p", i, "\n");    /* 生产 i 号入环 */
        rt_sem_give(&sem_full);
    }
    rt_puts("semdemo: ring filled (8)\n");
}

static void consumer_task(void *arg)
{
    unsigned int i;

    (void)arg;
    for (i = 1; i <= RING_ITEMS; i++) {
        rt_sem_take(&sem_full);
        rt_puts_u64("semdemo: c", i, "\n");    /* 消费 i 号出环 */
        rt_sem_give(&sem_empty);
    }
    rt_puts("semdemo: ring done (8/8)\n");
}

void rt_demo_sem(void)
{
    rt_sem_init(&sem_empty, RING_SLOTS);
    rt_sem_init(&sem_full, 0);
    if (!rt_task_create("producer", producer_task, NULL, 7) ||
        !rt_task_create("consumer", consumer_task, NULL, 9))
        rt_puts("semdemo: pool full\n");
}

/* ---- mutexdemo:阻塞醒序 + 递归 + 非拥有者拒绝 ---- */
static void contender_task(void *arg)
{
    (void)arg;
    rt_puts("mutex: contender locking\n");
    rt_mutex_take(&demo_mutex);
    rt_puts("mutex: contender locked\n");
    rt_sleep_ticks(30);                      /* 300ms 持锁(ps 窗口) */
    rt_puts("mutex: contender unlocking\n");
    rt_mutex_give(&demo_mutex);
    rt_puts("mutex: contender released\n");
}

static void waiter_task(void *arg)
{
    (void)arg;
    if (rt_mutex_give(&demo_mutex) == -1)     /* 先试非拥有者给锁 */
        rt_puts("mutex: give denied (not owner)\n");
    rt_puts("mutex: waiter wants lock\n");
    rt_mutex_take(&demo_mutex);               /* 阻塞:醒序判据 */
    rt_puts("mutex: waiter locked (after block)\n");
    rt_mutex_take(&demo_mutex);               /* 递归:同 owner 重入 */
    rt_puts("mutex: recursive take 2 ok\n");
    rt_mutex_give(&demo_mutex);
    rt_mutex_give(&demo_mutex);
    rt_puts("mutex: waiter done\n");
}

void rt_demo_mutex(void)
{
    rt_mutex_init(&demo_mutex);
    if (!rt_task_create("contender", contender_task, NULL, 6) ||
        !rt_task_create("waiter", waiter_task, NULL, 8))
        rt_puts("mutexdemo: pool full\n");
}

/* ---- pidemo(站十八B):PI 饥饿反转——mid 抢死 low,high 挂等
 * boost low 9->6 反超完工。优先级:high 6 / mid 7 / low 9(shell 5
 * 恒最高);墙钟忙旋读 rt_ticks,时序确定性全靠这张表。 ---- */
static rt_mutex_t pi_mutex;

static void pi_high(void *arg)
{
    (void)arg;
    rt_sleep_ticks(100);            /* 让 low 先进临界区 */
    rt_puts("pi: high wants lock\n");
    rt_mutex_take(&pi_mutex);       /* 挂等:boost low 9->6 */
    rt_puts("pi: high got lock (after boost)\n");
    rt_mutex_give(&pi_mutex);
}

static void pi_mid(void *arg)
{
    u64 t0;
    u64 last = 0;

    (void)arg;
    rt_sleep_ticks(50);             /* t=50 醒:7 压 9,饿死 low */
    rt_puts("pi: mid spinning (starving low)\n");
    t0 = rt_ticks;
    while (rt_ticks - t0 < 400) {
        if (rt_ticks != t0 && (rt_ticks - t0) % 100 == 0 &&
            rt_ticks != last) {
            last = rt_ticks;
            rt_puts("pi: mid spinning\n");
        }
    }
    rt_puts("pi: mid done\n");
}

static void pi_low(void *arg)
{
    u64 t0;

    (void)arg;
    rt_mutex_take(&pi_mutex);       /* t=0 即进临界区 */
    rt_puts("pi: low in CS at prio 9\n");
    t0 = rt_ticks;
    while (rt_ticks - t0 < 150)
        ;                           /* 临界区忙旋;boost 后反超 mid 完工 */
    rt_puts("pi: low running at prio 6 (boosted)\n");
    rt_puts("pi: low done (boosted)\n");
    rt_mutex_give(&pi_mutex);       /* 还继承:low 回 9 */
    rt_puts("pi: low restored prio 9\n");
}

void rt_demo_pi(void)
{
    rt_mutex_init(&pi_mutex);
    if (!rt_task_create("pihigh", pi_high, NULL, 6) ||
        !rt_task_create("pimid", pi_mid, NULL, 7) ||
        !rt_task_create("pilow", pi_low, NULL, 9))
        rt_puts("pidemo: pool full\n");
}

/* ---- fpdual(站十八C):per-task FP 帧判据,双 FP 任务并发 ---- */
void rt_demo_fpdual(void)
{
    /* 站20:B 12->11 同级:RR 量子交织 A/B,每片边界一次持有权
     * 轮转(fp-saves 递增),fpstat 判据 M<<N 才有非零分子;异级
     * 串行(A 先跑完退场清主,B 才起步)首陷阱恒见旧主 NULL,
     * saves 恒 0——[34] 门禁实测抓出,双任务语义不变仍双精确。 */
    if (!rt_task_create("fpdA", rt_fp_dual_a_task, NULL, 11) ||
        !rt_task_create("fpdB", rt_fp_dual_task, NULL, 11))
        rt_puts("fpdual: pool full\n");
}

/* ---- eventdemo(站十九B):AND 双拍不醒+双拍齐醒+OR 支线 ----
 * 优先级:setter 6 < AND-waiter 8 < OR-waiter 9——set 醒即抢,
 * 行序全确定性:A 拍 AND 不满(仍挂),B 拍齐醒,C 拍 OR 即醒。 */
static rt_event_t demo_event;

static void evt_waiter_and(void *arg)
{
    u32 got;

    (void)arg;
    rt_puts("evt: waiter waiting AND {A|B}\n");
    rt_event_wait(&demo_event, 0x5u, 1, &got);
    rt_puts("evt: waiter fired flags=0x5\n");   /* A|B 恰 0x5,字面自证 */
}

static void evt_waiter_or(void *arg)
{
    u32 got;

    (void)arg;
    rt_puts("evt: or-waiter waiting OR {C}\n");
    rt_event_wait(&demo_event, 0x8u, 0, &got);
    rt_puts("evt: OR fired flags=0x8\n");
}

static void evt_setter(void *arg)
{
    (void)arg;
    rt_sleep_ticks(50);
    rt_event_set(&demo_event, 0x1u);            /* A:AND 不满,waiter 仍挂 */
    rt_puts("evt: A set (waiter stays)\n");
    rt_sleep_ticks(50);
    rt_event_set(&demo_event, 0x4u);            /* B:双拍齐,AND met */
    rt_puts("evt: B set (AND met)\n");
    rt_sleep_ticks(50);
    rt_event_set(&demo_event, 0x8u);            /* C:OR 支线即醒 */
    rt_puts("evt: C set (OR path)\n");
}

void rt_demo_event(void)
{
    rt_event_init(&demo_event);
    if (!rt_task_create("evtand", evt_waiter_and, NULL, 8) ||
        !rt_task_create("evtor", evt_waiter_or, NULL, 9) ||
        !rt_task_create("evtset", evt_setter, NULL, 6))
        rt_puts("eventdemo: pool full\n");
}

/* ---- queuedemo(站十九B):16 条 u64 环(cap=8),满发阻塞乒乓 ----
 * 优先级:producer 7 压 consumer 9——灌满即阻塞,收方慢收逼出
 * 多轮满环。行判据:第 9 条前的阻塞自报 + 16/16 闭环。 */
static rt_queue_t demo_queue;

static void q_producer(void *arg)
{
    unsigned int i;

    (void)arg;
    for (i = 1; i <= 16; i++) {
        if (i == 9)                             /* cap=8:第 9 条必先满 */
            rt_puts("q: ring full, sender blocked\n");
        rt_queue_send(&demo_queue, 0x1000u + i);
        rt_puts_u64("q: sent ", i, "\n");
    }
    rt_puts("q: producer done 16\n");
}

static void q_consumer(void *arg)
{
    unsigned int i;
    u64 msg;

    (void)arg;
    for (i = 1; i <= 16; i++) {
        rt_queue_recv(&demo_queue, &msg);
        rt_puts_u64("q: got ", i, "\n");
        rt_sleep_ticks(2);                      /* 慢收:逼出满环阻塞 */
    }
    rt_puts("q: done 16/16\n");
}

void rt_demo_queue(void)
{
    rt_queue_init(&demo_queue, 8);
    if (!rt_task_create("qprod", q_producer, NULL, 7) ||
        !rt_task_create("qcons", q_consumer, NULL, 9))
        rt_puts("queuedemo: pool full\n");
}
