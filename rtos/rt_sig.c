/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "rtos.h"

/* 站三十二:signal+futex——musl 的两张内核脸(参考实现 §10 站内收编,
 * 机制参考零拷贝)。两原语互为判据:真实"等待->唤醒"对由跨进程
 * 信号打断 futex 等待完成(EINTR 契约)——同站的架构理由。
 *
 * futex(16 桶静态表,key={proc,va} 随等待者;无 shm=is_shared 恒
 * 私):WAIT=入口值校验(*uaddr!=expect 即 -EAGAIN,Linux 契约)
 * +站 21 单窗形态阻塞(窗内终判+五字段登记+摘 rdy 一气呵成)
 * +醒后四路分诊:timed_out→-ETIMEDOUT / fut_woken→0 / sig_intr
 * →-EINTR / 值动→0(futex 协议:改字方须 WAKE,值动即释放);
 * 未定(伪醒)回环重挂。WAKE=桶内 key 匹配摘链+置 fut_woken+
 * push(DAIF 窗内,rt_handoff 律)——**不读用户字,纯队列操作**
 * (demo 的扑克任务据此代演同址空间第二执行流)。
 *
 * signal(v0 位图态):action(1..31 存 handler 用户地址)/kill
 * (活进程 pending|=位;唤醒 leader:pend 型=rt_timeout_unpend
 * +sig_intr=1+push,wait_mask 型=mask 清+push,SLEEP 不搅=醒后
 * 出口投递,RUN/READY 自然等出口)/投递(**syscall 出口**,参考实现
 * SWITCH_OUT 点的站内形态):整帧 272B 存 sig_saved+改写 elr=
 * handler、x0=sig——handler 在目标自身用户上下文执行,非他者
 * 代跑;handler 末尾显式 rt_sigreturn 恢复(SA_RESTORER 形=站
 * 33 musl 对齐)。默认动作=忽略(不 terminate,记档);在役
 * (sig_active)嵌套=挂起留下轮(不丢);sigqueue 排队/重启/
 * coredump=站 33+ 边界(spec §4)。 */

#define RT_FUTEX_BUCKETS 16u

struct rt_futex_q {
    rt_task_t *head;
    rt_task_t *tail;
};

static struct rt_futex_q futex_q[RT_FUTEX_BUCKETS];

static struct rt_futex_q *fx_bucket(struct rt_proc *p, u64 va)
{
    u64 h = (u64)(unsigned long)p ^ (va >> 2);

    return &futex_q[h & (RT_FUTEX_BUCKETS - 1u)];
}

/* 等待环出口收缴:站 21 缴械律 + futex 键/分诊位自清 */
static void fx_disarm(void)
{
    rt_pend_disarm(rt_cur);
    rt_cur->fut_proc = NULL;
    rt_cur->fut_va = 0;
    rt_cur->fut_woken = 0;
    rt_cur->sig_intr = 0;
}

static long rt_futex_wait(struct rt_proc *p, u64 va, u32 expect, u64 ticks)
{
    struct rt_futex_q *b = fx_bucket(p, va);
    volatile u32 *w = (volatile u32 *)(unsigned long)va;
    u64 deadline = (ticks == RT_WAIT_FOREVER) ? 0 : rt_ticks + ticks;

    if (*w != expect)
        return -11;                     /* -EAGAIN:入口值错(Linux 契约) */
    if (ticks == 0)
        return -11;                     /* no-wait 探测:值合而无可等 */
    for (;;) {
        u64 d = rt_daif_save();

        if (*w != expect) {             /* 窗内终判:值动即释放 */
            rt_daif_load(d);
            return 0;
        }
        rt_cur->state = RT_BLOCK;
        rt_cur->timed_out = 0;
        rt_cur->sig_intr = 0;           /* 重挂恒自清(kill 只碰 BLOCK) */
        rt_cur->fut_woken = 0;
        rt_cur->pend_deadline = deadline;
        rt_cur->pend_qh = &b->head;
        rt_cur->pend_qt = &b->tail;
        rt_cur->pend_owner = NULL;
        rt_cur->fut_proc = p;
        rt_cur->fut_va = va;
        rt_wq_push(&b->head, &b->tail, rt_cur);
        rdy_remove(rt_cur);
        rt_daif_load(d);
        rt_sched();
        if (rt_cur->timed_out) {
            fx_disarm();
            return -110;               /* -ETIMEDOUT */
        }
        if (rt_cur->fut_woken) {
            fx_disarm();
            return 0;                  /* FUTEX_WAKE 唤醒 */
        }
        if (rt_cur->sig_intr) {
            fx_disarm();
            return -4;                  /* -EINTR:信号打断 */
        }
        /* 伪醒:回环重检值 */
    }
}

long rt_futex_wake_key(struct rt_proc *p, u64 va, u32 n)
{
    struct rt_futex_q *b = fx_bucket(p, va);
    rt_task_t *w, *pred = NULL, *hit = NULL;
    u32 cnt = 0;
    u64 d = rt_daif_save();

    w = b->head;
    while (w && cnt < n) {
        rt_task_t *nx = w->wq_next;

        /* key 过滤:桶内多 key 共存(站内 4 进程,16 桶必碰撞) */
        if (w->state == RT_BLOCK && w->fut_proc == p &&
            w->fut_va == va) {
            if (pred)
                pred->wq_next = nx;
            else
                b->head = nx;
            if (b->tail == w)
                b->tail = pred;
            w->wq_next = NULL;
            w->pend_deadline = 0;      /* 摘死超时扫描(双摘互斥) */
            w->fut_woken = 1;
            rt_ready_push(w);
            cnt++;
            hit = w;
        } else {
            pred = w;
        }
        w = nx;
    }
    rt_daif_load(d);
    if (hit && hit->prio < rt_cur->prio)
        rt_sched();                     /* handoff 律:高优先醒客即位 */
    return (long)cnt;
}

long rt_futex(struct rt_proc *p, u64 uaddr, u32 op, u32 val, u64 ticks)
{
    if (!p)
        return -22;
    switch (op) {
    case RT_FUTEX_WAIT:
        return rt_futex_wait(p, uaddr, val, ticks);
    case RT_FUTEX_WAKE:
        return rt_futex_wake_key(p, uaddr, val);
    default:
        return -38;                     /* 命令集全集=站 33+ 后议 */
    }
}

/* ---- signal 面 ---- */

int rt_sig_action(struct rt_proc *p, u32 sig, u64 handler, u64 restorer)
{
    if (!p || sig == 0 || sig >= RT_SIG_MAX)
        return -22;
    p->sig_hand[sig] = handler;
    p->sig_rest[sig] = restorer;      /* 站38:SA_RESTORER 面(0=显式
                                       * rt_sigreturn 旧契约,两立) */
    return 0;
}

static void sig_wake_task(rt_task_t *t)
{
    u64 d;

    if (!t || t->state != RT_BLOCK)
        return;
    d = rt_daif_save();
    if (t->pend_qh) {               /* pend 型:摘链+置打断位 */
        rt_timeout_unpend(t);
        t->pend_deadline = 0;
        t->sig_intr = 1;
        rt_ready_push(t);
    } else if (t->wait_mask) {      /* wait_mask 型(壳 RX 面) */
        t->wait_mask = 0;
        rt_ready_push(t);
    }
    /* SLEEP 型不搅:醒后 syscall 出口投递;非 futex 的 pend 型
     * (sem/msg 等待者)由其 take 环吸收重挂,投递延至自然醒
     * (spec §4 边界)。 */
    rt_daif_load(d);
    if (t->prio < rt_cur->prio)
        rt_sched();
}

int rt_sig_kill(int pid, u32 sig)
{
    struct rt_proc *p = rt_proc_lookup(pid);

    if (sig == 0 || sig >= RT_SIG_MAX)
        return -22;
    if (!p)
        return -3;                      /* -ESRCH:死者不收信号 */
    p->sig_pending |= 1u << sig;
    p->sig_val[sig] = 0;                /* 站38:kill 面=零值 */
    sig_wake_task(p->leader);
    return 0;
}

/* 站38:kill+值+tid 定向(真号 138)。先按 tid 解析(任务级
 * 待决位图),不中再按 pid(进程面,leader 收)。返回 0/-ESRCH。 */
long rt_sig_queueinfo(long pid_or_tid, u32 sig, u64 value)
{
    struct rt_proc *p;
    rt_task_t *t;
    unsigned int idx;

    if (sig == 0 || sig >= RT_SIG_MAX)
        return -22;
    for (t = rt_ps_first(&idx); t; t = rt_ps_next(&idx))
        if (t->state != RT_FREE && t->state != RT_DEAD &&
            t->tid && t->tid == (u64)pid_or_tid) {
            /* 站40 定谳([95] 两轮同点):rt_ps_first/next=裸池
             * 遍历,死槽残 tid 抢路由——94 门长跑后死 tid=2/3/4
             * 槽早于活子,任务级信号入死槽=handler 永不上场
             * (子的 resumed 实为睡满超时,queued 回 0 是假绿)。
             * 站 38 律同型:死槽残值不作路由判据。 */
            t->sig_pending |= 1u << sig;
            t->sig_val = value;
            sig_wake_task(t);
            return 0;
        }
    p = rt_proc_lookup((int)pid_or_tid);
    if (!p)
        return -3;
    p->sig_pending |= 1u << sig;
    p->sig_val[sig] = value;
    sig_wake_task(p->leader);
    return 0;
}

/* 站38:sigtimedwait v0 轮询面(阻塞全弧=后弧):当前任务的
 * 任务级/进程级待决命中即耗并返号;未决=-(11) EAGAIN
 * (ticks v0 不消费:POSIX 语义=0 立返,阻塞后弧记档)。 */
long rt_sig_timedwait(u32 sig, u64 ticks)
{
    struct rt_proc *p = rt_cur->proc;

    (void)ticks;
    if (sig == 0 || sig >= RT_SIG_MAX)
        return -22;
    if (!p)
        return -22;
    if (rt_cur->sig_pending & (1u << sig)) {
        rt_cur->sig_pending &= ~(1u << sig);
        rt_cur->sig_val = 0;
        return (long)sig;
    }
    if ((p->sig_pending & ~p->sig_blocked) & (1u << sig)) {
        p->sig_pending &= ~(1u << sig);
        p->sig_val[sig] = 0;
        return (long)sig;
    }
    return -11;
}

/* 投递(syscall 出口,dispatch 尾调):最低位号一次一个,余者
 * 挂起留下轮(每出口清一笔,不丢)。帧手术:整帧存 sig_saved+
 * elr=handler/x0=sig;SP_EL0 不动(handler 与被打断者共用户栈);
 * x1-x30 未定义(裸 ABI 面,musl 到位后走 siginfo/restorer 契约)。 */
int rt_sig_deliver(u64 *frame)
{
    struct rt_proc *p = rt_cur->proc;
    u32 pend, sig;
    u64 val, rest;
    int taskface = 0;

    if (!p || p->sig_active)
        return 0;                       /* 在役:挂起(不丢) */
    pend = p->sig_pending & ~p->sig_blocked;
    /* 站38:任务级待决先于进程面(tgkill 定向优先) */
    if (rt_cur->sig_pending & ~p->sig_blocked) {
        pend = rt_cur->sig_pending & ~p->sig_blocked;
        taskface = 1;
    }
    if (!pend)
        return 0;
    sig = 1;
    while (sig < RT_SIG_MAX && !(pend & (1u << sig)))
        sig++;
    if (taskface) {
        rt_cur->sig_pending &= ~(1u << sig);
        val = rt_cur->sig_val;
    } else {
        p->sig_pending &= ~(1u << sig);
        val = p->sig_val[sig];
    }
    if (!p->sig_hand[sig])
        return (int)(128 + sig);        /* 站38:默认动作=终局
                                         * (POSIX 面;返退出码由
                                         * dispatcher 走 exit 链;
                                         * SIGCHLD 类忽略面=后弧) */
    {
        u32 i;

        for (i = 0; i < 34; i++)        /* 272B 紧帧整存 */
            p->sig_saved[i] = frame[i];
    }
    /* SP_EL0 驻 t->usp(硬件域,不在 272B 帧):一并存——handler
     * 在中断 SP 下压自家帧,其 epilogue 被 sigreturn 的 eret 跳过,
     * 不还原 SP=用户栈错窗读局部(冒烟:子三元打出 shell_line
     * 序言预存槽里的别家字面;反汇编:[sp+720]=wake-ok 槽定谳) */
    p->sig_saved_usp = rt_cur->usp;
    frame[0] = p->sig_hand[sig];        /* elr=handler */
    frame[2] = sig;                     /* x0 槽@16=sig 号 */
    frame[4] = val;                    /* 站38:x1 槽@32=sigval */
    rest = p->sig_rest[sig];
    if (rest)
        frame[33] = rest;              /* 站38:x30 槽@264=SA_RESTORER:
                                        * handler ret 落点(其内自调
                                        * rt_sigreturn;0=旧契约=handler
                                        * 末尾显式 rt_sigreturn,两立) */
    p->sig_active = 1;
    return 0;
}

/* sigreturn:整帧回拷;分发器 FR(0) 返回值回写在调用后——
 * 同值回写保持恢复帧 x0(exec_setup 返 a0 同律,一字两用)。 */
long rt_sig_return(u64 *frame)
{
    struct rt_proc *p = rt_cur->proc;
    long x0;
    u32 i;

    if (!p || !p->sig_active)
        return -22;                     /* 野 sigreturn:诚实拒 */
    for (i = 0; i < 34; i++)
        frame[i] = p->sig_saved[i];
    rt_cur->usp = p->sig_saved_usp;     /* SP_EL0 还原(见投递注) */
    p->sig_active = 0;
    x0 = (long)frame[2];
    return x0;
}

/* fork 继承:handler/blocked 继承(Linux 同律),pending 不继承
 * (子进程出生无欠账),sig_active/sig_saved 零(创建期 as_zero
 * 已清,显式再清=自证面)。 */
void rt_sig_fork(struct rt_proc *pp, struct rt_proc *cp)
{
    u32 i;

    for (i = 0; i < RT_SIG_MAX; i++) {
        cp->sig_hand[i] = pp->sig_hand[i];
        cp->sig_rest[i] = pp->sig_rest[i];  /* 站38:restorer 随
                                             * handler 面(fork 继承
                                             * =action 全组,Linux 律) */
    }
    cp->sig_blocked = pp->sig_blocked;
    cp->sig_pending = 0;
    cp->sig_active = 0;
}

/* ---- 扑克任务:代演同址空间第二执行流 ----
 * 真 threads(clone)=站 33+;扑克=机制级替身:只做队列唤醒
 * (WAKE 不读用户字),不碰用户 VA(内核任务跑内核 mm,碰不到
 * 用户窗=站 29 分区)。单发串行契约:静态一槽,demo 等待返回
 * 后再发(双发并发=站 33 threads 的题)。 */
struct rt_fx_poke {
    struct rt_proc *proc;
    u64 va;
    u64 delay;
};

static void fx_poker(void *arg)
{
    struct rt_fx_poke *pk = arg;

    rt_sleep_ticks(pk->delay);
    rt_futex_wake_key(pk->proc, pk->va, 1);
}

void rt_futex_poke(struct rt_proc *p, u64 va, u64 delay)
{
    static struct rt_fx_poke poke;

    poke.proc = p;
    poke.va = va;
    poke.delay = delay;
    if (!rt_task_create("fxpoke", fx_poker, &poke, 9))
        rt_puts("futex: poke spawn failed\n");
}
