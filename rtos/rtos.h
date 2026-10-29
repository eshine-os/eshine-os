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
#define RT_NCPU      2u               /* 站二十:vm0 双核(per-CPU 就绪队列) */
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
    u32 base_prio;               /* 站十八B:PI 基准优先级(boost 还原用;
                                   * 置结构尾:前部偏移是 asm/帧契约不许动 */
    /* per-task FP 帧(站十八C):16B 对齐 q 阵列,偏移 96 = vec.S 的
     * .equ RT_TASK_Q asm 契约(下方 _Static_assert 钉死);帧即完备,
     * 切换全量存取,泵 per-VM FP 同哲学第三兑。idle 同结构同享。 */
    u64 q[64] __attribute__((aligned(16)));
    u64 fpcr;
    u64 fpsr;
    u32 cpu;                     /* 站二十:就绪队列亲和(0/1)。结构尾
                                   * 追加——前部偏移 asm/帧契约钉死,
                                   * q@96/fpcr@608/fpsr@616 断言不动 */
    /* 站二十一:超时挂等登记(结构尾追加,同上契约;扫描门
     * pend_deadline!=0——wait_mask/事件注册表型 BLOCK 一律免疫) */
    u64 pend_deadline;           /* 绝对截止一次算死;0=未武装 */
    struct rt_task **pend_qh;    /* 超时自摘回指针(对象等待队) */
    struct rt_task **pend_qt;
    struct rt_task *pend_owner;  /* 互斥挂等时=锁主(PI 重算用);
                                  * sem/queue=NULL(typedef 于 struct 后,
                                  * 同 next/wq_next 用 struct 形) */
    u32 timed_out;               /* 扫描置位,take 环消费 */
    u32 low_warned;              /* 站23B:live-sp 守卫一次性告警旗
                                  * (create 清零;deepdiver 封窗深潜
                                  * tick 看不见,零假阳性) */
};
typedef struct rt_task rt_task_t;

/* asm 契约断言:vec.S 的 .equ RT_TASK_Q/FPCR/FPSR 必须与此一致
 * (改结构必编译期炸——站 17 名字长度栈下标的教训:契约要机器盯) */
_Static_assert(__builtin_offsetof(rt_task_t, q) == 96, "RT_TASK_Q=96");
_Static_assert(__builtin_offsetof(rt_task_t, fpcr) == 608, "RT_TASK_FPCR=608");

extern rt_task_t *volatile rt_cur_arr[RT_NCPU];   /* 站二十:per-CPU 当前
                                                   * 任务;宏保旧名,vec.S
                                                   * 契约符号转数组 */
extern volatile u64 rt_ticks;

/* 站二十:本核号。TPIDR_EL1 引导/次核入口各写一次(0/1),此后只读
 * ——per-CPU 数据索引的标准位置(免每次读 MPIDR)。 */
static inline u32 rt_cpu_id(void)
{
    u64 v;

    __asm__ volatile("mrs %0, tpidr_el1" : "=r"(v));
    return (u32)v;
}

#define rt_cur (rt_cur_arr[rt_cpu_id()])   /* C 树零改动:读写点全保旧名 */

/* 站23B:当前 SP 直读(asm;判深/守卫共用——__builtin_frame_address
 * 在 -O2 无帧指针下不可用,站21 实测定谳)。 */
static inline char *rt_cur_sp(void)
{
    char *p;

    __asm__ volatile("mov %0, sp" : "=r"(p));
    return p;
}

/* rt_task.c */
rt_task_t *rt_task_create(const char *name, void (*entry)(void *),
                          void *arg, u32 prio);
rt_task_t *rt_task_create_cpu(const char *name, void (*entry)(void *),
                             void *arg, u32 prio, u32 cpu); /* 站二十 */
void rt_task_exit(void) __attribute__((noreturn));
void rt_sleep_ticks(u64 n);
void rt_block_rx(void);
const char *rt_state_name(u32 s);
void rt_task_wake_all(u32 mask);         /* 扫池:命中等待位的 BLOCK->READY */
void rt_task_wake_sleepers(void);       /* 扫池:到期的 SLEEP->READY */
rt_task_t *rt_ps_first(unsigned int *idx);
rt_task_t *rt_ps_next(unsigned int *idx);
rt_task_t *rt_idle_task(void);
rt_task_t *rt_idle1_task(void);      /* 站二十:次核 idle(池外伪任务) */
void rtos_secondary_main(u32 cpu);   /* 站二十:次核世界(asm 入口落点) */
void rtos_secondary_entry(void);      /* 站二十:asm 入口(vec.S .global) */
extern char __cpu1_stack_top[];       /* 站二十:链接器次核引导栈页顶 */

/* rt_smp 面(站二十;落在 rt_sched.c) */
u64   rt_spin_lock(unsigned long *lk); /* DAIF 屏 + 跨核自旋(WFE),返回
                                        * 屏蔽前 DAIF;同核递归=死锁 */
void  rt_spin_unlock(unsigned long *lk, u64 d); /* SEV 广播 + DAIF 还原 */
void  rt_ipi(u32 cpu);                 /* 跨核重调度:ICC_SGI1R_EL1 写
                                         * (EL2 陷阱转发+poke,19D 机制) */
void  rt_resched_ipi(void);            /* 收 IPI 的重调度判(SGI handler) */
unsigned long rt_ipi_rx(u32 cpu);      /* 每核收到的 IPI 拍数(demo 判据) */
extern volatile unsigned int rt_ipi_echo; /* 站二十:ipidemo 回合窗(核 1
                                           * handler 见 1 回发核 0) */

/* rt_sched.c */
void rt_sched(void);             /* 让出点:C 选,asm 换 */
rt_task_t *rt_pick_ready(void);  /* 就绪摘头;空则 NULL */
void rt_ready_push(rt_task_t *t);
void rt_ready_requeue(rt_task_t *t); /* 自拣回流令牌:只回队不碰状态(站19) */
void rt_preempt_requeue(rt_task_t *t); /* 抢占回队恒单份:先清残令牌(站19) */
void rdy_remove(rt_task_t *t);   /* 站十八B:PI 降级摘除;站19 起兼清自拣令牌 */
void rt_wake_rx(void);
void rt_tick_bookkeep(void);     /* tick ISR:唤醒/轮转/切判 */
int  rt_need_switch(void);

/* rt_uart.c:自有 RX 环(miniuart.h 的 static 环 per-TU,多 TU 会各持
 * 一份串不成一环——本环是唯一真环,ISR 与壳都走这里) */
void rt_puts(const char *s);     /* 行级临界区(DAIF 屏蔽)打印 */
void rt_puts_u64(const char *pfx, u64 v, const char *sfx);
void rt_puts_hex(const char *pfx, u64 v, const char *sfx); /* 站21B:十六进制
                                   * 裸打(不取 puts_lk)——panic/诊断面,
                                   * 持锁语境取锁=同核自死锁 */
void rt_uart_drain_isr(void);    /* IRQ 上下文:排空 DR 入 RTOS 环 */
int  rt_getc_nb(void);           /* 空环返回 -1,不阻塞 */
int  rt_rx_pending(void);        /* 环非空即 1:挂起前终判([41] 修复判据) */

/* rt_shell.c */
void rt_shell_task(void *arg);

/* rt_fp.c */
void rt_fp_task(void *arg);
void rt_fp_dual_task(void *arg);    /* 站十八C:Σ(i×0.5) 半值闭式 */
void rt_fp_smp_a_task(void *arg);   /* 站二十:fpsmp 核 0 面(整段精确) */
void rt_fp_smp_b_task(void *arg);   /* 站二十:fpsmp 核 1 面(半值精确) */
void rt_fp_dual_a_task(void *arg);  /* 站十八C:Σi 复用,fpdual 独立针 */
void rt_sync_fault(void);           /* 站19C:EC 0x07 FPEN 陷阱,轮转持有权 */
extern rt_task_t *rt_fp_owner_arr[RT_NCPU]; /* 站20:per-CPU FP 持有者
                                             * (每核各持各的 FP 硬件,
                                             * vec.S 簿记符号转数组) */
extern u32 rt_fp_stat_arr[RT_NCPU];  /* 站20:per-CPU 切换计数(asm RMW
                                      * 双核互踩根除;显示走求和) */
extern u32 rt_fp_saves;              /* 站19C:持有权轮转数(fpstat 分子;
                                      * 唤醒源单点,暂全局) */
#define rt_fp_owner (rt_fp_owner_arr[rt_cpu_id()])
#define rt_fp_stat  (rt_fp_stat_arr[rt_cpu_id()])
u32 rt_fp_stat_total(void);         /* 站20:两核求和(fpstat 显示面) */

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
long rt_sem_take(rt_sem_t *s);    /* 资源不足则挂等(RT_WAIT_FOREVER 薄壳) */
long rt_sem_give(rt_sem_t *s);    /* count++ + 唤醒队头 */
void rt_mutex_init(rt_mutex_t *m);
long rt_mutex_take(rt_mutex_t *m);/* 空则占;owner 重入 nest++;否则挂等 */
long rt_mutex_give(rt_mutex_t *m);/* 非拥有者 -1;nest 归零让渡+唤醒 */

/* 站二十一:超时族(参考RTOS 公开语义:0=no-wait 单次复核即判负;
 * RT_WAIT_FOREVER=永不超时;其他=绝对截止,take 环被醒后复核,
 * 条件仍不满足按剩余恒以首绝对 deadline 重挂——锚点=调用点) */
#define RT_WAIT_FOREVER (~0UL)
#define RT_TIMEOUT      (-2L)
long rt_sem_take_to(rt_sem_t *s, u64 ticks);        /* 0=取到 RT_TIMEOUT=超时 */
long rt_mutex_take_to(rt_mutex_t *m, u64 ticks);
void rt_timeout_unpend(rt_task_t *t);               /* 扫描侧:摘链+owner
                                                    * PI 重算(等待集变化
                                                    * 不变量,与 give 同律) */
void rt_pend_disarm(rt_task_t *t);                  /* 成功出口缴械挂等
                                                    * 登记(评审核:stale
                                                    * 武装穿透免疫承诺) */

/* 站21B:panic 观测面。扫描面无寄存器行(ESR/ELR 此刻是残值,印了
 * 冒充现场);陷阱面 ESR/ELR/FAR 架构有效才印。皆 noreturn。 */
void rt_panic(const char *why, unsigned int slot) __attribute__((noreturn));
void rt_panic_fault(const char *why) __attribute__((noreturn));

/* 站21C:栈限位导出(pool/task_stacks 私有 static,demo 经此取址)。
 * NULL=非池任务(idle/引导语境),demo 自守。 */
void *rt_cur_stack_limit(void);    /* 可用区底(哨兵上方) */
void *rt_cur_stack_floor(void);    /* 垫内深限(越哨兵受控止点) */
u32   rt_cur_stack_margin(unsigned int slot);  /* stackcheck/ps 用;0=槽空 */
int   rt_stack_slot_ok(unsigned int slot);    /* 1=哨兵完好 0=已坏
                                               * -1=槽未创建 */

/* 站19B 起导出(原 static):rt_ipc.c 队列同机器;站21A 起挂等并入
 * take 族单窗形态,rt_pend 退役,wq_push 随 _to 族一并导出 */
void rt_wq_push(rt_task_t **head, rt_task_t **tail, rt_task_t *t);
void rt_handoff(rt_task_t **head, rt_task_t **tail);

/* rt_ipc.c(第十九站B):事件组+消息队列。事件组=每事件条件注册表
 * (任务链挂不下 want/and_mode),set 扫表只摘满足者;队列=站 17
 * 原样 FIFO 等待队列机器(满发挂 tx 队,空收取 rx 队)。 */
#define RT_QCAP 8u                 /* 环容量上限(demo 尺度) */

typedef struct rt_event_waiter {
    rt_task_t *t;
    u32 want;
    int and_mode;
} rt_event_waiter_t;

typedef struct rt_event {
    u32 flags;
    unsigned int nw;                           /* 注册表在用数 */
    rt_event_waiter_t wait[RT_POOL_MAX];       /* 条件注册表(池即容量) */
} rt_event_t;

typedef struct rt_queue {
    u64 buf[RT_QCAP];
    u32 head, count, cap;                      /* 尾=(head+count)%cap,免冗余 */
    rt_task_t *rx_h, *rx_t;                    /* 空取等待队列 */
    rt_task_t *tx_h, *tx_t;                    /* 满发等待队列 */
} rt_queue_t;

void rt_event_init(rt_event_t *e);
int rt_event_wait(rt_event_t *e, u32 want, int and_mode, u32 *got); /* 命中即耗 */
void rt_event_set(rt_event_t *e, u32 bits);    /* |= 且扫表醒满足者 */
void rt_queue_init(rt_queue_t *q, u32 cap);    /* cap<=RT_QCAP,0 拒 */
int rt_queue_send(rt_queue_t *q, u64 msg);     /* 满则挂发队,醒后复核 */
int rt_queue_recv(rt_queue_t *q, u64 *msg);    /* 空则挂取队,醒后复核 */
int rt_queue_send_to(rt_queue_t *q, u64 msg, u64 ticks);   /* 站21A(超时族,
                                   * 定义在 rtos.h 同步节:rt_queue_t 此处
                                   * 才完成定义) */
int rt_queue_recv_to(rt_queue_t *q, u64 *msg, u64 ticks);

/* rt_demo.c(第十七站):演示即判据 */
void rt_demo_sem(void);
void rt_demo_mutex(void);
void rt_demo_pi(void);           /* 站十八B:PI 饥饿反转 */
void rt_demo_fpdual(void);       /* 站十八C:per-task FP 并发自证 */
void rt_demo_event(void);
void rt_demo_smp(void);              /* 站二十:双核计数交错 */
void rt_demo_ipi(void);              /* 站二十:SGI 跨核唤醒全链 */
void rt_demo_fpsmp(void);            /* 站二十:per-CPU FP 双精确 */
void rt_demo_queue(void);        /* 站十九B:16 条收发+满环阻塞 */
void rt_demo_tmo(void);              /* 站21A:超时面全弧(no-wait/超时/
                                     * 在时醒/PI boost-回收) */
void rt_demo_stk(void);              /* 站21C:受控深潜(水位判据) */
void rt_demo_overflow(void);         /* 站21C:受控越哨→panic([74]) */

/* rt_timer.c(站二十二B):软定时器。==== 回调语境契约(评审核折入,
 * 参考RTOS 反转警示:ktimer 回调跑在 timer 任务语境可阻塞,本设计是
 * ISR 语境,直译即灾难)====:回调=核 0 tick-ISR(I=1),rt_cur 是
 * 被打断任务不属于回调。允许:rt_puts/rt_puts_u64(持锁即屏本核)/
 * rt_ready_push(限 SLEEP 态唤醒)/rt_timer_stop(含自停)。禁止:
 * 一切挂起 rt_cur 的调用(sleep/take 族——ISR 帧内 rt_sched 换帧
 * =EOI 永不执行=核 0 中断面死);rt_sem_give/rt_event_set(尾段
 * rt_sched 同病);rt_timer_start(同环重入)。
 * ==== 登记契约:==== start/stop 仅核 0 语境(同核序贯免锁;跨核
 * 需池自旋锁,记残余);结构体须常驻存储且活过回调本体(单发回调
 * 执行时 deadline 已清 0);重复 start=改期不双登;stop 幂等。
 * start 返 0 成功/-1 池满或 ticks==0(可观测拒绝)。 */
#define RT_TMAX 8u

typedef struct rt_timer {
    u64 deadline;                     /* 绝对截止;0=停(0=哨兵同 wake_tick) */
    u64 period;                       /* 0=单发;>0=周期,零漂移重臂 */
    void (*fn)(void *);
    void *arg;
} rt_timer_t;

void rt_timer_init(rt_timer_t *t, void (*fn)(void *), void *arg);
int  rt_timer_start(rt_timer_t *t, u64 ticks, int periodic);
void rt_timer_stop(rt_timer_t *t);
void rt_timer_tick(void);            /* 核 0 单写者扫描:到期即回调 */

void rt_demo_swt(void);              /* 站22B:swtdemo 判据 */

/* rt_main.c(站23A):irq handler 注册表。替换式登记(一 intid 一槽,
 * 重复登记=替换,返回旧 isr 供链式包装/恢复);IAR→查表→isr 先行,
 * 掩码后醒(rt_task_wake_all(mask)——RT_RX_BIT 泛化至此);无登记
 * intid 一行报(默认拒绝面诚实化)。登记窗口期(替换瞬间)同核
 * 序贯,IRQ 走新旧槽皆可,语义无害。 */
#define RT_IRQ_MAX 8
void (*rt_irq_register(u32 intid, void (*isr)(void), u32 wake_mask))(void);
void (*rt_irq_swap_uart(void (*isr)(void)))(void);  /* 站23A:33 号槽
                                   * 换钩助手(demo 面;掩码恒 RT_RX_BIT) */

/* 站23B:水位守卫预算(评审 disassembly 定谳账目)。守卫链=vec.S
 * 异常帧 272+dispatch 48+isr_vtimer 0(尾调)+bookkeep 64=384
 * ——恰 RT_ISR_BUDGET;sp(真值)低于可用底+384 时 ISR 链写到
 * magic 区,下一拍哨兵扫描即 panic(**任务真 sp 系**危险线)。
 * 守卫告警线=危险线+读点差 D≈350→734 取整 640——**守卫读点系**
 * (读点在真 sp 下方 D);打印链(组串后单 rt_puts)实测 ~160B
 * 自读点向下,640−384=256 即其+帧漂移余量。两线分属两坐标系,
 * 勿混比(评审余量算术定谳:旧注"打印链 ~100B"实测 160,已正)。 */
#define RT_ISR_BUDGET  384u
#define RT_GUARD_LINE  640u

void rt_demo_irq(void);              /* 站23A:irqdemo(注册表活证) */
void rt_demo_low(void);              /* 站23B:lowdemo(贴线守卫) */

#endif /* RTOS_H */
