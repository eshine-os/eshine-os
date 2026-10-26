/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "rtos.h"

/* 任务池(静态,无运行时分配器——YAGNI):8 槽 + 各 4KB 栈。
 * idle 是引导语境就地转正的伪任务,不占池。 */

rt_task_t *volatile rt_cur;
volatile u64 rt_ticks;

static rt_task_t pool[RT_POOL_MAX];
static u8 task_stacks[RT_POOL_MAX][4096] __attribute__((aligned(16)));
static rt_task_t idle_task;

rt_task_t *rt_task_create(const char *name, void (*entry)(void *),
                          void *arg, u32 prio)
{
    unsigned int i, slot;
    rt_task_t *t = NULL;

    for (i = 0; i < RT_POOL_MAX; i++) {
        if (pool[i].entry == NULL || pool[i].state == RT_DEAD) {
            t = &pool[i];
            slot = i;              /* 栈下标=池槽位,与名字长度无关
                                     * (站 16 潜伏 bug:名字循环曾复用
                                     * i,frame_init 拿到的是名字长度当
                                     * 栈下标——shell 住 stack[5]、
                                     * heartbeat 住越界 stack[9],全靠
                                     * 名字长度不撞车;站 17 首对同长并
                                     * 发任务 producer/consumer 双双 8 字
                                     * 共享 0xd430 帧,当场对撞炸出) */
            break;
        }
    }
    if (!t)
        return NULL;                /* 池满:调用方自报(门禁不触发) */
    for (i = 0; i < sizeof(t->name) - 1 && name[i]; i++)
        t->name[i] = name[i];
    t->name[i] = '\0';
    t->entry = entry;
    t->arg = arg;
    t->prio = prio < RT_NPRIO ? prio : RT_NPRIO - 1;
    t->state = RT_READY;
    t->wait_mask = 0;
    t->wq_next = NULL;            /* 站十七:同步队列链,复用槽必清 */
    t->wake_tick = 0;
    t->slice_left = RT_SLICE;
    rt_frame_init(t, task_stacks[slot] + sizeof(task_stacks[slot]), entry, arg);
    rt_ready_push(t);
    return t;
}

void rt_task_exit(void)             /* noreturn:DEAD 后必切走 */
{
    rt_task_t *next;

    rt_cur->state = RT_DEAD;
    next = rt_pick_ready();
    if (!next)
        rt_idle_loop();             /* 理论兜底:全灭时回 idle 轮询 */
    next->state = RT_RUN;
    rt_switch_to(next);              /* 本任务 DEAD,永不续跑;堵返回路径 */
    __builtin_unreachable();
}

void rt_sleep_ticks(u64 n)
{
    rt_cur->wake_tick = rt_ticks + n;
    rt_cur->state = RT_SLEEP;
    rt_sched();                      /* 让位;醒来从此处继续 */
}

void rt_block_rx(void)
{
    rt_cur->wait_mask = RT_RX_BIT;
    rt_cur->state = RT_BLOCK;
    rt_sched();
}

void rt_task_wake_all(u32 mask)
{
    unsigned int i;

    for (i = 0; i < RT_POOL_MAX; i++) {
        rt_task_t *t = &pool[i];

        if (t->entry && t->state == RT_BLOCK && (t->wait_mask & mask)) {
            t->wait_mask = 0;
            rt_ready_push(t);
        }
    }
}

void rt_task_wake_sleepers(void)
{
    unsigned int i;

    for (i = 0; i < RT_POOL_MAX; i++) {
        rt_task_t *t = &pool[i];

        if (t->entry && t->state == RT_SLEEP &&
            ((long long)rt_ticks - (long long)t->wake_tick) >= 0)
            rt_ready_push(t);
    }
}

const char *rt_state_name(u32 s)
{
    switch (s) {
    case RT_READY: return "READY";
    case RT_RUN:   return "RUN";
    case RT_SLEEP: return "SLEEP";
    case RT_BLOCK: return "BLOCK";
    default:       return "DEAD";
    }
}

/* ps 表遍历:池 8 槽,随后 idle 虚拟行(rt_main 转正时已填名) */
rt_task_t *rt_ps_first(unsigned int *idx)
{
    *idx = 0;
    return &pool[0];
}

rt_task_t *rt_ps_next(unsigned int *idx)
{
    (*idx)++;
    if (*idx < RT_POOL_MAX)
        return &pool[*idx];
    if (*idx == RT_POOL_MAX)
        return &idle_task;
    return NULL;
}

rt_task_t *rt_idle_task(void)
{
    return &idle_task;
}
