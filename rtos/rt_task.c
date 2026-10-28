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
    t->base_prio = t->prio;          /* 站十八B:PI 基准 */
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
    rdy_remove(rt_cur);              /* 站19:清自拣残令牌——否则摘头会
                                      * 摘到自己,DEAD 被复活的假醒 */
    if (rt_fp_owner == rt_cur)       /* 站19C:持权者退场即弃权——槽位
                                      * 会被 create 复用,stale owner 指
                                      * 向复用后的活结构会错存帧 */
        rt_fp_owner = NULL;
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
    rdy_remove(rt_cur);              /* 站19:真挂睡前清自拣令牌——不清则
                                      * rt_sched 摘到自己即"醒",睡不
                                      * 落地变忙自旋,饿死低优先就绪者 */
    rt_sched();                      /* 让位;醒来从此处继续 */
}

void rt_block_rx(void)
{
    u64 d = rt_daif_save();

    /* 站20 [41] 闪断定谳:壳在 rt_getc_nb 判空与发布 BLOCK 之间存在
     * 无屏窗——UART ISR 恰落窗内:字节已入 rrx 环,而 wake_all 只认
     * BLOCK 态,RUN 态任务收到唤醒即吞,壳照旧挂起。末字节入环而壳
     * 永睡,vCPU 心跳照跳 = 门禁 [37] "token 单见" 的全息画像(原生
     * 唤醒源只有后续 IRQ,末字节无后继,永不自愈)。判空+发布+摘队
     * 同入 DAIF 屏蔽窗:窗内终判环已非空则不挂起,回调用者重查即得;
     * 窗后挂起已可见,ISR 唤醒走回流令牌兑付(rt_sched 自拣支)。 */
    if (rt_rx_pending()) {
        rt_daif_load(d);
        return;                      /* 迟到字节:不挂起,壳循环顶再取 */
    }
    rt_cur->wait_mask = RT_RX_BIT;
    rt_cur->state = RT_BLOCK;
    rdy_remove(rt_cur);              /* 站19:同上,真阻塞必清令牌 */
    rt_daif_load(d);                /* 窗闭合:此后 ISR 见 BLOCK 即正确唤醒 */
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
