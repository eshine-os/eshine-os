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
