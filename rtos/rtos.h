/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#ifndef RTOS_H
#define RTOS_H

typedef unsigned long long u64;
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

#ifndef NULL
#define NULL ((void *)0)
#endif

/* eshine-rtos:参考RTOS 公开设计为基准的最小任务内核(第十六站)。
 * 32 级优先级(0 最高,31 最低),位图找就绪,同级 tick 轮转,
 * 统一任务帧(elr 自描述续跑点,见 vec.S)。 */

#define RT_NPRIO     32u
#define RT_POOL_MAX  8u
#define RT_SLICE     4u               /* 同级轮转量子(tick 计) */
#define RT_HZ        100u             /* vtimer tick 频率 */

#define RT_RX_BIT    0x0001u          /* BLOCK 等待位:串口 RX
                                       * (通用原语——信号量/互斥——留十七站) */

#define RT_READY  0u
#define RT_RUN    1u
#define RT_SLEEP  2u
#define RT_BLOCK  3u
#define RT_DEAD   4u

struct rt_task {
    /* asm 契约(vec.S):sp 必须在偏移 0;统一帧顶指针(elr 槽) */
    u64 sp;
    void (*entry)(void *arg);
    void *arg;
    u32 prio;
    u32 state;
    u32 wait_mask;
    u64 wake_tick;
    u32 slice_left;
    struct rt_task *next;        /* 就绪 FIFO 尾链(非环,摘除简单) */
    struct rt_task *wq_next;     /* 同步对象等待队列链(第十七站) */
    char name[12];
};
typedef struct rt_task rt_task_t;

extern rt_task_t *volatile rt_cur;
extern volatile u64 rt_ticks;

/* rt_task.c */
rt_task_t *rt_task_create(const char *name, void (*entry)(void *),
                          void *arg, u32 prio);
void rt_task_exit(void) __attribute__((noreturn));
void rt_sleep_ticks(u64 n);
void rt_block_rx(void);
const char *rt_state_name(u32 s);
void rt_task_wake_all(u32 mask);         /* 扫池:命中等待位的 BLOCK->READY */
void rt_task_wake_sleepers(void);       /* 扫池:到期的 SLEEP->READY */
rt_task_t *rt_ps_first(unsigned int *idx);
rt_task_t *rt_ps_next(unsigned int *idx);
rt_task_t *rt_idle_task(void);

/* rt_sched.c */
void rt_sched(void);             /* 让出点:C 选,asm 换 */
rt_task_t *rt_pick_ready(void);  /* 就绪摘头;空则 NULL */
void rt_ready_push(rt_task_t *t);
void rt_wake_rx(void);
void rt_tick_bookkeep(void);     /* tick ISR:唤醒/轮转/切判 */
int  rt_need_switch(void);

/* rt_uart.c:自有 RX 环(miniuart.h 的 static 环 per-TU,多 TU 会各持
 * 一份串不成一环——本环是唯一真环,ISR 与壳都走这里) */
void rt_puts(const char *s);     /* 行级临界区(DAIF 屏蔽)打印 */
void rt_puts_u64(const char *pfx, u64 v, const char *sfx);
void rt_uart_drain_isr(void);    /* IRQ 上下文:排空 DR 入 RTOS 环 */
int  rt_getc_nb(void);           /* 空环返回 -1,不阻塞 */

/* rt_shell.c */
void rt_shell_task(void *arg);

/* rt_fp.c */
void rt_fp_task(void *arg);

/* rt_main.c */
rt_task_t *rt_irq_dispatch(void);  /* vec.S bl 目标:返回 0=续跑,否则=新任务 */
void rt_idle_loop(void) __attribute__((noreturn));

/* vec.S:续跑路径经 .Lcoop_ret -> rt_sched 调用点后的正常收尾返回
 * ——绝不能标 noreturn(GCC 会在调用点后排环回头/死代码,续跑即踩;
 * 本站实测:明雷=空指针炸,暗雷=落进 puts_uart 无限打印)。 */
void rt_switch_to(rt_task_t *next);
void rt_frame_init(rt_task_t *t, void *stack_top, void (*entry)(void *), void *arg);

/* rt_sched.c:DAIF 短临界段(第十七站)。save/restore 而非 set/clr——
 * ISR 语境(I=1)restore 原样归还,不泄掩码。 */
u64 rt_daif_save(void);
void rt_daif_load(u64 d);

/* rt_sync.c(第十七站):同步原语——每对象 FIFO 等待队列(参考RTOS 公开
 * 设计为基准的最小形态:唤醒队头,非广播)。take 重检环(醒后自己复核
 * 条件),give 让渡+唤醒队头,被醒者更高优先则立即让位;
 * "唤醒=允许重试,非授予"。 */
typedef struct rt_sem {
    long count;                   /* 可用资源数 */
    rt_task_t *wq_head;           /* 等待队列(单向,尾链) */
    rt_task_t *wq_tail;
} rt_sem_t;

typedef struct rt_mutex {
    rt_task_t *owner;             /* 持锁者(递归判据) */
    u32 nest;                     /* 同 owner 重入深度 */
    rt_task_t *wq_head;
    rt_task_t *wq_tail;
} rt_mutex_t;

void rt_sem_init(rt_sem_t *s, long init);
long rt_sem_take(rt_sem_t *s);    /* 资源不足则挂等(无超时,0=取到) */
long rt_sem_give(rt_sem_t *s);    /* count++ + 唤醒队头 */
void rt_mutex_init(rt_mutex_t *m);
long rt_mutex_take(rt_mutex_t *m);/* 空则占;owner 重入 nest++;否则挂等 */
long rt_mutex_give(rt_mutex_t *m);/* 非拥有者 -1;nest 归零让渡+唤醒 */

/* rt_demo.c(第十七站):演示即判据 */
void rt_demo_sem(void);
void rt_demo_mutex(void);

#endif /* RTOS_H */
