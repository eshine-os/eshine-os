/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "rtos.h"

/* 站三十五:线程——多执行体纵队的第一柱。musl 1.2.5 aarch64
 * 实测契约(clone.s/pthread_create.c/pthread_join.c):
 *
 *   __clone(func,stack,flags,arg,ptid,tls,ctid)
 *     -> SYS_clone(220, flags, stack, ptid, tls, ctid)
 *   子:帧整拷+**x0=0**+SP=传入栈(musl 父已把 func/arg 压其上),
 *       回到 svc 后继指令,func 返回即 SYS_exit(93);
 *   join=纯用户态 futex(等 pthread 结构 detach_state),内核只须
 *       兑现 CHILD_CLEARTID:线程退出时 *ctid=0+futex 唤醒
 *       (musl 传 &__thread_list_lock,持锁死线程的释放面,Linux
 *       同语义);
 *   PARENT_SETTID:父语境写 tid 到 *ptid(在 rt_syscall 包装层,
 *       access_ok 纪律同站 28);
 *   SETTLS:新任务 tpidr 初值(vec.S 簿记每切换存还 TPIDR_EL0;
 *       musl 主线程自写——aarch64 set_thread_area=用户态 msr)。
 *
 * 线程=同 proc 的 EL0 任务:共享地址空间(同 ttbr)/fd 表(同
 * rt_proc.fds,无 DUP)/信号 handler 面(fork 组拷已就绪);
 * 退场语义二分:非 leader=纯任务死(proc 不动,wait4 只认
 * leader);leader(或 exit_group)退场=清扫线程群再走既有进程
 * 退场链(孤儿 futex 持有者不悬空——组清扫摘 pend 队+摘 rdy)。
 *
 * v0 边界(spec §4):pending 仍 proc 级位图;futex PI/robust 不做
 * (musl 默认 mutex 纯 WAIT/WAKE);线程全核 0 亲和(无跨核迁移);
 * 非 CLONE_THREAD 的 clone 族拒;tid=发号器(0=非法,leader=pid)。 */

static u64 thr_tid_ctr;         /* 发号器(0x100 起,免与 pid 混) */

long sys_clone_impl(u64 *frame, u32 flags, u64 stack, u64 ptid,
                    u64 tls, u64 ctid)
{
    struct rt_proc *p = rt_cur->proc;
    rt_task_t *ct;
    u64 tid;
    u32 i;

    (void)ptid;                     /* PARENT_SETTID 写在包装层 */
    if (!p)
        return -22;
    if ((flags & RT_CLONE_NEEDS) != RT_CLONE_NEEDS ||
        (flags & ~RT_CLONE_ALLOW))
        return -22;                 /* v0:只认线程族旗集 */
    if (!stack || (stack & 15u))
        return -22;                 /* AAPCS64:SP 16 对齐 */
    ct = rt_task_create_el0("thr", NULL, NULL, rt_cur->prio,
                            (void *)(unsigned long)stack);
    if (!ct) {
        return -11;                 /* 池满 */
    }
    {   /* 帧整拷(fork 同款)+x0=0:子回 svc 后继指令零号返 */
        u64 *dst = (u64 *)(void *)ct->sp;

        for (i = 0; i < 272 / 8; i++)
            dst[i] = frame[i];
        dst[2] = 0;                 /* x0 槽@16 */
    }
    ct->usp = stack;                /* musl 父压的 func/arg 在栈顶 */
    ct->proc = p;                   /* 共享:同 AS/fd/handler 面 */
    ct->ttbr = rt_proc_ttbr(p);
    ct->tpidr_el0 = (flags & RT_CLONE_SETTLS) ? tls : 0;
    ct->ctid = (flags & RT_CLONE_CHILD_CLEARTID) ? ctid : 0;
    ct->sig_pending = 0;             /* 站38:任务级信号面,新槽必清 */
    ct->sig_val = 0;
    tid = ++thr_tid_ctr + 0x100;
    ct->tid = tid;
    return (long)tid;
}

/* 线程退场(rt_syscall.c sys_exit 的非 leader 支):CLEARTID 兑现
 * =本人语境写 *ctid=0+futex 唤醒(地址已过包装层校验,同 proc
 * mm),然后纯任务死。noreturn。 */
void rt_thr_exit(void)
{
    struct rt_proc *p = rt_cur->proc;

    if (p && rt_cur->ctid) {
        *(u64 *)(unsigned long)rt_cur->ctid = 0;
        rt_futex_wake_key(p, rt_cur->ctid, 1);
    }
    rt_task_exit();                 /* noreturn:DEAD+切走 */
    __builtin_unreachable();
}

/* leader 退场前的线程群清扫:孤儿 futex/sem 持有者不悬空——
 * 摘 pend 队(rt_timeout_unpend 同站 32 信号唤醒同律)+清
 * wait_mask+READY 者摘 rdy+置 DEAD。单核亲和下无人正在别核
 * 跑(leader 退场时线程必不在 RUN);DEAD 槽由 create 复用面
 * (entry 非空判)接管。 */
void rt_thr_group_kill(struct rt_proc *p)
{
    unsigned int idx;
    rt_task_t *t;

    if (!p)
        return;
    for (t = rt_ps_first(&idx); t; t = rt_ps_next(&idx)) {
        if (t == p->leader || t->proc != p || t->state == RT_DEAD)
            continue;
        if (t->state == RT_READY)
            rdy_remove(t);
        if (t->pend_qh)
            rt_timeout_unpend(t);  /* futex/sem 等待队自摘 */
        t->pend_deadline = 0;
        t->wait_mask = 0;
        t->state = RT_DEAD;
    }
}
