/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "rtos.h"
#include "miniuart.h"
#include "hvc.h"
#include "kernel/include/version.h"

/* 站二十八:syscall 分发+内核镜像命令蹦床。
 *
 * 帧契约(vec.S guest_el0_sync):传入帧=内核栈上的用户上下文,
 * [0]=elr,[1]=psr,x_i 槽字节偏移 16+16i(与 RT_PUSH_GPRS 严格
 * 镜像);x8=号,x0-x5=参,返回值写 x0 槽,eret 即达用户。
 *
 * ABI=Linux arm64 标准(偏差 #27:对参考实现七参走结构体指针);
 * 号面:asm-generic 原样(read/write/getpid)+eshine 扩展 512 起;
 * 静态表 [ES_NR_MAX] 全填 ni 后覆写(参考实现分发形态,实现分文件
 * ——本仓按面分文件即本文件+后续站各面文件,不蹈其 5456 行
 * 单文件审计债)。
 *
 * 蹦床(sys_demo):EL0 壳让内核语境跑既有命令体,80 门判据逐
 * 字节不变(体从 rt_shell.c 原样移植);30+ 各面 syscall 化后
 * 逐批退役。uaccess=access_ok 用户窗校验+直接拷贝(v0 单窗静态
 * 映射无缺页,cortex-a57 无 PAN,30+ 按需上异常表)。 */

/* x_i 槽(字节偏移,帧基=elr 槽):RT_PUSH_GPRS 是紧打包——
 * stp 双寄存器 16B/对无 hole,仅 x0 的 str 带 8B hole(站16
 * rt_frame_init 注释自证:"x1 槽在 +32,x0 槽后带 8B hole")。
 * 布局(字节):elr@0 psr@8 x0@16 hole@24 x_i@24+8i(i>=1)。
 * 站28 冒烟定谳:曾按 u64 16B 步长错读(FR(8) 落 x15 槽),壳的
 * write/read 全打 sys_ni 返 -ENOSYS——静默提示符+每秒百万次
 * ENOSYS SVC 风暴一案全解。 */
#define FR(i) (*(u64 *)((char *)frame + ((i) == 0 ? 16 : 24 + 8 * (i))))

#define ES_EFAULT 14

#define UWIN_LO 0x40200000UL
#define UWIN_HI 0x41000000UL

static int access_ok(const void *p, u64 len)
{
    u64 a = (u64)p;

    return a >= UWIN_LO && len <= UWIN_HI - UWIN_LO && a <= UWIN_HI - len;
}

/* ---------------- 标准面 ---------------- */

static long sys_read(u64 *frame)
{
    char *ub = (char *)FR(1);
    long c;

    if ((long)FR(0) >= 3) {          /* 站40:真 fd 读(busybox 面专用
                                      * fd>=3;EOF=0 让 cat/md5 收口) */
        char kbuf[64];
        long r = rt_fd_read(rt_cur->proc, (int)FR(0), kbuf, sizeof(kbuf));

        if (r <= 0)
            return r;
        if (!access_ok(ub, (u32)r))
            return -ES_EFAULT;
        for (long i = 0; i < r; i++)
            ub[i] = kbuf[i];
        return r;
    }
    for (;;) {
        c = rt_getc_nb();
        if (c >= 0)
            break;
        rt_block_rx();               /* 站17 机器原样:DAIF 窗+RT_RX_BIT */
    }
    ub[0] = (char)c;
    return 1;
}

static long sys_write(u64 *frame)
{
    const char *ub = (const char *)FR(1);
    u64 len = FR(2);
    char kbuf[256];

    if (len > sizeof(kbuf))
        len = sizeof(kbuf);
    if (len && !access_ok(ub, len))
        return -ES_EFAULT;
    for (u64 i = 0; i < len; i++)
        kbuf[i] = ub[i];
    if ((long)FR(0) >= 3)            /* 站40:真 fd 写(同 sys_read 路由律) */
        return rt_fd_write(rt_cur->proc, (int)FR(0), kbuf, (u32)len);
    rt_puts_n(kbuf, (u32)len);
    return (long)len;
}

static long sys_getpid(u64 *frame)
{
    (void)frame;
    return (long)rt_pid_of(rt_cur);
}

static long sys_sysinfo(u64 *frame)
{
    struct rt_sysinfo *ub = (struct rt_sysinfo *)FR(0);
    u64 len = FR(1);
    struct rt_sysinfo si;
    u32 i;

    if (len < sizeof(si))
        return -ES_EINVAL;
    if (!access_ok(ub, sizeof(si)))
        return -ES_EFAULT;
    for (i = 0; i + 1 < sizeof(si.ver) && ESHINE_VERSION_STR[i]; i++)
        si.ver[i] = ESHINE_VERSION_STR[i];
    for (; i < sizeof(si.ver); i++)
        si.ver[i] = '\0';
    si.tick = rt_ticks;
    si.el0 = 1;
    si.rsv = 0;
    *ub = si;
    return 0;
}

/* ---------------- eshine 扩展面 ---------------- */

/* 站二十九:进程+地址空间六面(机制形状参考实现 §7/§8;号面见
 * rtos.h——exit/mmap/munmap/wait4 标准, fork/exec_setup 扩展)。 */

#define ES_EAGAIN 11
#define ES_ENOMEM 12

/* ---- 站四十二(前置体):posix timer 全局池+robust 链收尾 ----
 * 池驻内核(8 槽),rt_ptimer.exit 面在 sys_exit 死者本人语境先于
 * 地址空间拆除跑(tick 回调持有 owner 指针=收件人死后必先摘池)。 */
static struct rt_ptimer ptimer_pool[8];

/* 站42:posix timer 池=tp 守卫的合法第二驻地(站 28 守卫谓词
 * 原只认任务池槽——vtimer 时代唯一合法居所;posix timer 的
 * rt_timer_t 内嵌于本池槽头,守卫须放行,真野值照抓不变)。 */
int rt_ptimer_in_pool(void *tm)
{
    u64 v = (u64)tm;

    return v >= (u64)ptimer_pool &&
           v < (u64)(ptimer_pool + 8) &&
           (v - (u64)ptimer_pool) % sizeof(struct rt_ptimer) == 0;
}

static void ptimer_fire(void *arg)
{
    struct rt_ptimer *pt = arg;
    struct rt_proc *p = pt->owner;
    rt_task_t *t;
    unsigned int idx;

    if (!p || !p->used || p->state == RT_PROC_ZOMBIE) {
        rt_timer_stop(&pt->tm);       /* 收件人先死:停摆即安 */
        return;
    }
    if (pt->target_tid) {
        for (t = rt_ps_first(&idx); t; t = rt_ps_next(&idx))
            if (t->state != RT_FREE && t->state != RT_DEAD &&
                t->tid && t->tid == pt->target_tid)
                break;
    } else {
        t = p->leader;
    }
    if (!t)
        return;
    if (t->sig_pending & (1u << pt->signo)) {
        pt->overrun++;                /* 上发未收:overrun 记账(POSIX 面) */
        return;
    }
    rt_sig_queueinfo_ctx((long)(pt->target_tid ? pt->target_tid : t->tid),
                         (u32)pt->signo, pt->value);
}

static void ptimer_exit_all(struct rt_proc *p)
{
    u32 i;

    for (i = 0; i < 8; i++)
        if (ptimer_pool[i].owner == p) {
            rt_timer_stop(&ptimer_pool[i].tm);
            ptimer_pool[i].owner = NULL;
        }
}

/* robust futex 链收尾(站42 v0):持锁者死=遍历注册链逐节点唤醒。
 * musl 律:节点基址=锁字(futex offset 0),next@+0;野环保险丝=
 * 上限 100 跳+用户窗范围门(链数据在死者自己地址空间,exit 语境
 * 页表未拆=直读合法)。 */
static void robust_exit_walk(struct rt_proc *p)
{
    u64 node = p->robust_head;
    u32 guard = 0;

    while (node && guard++ < 100) {
        if (node < 0x40200000ULL || node >= 0x41200000ULL)
            break;
        rt_futex_wake_key(p, node, 1);
        node = *(u64 *)(void *)node;
    }
}

static long sys_exit(u64 *frame)
{
    long code = (long)FR(0);
    struct rt_proc *p = rt_cur->proc;

    if (p) {
        if (p->leader != rt_cur) {
            /* 站35:线程退场=纯任务死(proc/wait4 不动);CLEARTID
             * 兑现(*ctid=0+futex 唤醒)=musl join/thread_list 锁面 */
            rt_thr_exit();
            __builtin_unreachable();
        }
        /* 站31:逐 fd CLOSE(服务器 ocb 引用减+断连)——死亡关流;
         * rt_cur=死者本人,阻塞往返合法(仅此处调,ISR 无涉) */
        /* 站42:先 robust 链唤醒(锁面)与 posix timer 摘池
         * (tick 回调持 owner 指针)——都先于地址空间拆除。 */
        robust_exit_walk(p);
        ptimer_exit_all(p);
        rt_fd_close_all(p);
        rt_thr_group_kill(p);             /* 站35:线程群清扫(孤儿不悬空) */
        rt_proc_exit(p, (int)code);       /* 页池回收+L2 清+TLB 冲 */
        rt_task_wake_all(RT_PROC_BIT);    /* 父 wait4 的收尸唤醒 */
    }
    rt_task_exit();                      /* 任务 DEAD+切走,noreturn */
    __builtin_unreachable();
}

static long sys_mmap(u64 *frame)
{
    u64 hint = FR(0);                    /* 站33:窗内合法 hint 落位 */
    u64 len = FR(1);
    u64 va;

    if (!rt_cur->proc)
        return -ES_EINVAL;
    if (!(rt_cur->proc->caps & CAP_MM))
        return -1;                     /* 站41:CAP_MM fail-closed */
    va = rt_as_mmap(rt_cur->proc, hint, len);
    return va ? (long)va : -ES_ENOMEM;
}

static long sys_munmap(u64 *frame)
{
    if (!rt_cur->proc ||
        rt_as_munmap(rt_cur->proc, FR(0), FR(1)) < 0)
        return -ES_EINVAL;
    return 0;
}

static long sys_wait4(u64 *frame)
{
    long pid = (long)FR(0);
    u64 *ust = (u64 *)FR(1);
    struct rt_proc *z;
    int code = 0;

    if (pid <= 0)
        return -ES_EINVAL;
    if (FR(2) & 1u) {                 /* 站40:WNOHANG(respawn 探询面) */
        z = rt_proc_zombie_find((int)pid, &code);
        if (!z) {
            if (rt_proc_child_alive(rt_cur->proc, (int)pid) > 0)
                return 0;             /* 活着:0=未收(POSIX 面) */
            return -10;               /* -ECHILD */
        }
        rt_proc_reap(z);
        if (ust) {
            if (!access_ok(ust, 8))
                return -ES_EFAULT;
            *ust = (u64)(u32)code;
        }
        return pid;
    }
    for (;;) {
        z = rt_proc_zombie_find((int)pid, &code);
        if (z)
            break;
        /* 站40 定谳(七轮全息画像):阻塞路径从无 ECHILD 路——
         * 已收/非子 pid 永挂(sync respawn 首撞;站 29 v0 子集
         * 无人走此路)。POSIX 正身:非活非尸即 -ECHILD 即返。 */
        if (rt_proc_child_alive(rt_cur->proc, (int)pid) <= 0)
            return -10;
        /* 站17 纪律同 rt_block_rx:判-挂-摘同入 DAIF 屏蔽窗,
         * 窗内终判已 ZOMBIE 则不挂(零窗竞);窗后挂起已可见,
         * 子 exit 的广播唤醒走回流令牌兑付。 */
        {
            u64 d = rt_daif_save();

            if (rt_proc_zombie_find((int)pid, &code))
                break;
            rt_cur->wait_mask = RT_PROC_BIT;
            rt_cur->state = RT_BLOCK;
            rdy_remove(rt_cur);
            rt_daif_load(d);
        }
        rt_sched();
    }
    rt_proc_reap(z);
    if (ust) {
        if (!access_ok(ust, 8))
            return -ES_EFAULT;
        *ust = (u64)(u32)code;
    }
    return pid;
}

/* COPY_UCONTEXT(参考实现 fork 形状):子进程 eager 全拷+子任务帧整拷
 * (272B 紧布局),x0=魔数辨 child。协作式无抢占窗,帧覆写与子首
 * 派发之间零竞(父 prio 低于子,父续跑到显式让位)。 */
static long sys_fork(u64 *frame)
{
    struct rt_proc *pp = rt_cur->proc, *cp;
    rt_task_t *ct;

    if (!pp)
        return -ES_EINVAL;
    if (!(pp->caps & CAP_SPAWN))
        return -1;                     /* 站41:CAP_SPAWN fail-closed */
    if (rt_proc_child_count(pp) >= pp->max_child)
        return -ES_EAGAIN;             /* 站41:rlimit fork 满额 */
    if (rt_proc_create(NULL, &cp) < 0)
        return -ES_EAGAIN;
    if (rt_proc_fork_as(pp, cp) < 0) {
        rt_proc_reap(cp);                /* 表无映射,槽回收不伤人 */
        return -ES_ENOMEM;
    }
    cp->state = RT_PROC_RUN;
    cp->parent = pp->pid;
    rt_fd_fork(pp, cp);                /* 站31:fdtable 组拷+双侧引用 */
    rt_sig_fork(pp, cp);               /* 站32:handler/blocked 继承 */
    ct = rt_task_create_el0("child", NULL, NULL, rt_cur->prio + 1,
                            (void *)rt_cur->usp);
    if (!ct) {
        rt_proc_reap(cp);
        return -ES_EAGAIN;
    }
    {   /* 帧整拷+魔数;任务 sp=帧顶契约(@0)直取 */
        u64 *dst = (u64 *)(void *)ct->sp;
        u32 i;

        for (i = 0; i < 272 / 8; i++)
            dst[i] = frame[i];
        dst[2] = RT_FORK_MAGIC;          /* x0 槽@16 */
    }
    ct->usp = rt_cur->usp;
    ct->proc = cp;
    ct->ttbr = rt_proc_ttbr(cp);
    ct->tid = (u64)cp->pid;           /* 站35:leader tid=pid */
    cp->leader = ct;
    return (long)cp->pid;
}

static long sys_exec_setup(u64 *frame)
{
    /* 帧改写(参考实现 EXEC_SETUP 形状,内核不读任何镜像字节):
     * v0 入参 x0=entry x1=sp x2=arg;x3-x5 不读(壳 wrapper 只
     * 3 参;impl 侧整帧卫生清零,残值不进新帧)。 */
    if (rt_cur->proc) {
        rt_proc_t *p = rt_cur->proc;
        u32 i;

        /* 站42:CLOEXEC 摘除(exec 语境逐 fd:位在者关流断连;
         * 死者本人在跑,阻塞往返合法同 sys_exit 律)。 */
        for (i = 0; i < RT_FD_MAX; i++)
            if (p->fds[i].used && p->fds[i].cloexec)
                rt_fd_close(p, (int)i);
        p->brk = p->mmap_next;
        /* 站40:堆种子=装像末(窗内游标;brk(0)=读,brk(a)=增量
         * hint-mmap——musl mallocng 探测面,qemu-user 实测定谳) */
    }
    return sys_exec_setup_impl(frame, FR(0), FR(1), FR(2), 0, 0, 0);
}

/* 站三十:消息 IPC 面(内核任务直调 rt_msg 层,本面=EL0 客户端
 * 通道;uaccess 纪律=access_ok+拷贝,同站 28) */
#define ES_EEXIST 17
#define ES_ESRCH   3

static int copy_in_str(char *dst, u64 uptr, u32 max)
{
    u32 i = 0;

    if (!access_ok((const void *)uptr, max))
        return -1;
    for (; i + 1 < max; i++) {
        dst[i] = ((const char *)uptr)[i];
        if (!dst[i])
            return 0;
    }
    dst[max - 1] = '\0';
    return 0;
}

static long sys_ch_create(u64 *frame)
{
    return rt_msg_ch_create((u32)FR(0));
}

static long sys_ch_destroy(u64 *frame)
{
    return rt_msg_ch_destroy((int)FR(0));
}

static long sys_name_attach(u64 *frame)
{
    char kpath[48];

    if (copy_in_str(kpath, FR(0), sizeof(kpath)) < 0)
        return -ES_EFAULT;
    return rt_msg_name_attach(kpath, (int)FR(1));
}

static long sys_name_detach(u64 *frame)
{
    char kpath[48];

    if (copy_in_str(kpath, FR(0), sizeof(kpath)) < 0)
        return -ES_EFAULT;
    return rt_msg_name_detach(kpath);
}

static long sys_name_open(u64 *frame)
{
    char kpath[48];

    if (copy_in_str(kpath, FR(0), sizeof(kpath)) < 0)
        return -ES_EFAULT;
    return rt_msg_name_open(kpath);
}

static long sys_conn_detach(u64 *frame)
{
    return rt_msg_conn_detach((int)FR(0));
}

static long sys_msg_send(u64 *frame)
{
    u64 sbuf = FR(1), rbuf = FR(3);
    u32 slen = (u32)FR(2), rlen = (u32)FR(4);

    if (slen && !access_ok((const void *)sbuf, slen))
        return -ES_EFAULT;
    if (rlen && !access_ok((void *)rbuf, rlen))
        return -ES_EFAULT;
    return rt_msg_send((int)FR(0), (const void *)sbuf, slen,
                       (void *)rbuf, rlen, FR(5));
}

static long sys_msg_recv(u64 *frame)
{
    u64 buf = FR(1);
    u32 buflen = (u32)FR(2);
    struct rt_recvinfo ki, *uinfo = (struct rt_recvinfo *)FR(4);
    long r;

    if (buflen && !access_ok((void *)buf, buflen))
        return -ES_EFAULT;
    if (!access_ok(uinfo, sizeof(ki)))
        return -ES_EFAULT;
    r = rt_msg_recv((int)FR(0), (void *)buf, buflen, FR(3), &ki);
    if (r == 0)
        *uinfo = ki;
    return r;
}

static long sys_msg_reply(u64 *frame)
{
    u64 rbuf = FR(2);
    u32 rlen = (u32)FR(3);

    if (rlen && !access_ok((const void *)rbuf, rlen))
        return -ES_EFAULT;
    return rt_msg_reply((int)FR(0), FR(1), (const void *)rbuf, rlen,
                        (long)FR(4));
}

static long sys_msg_pulse(u64 *frame)
{
    return rt_msg_pulse((int)FR(0), (s64)FR(1), FR(2));
}

/* 站三十一:fd=通道三元组面(EL0 壳->fd 层->三段消息->resmgr
 * 服务器;uaccess 纪律同站 28/30:access_ok+拷贝)。 */
static long sys_fdopen(u64 *frame)
{
    char kpath[48];
    struct rt_proc *p = rt_cur->proc;

    if (!p)
        return -ES_EINVAL;
    if (copy_in_str(kpath, FR(0), sizeof(kpath)) < 0)
        return -ES_EFAULT;
    return rt_fd_open(p, kpath);
}

/* ---- 站36:shm 段四面(值参数,无 uaccess;共享窗落位=固定 VA) */
static long sys_shm_create(u64 *frame)
{
    if (!rt_cur->proc)
        return -ES_EINVAL;
    if (!(rt_cur->proc->caps & CAP_MM))
        return -1;                     /* 站41:CAP_MM fail-closed */
    return rt_shm_create((u32)FR(0));
}

static long sys_shm_attach(u64 *frame)
{
    if (!rt_cur->proc)
        return -ES_EINVAL;
    return rt_shm_attach(rt_cur->proc, (long)FR(0), FR(1));
}

static long sys_shm_detach(u64 *frame)
{
    if (!rt_cur->proc)
        return -ES_EINVAL;
    return rt_shm_detach(rt_cur->proc, (long)FR(0));
}

static long sys_shm_destroy(u64 *frame)
{
    return rt_shm_destroy((long)FR(0));
}

/* ---- 站39:FS 深面(建/摘/枚举/whence) ---- */
static long sys_fdcreate(u64 *frame)
{
    char kpath[48];

    if (!rt_cur->proc)
        return -ES_EINVAL;
    if (copy_in_str(kpath, FR(0), sizeof(kpath)) < 0)
        return -ES_EFAULT;
    return rt_fd_create(rt_cur->proc, kpath);
}

static long sys_fdunlink(u64 *frame)
{
    char kpath[48];

    if (!rt_cur->proc)
        return -ES_EINVAL;
    if (copy_in_str(kpath, FR(0), sizeof(kpath)) < 0)
        return -ES_EFAULT;
    return rt_fd_unlink(rt_cur->proc, kpath);
}

static long sys_fddir(u64 *frame)
{
    char kpath[48];
    long r;
    u32 z;

    if (!rt_cur->proc)
        return -ES_EINVAL;
    if (!access_ok((void *)FR(2), (u32)FR(3)))
        return -ES_EFAULT;
    for (z = 0; z < sizeof(kpath); z++)
        kpath[z] = 0;
    r = rt_fd_dir(rt_cur->proc, (int)FR(0), kpath, sizeof(kpath));
    {
        u32 i;
        char *ub = (char *)FR(2);       /* kpath 整窗出(调用者截串) */

        for (i = 0; i < sizeof(kpath); i++)
            ub[i] = kpath[i];
    }
    return r;
}

/* ---- 站37:posix_queue 弧(mqueue/pipe/MsgRead 镜像) ---- */
static long sys_mq_open(u64 *frame)
{
    char kpath[32];

    if (!rt_cur->proc)
        return -ES_EINVAL;
    if (!(rt_cur->proc->caps & CAP_MM))
        return -1;                     /* 站41:CAP_MM fail-closed */
    if (copy_in_str(kpath, FR(0), sizeof(kpath)) < 0)
        return -ES_EFAULT;
    return rt_mq_open(kpath);
}

static long sys_mq_send(u64 *frame)
{
    return rt_mq_send((long)FR(0), FR(1), (u32)FR(2));
}

static long sys_mq_receive(u64 *frame)
{
    u64 v = 0;
    long r;

    if (!access_ok((void *)FR(0), sizeof(v)))
        return -ES_EFAULT;
    r = rt_mq_receive((long)FR(1), &v, FR(2));
    if (r == 0)
        *(u64 *)FR(0) = v;           /* 值面直写用户字(已 access_ok) */
    return r;
}

static long sys_mq_close(u64 *frame)
{
    return rt_mq_close((long)FR(0));
}

static long sys_mq_unlink(u64 *frame)
{
    char kpath[32];

    if (copy_in_str(kpath, FR(0), sizeof(kpath)) < 0)
        return -ES_EFAULT;
    return rt_mq_unlink(kpath);
}

static long sys_msg_read(u64 *frame)
{
    if (!access_ok((void *)FR(3), (u32)FR(4)))
        return -ES_EFAULT;
    return rt_msg_read((int)FR(0), FR(1), FR(2),
                       (void *)FR(3), (u32)FR(4));
}

static long sys_pipe(u64 *frame)
{
    long r;

    (void)frame;                    /* 站50:wire 面 r0/r1 出参,frame
                                     * 不入参——存量警告清账(站41 教训
                                     * "警告在,没人读"再犯即抓) */
    if (!rt_cur->proc)
        return -ES_EINVAL;
    if (!(rt_cur->proc->caps & CAP_MM))
        return -1;                     /* 站41:CAP_MM fail-closed */
    r = rt_pipe_create(rt_cur->proc);
    if (r < 0)
        return r;
    return r;                        /* (fd0 | fd1<<16) 打包 */
}

static long sys_fdclose(u64 *frame)
{
    if (!rt_cur->proc)
        return -ES_EINVAL;
    return rt_fd_close(rt_cur->proc, (int)FR(0));
}

static long sys_fdread(u64 *frame)
{
    char *ub = (char *)FR(1);
    u32 len = (u32)FR(2);
    char kbuf[64];
    long r;

    if (!rt_cur->proc)
        return -ES_EINVAL;
    if (len > sizeof(kbuf))
        len = sizeof(kbuf);
    if (len && !access_ok(ub, len))
        return -ES_EFAULT;
    r = rt_fd_read(rt_cur->proc, (int)FR(0), kbuf, len);
    if (r > 0) {
        for (u32 i = 0; i < (u32)r; i++)
            ub[i] = kbuf[i];
    }
    return r;
}

static long sys_fdwrite(u64 *frame)
{
    const char *ub = (const char *)FR(1);
    u32 len = (u32)FR(2);
    char kbuf[44];

    if (!rt_cur->proc)
        return -ES_EINVAL;
    if (len > sizeof(kbuf))
        len = sizeof(kbuf);
    if (len && !access_ok(ub, len))
        return -ES_EFAULT;
    for (u32 i = 0; i < len; i++)
        kbuf[i] = ub[i];
    return rt_fd_write(rt_cur->proc, (int)FR(0), kbuf, len);
}

/* 站三十二:signal+futex 面(真 Linux 号 98/129/134/139,站 33
 * musl 直连;v0 偏差=futex 四参/sigaction 两参,spec §2 记档)。 */
static long sys_futex(u64 *frame)
{
    u64 uaddr = FR(0);
    u32 op = (u32)FR(1) & ~RT_FUTEX_PRIVATE_FLAG;
    u64 tmo = FR(3);
    u64 ticks = RT_WAIT_FOREVER;

    /* 站35 契约(musl 实测):op 恒带 PRIVATE(128)掩掉=同 proc
     * 提示;WAIT 的 x3:NULL=无限等(旧"0=禁等探针"撞 musl join
     * 假 EINVAL 速回),指针=timespec{sec,nsec} 相对时长折 ticks
     * (sec*RT_HZ+nsec/(1e9/RT_HZ);折 0=立即到点 -ETIMEDOUT);
     * WAIT_BITSET(9)是 CLOCK_REALTIME 绝对面——无墙钟,v0 相对
     * 降格(spec §4 记档,demo 不依赖绝对语义)。WAKE 不读字免校。 */
    if (!rt_cur->proc)
        return -ES_EINVAL;
    if (op == RT_FUTEX_WAIT_BITSET)
        op = RT_FUTEX_WAIT;
    if (op == RT_FUTEX_WAIT) {
        if (!access_ok((const void *)uaddr, 4))
            return -ES_EFAULT;
        if (tmo) {
            u64 sec, nsec;

            if (!access_ok((const void *)tmo, 16))
                return -ES_EFAULT;
            sec = *(const u64 *)tmo;
            nsec = *(const u64 *)(tmo + 8);
            ticks = sec * RT_HZ + nsec / (1000000000ULL / RT_HZ);
            if (!ticks)
                return -110;          /* 零时长=立即到点 */
        }
    }
    return rt_futex(rt_cur->proc, uaddr, op, (u32)FR(2), ticks);
}

static long sys_kill(u64 *frame)
{
    if (rt_cur->proc && !(rt_cur->proc->caps & CAP_KILL))
        return -1;                     /* 站41:CAP_KILL fail-closed */
    return rt_sig_kill((int)FR(0), (u32)FR(1));
}

static long sys_sigaction(u64 *frame)
{
    /* 站38:4 参全脸(musl SA_RESTORER:handler+restorer 同装;
     * 站32 旧 2 参面=restorer 0=handler 末尾显式 rt_sigreturn) */
    if (!rt_cur->proc)
        return -ES_EINVAL;
    return rt_sig_action(rt_cur->proc, (u32)FR(0), FR(1), FR(2));
}

static long sys_sigqueueinfo(u64 *frame)
{
    if (rt_cur->proc && !(rt_cur->proc->caps & CAP_KILL))
        return -1;                     /* 站41:CAP_KILL fail-closed */
    return rt_sig_queueinfo((long)FR(0), (u32)FR(1), FR(2));
}

static long sys_sigtimedwait(u64 *frame)
{
    if (!rt_cur->proc)
        return -ES_EINVAL;
    return rt_sig_timedwait((u32)FR(0), FR(1));
}

static long sys_rt_sigreturn(u64 *frame)
{
    return rt_sig_return(frame);
}

/* 站三十三:musl 薄面(启动期实测需求;全真 Linux 号)。 */
/* 站59:readv(真号 65)——musl stdio 读面(fread/fscanf 等全走此)。
 * iov 逐项读:64B 内核窗分块(fd 层 wire 契约),短读=EOF/窗尽照实返。 */
static long sys_readv(u64 *frame)
{
    const u64 *iov = (const u64 *)FR(1);
    u64 cnt = FR(2);
    long total = 0;
    u32 i;

    if (!rt_cur->proc)
        return -ES_EINVAL;
    if (cnt > 8)
        return -ES_EINVAL;
    if (cnt && !access_ok(iov, cnt * 16))
        return -ES_EFAULT;
    for (i = 0; i < (u32)cnt; i++) {
        char *ub = (char *)iov[i * 2];
        u64 len = iov[i * 2 + 1];
        char kbuf[64];

        if (len == 0UL)
            continue;
        if (!access_ok(ub, len))
            return -ES_EFAULT;
        for (u64 off = 0; off < len; off += sizeof(kbuf)) {
            u64 m = len - off > sizeof(kbuf) ? sizeof(kbuf) : len - off;
            long r = rt_fd_read(rt_cur->proc, (int)FR(0), kbuf, (u32)m);

            if (r < 0)
                return total > 0 ? total : r;   /* 首项即错照传 errno */
            if (r == 0)
                return total;                   /* EOF */
            for (long k = 0; k < r; k++)
                ub[off + k] = kbuf[k];
            total += r;
            if ((u64)r < m)
                return total;      /* 短读:窗尽 */
        }
    }
    return total;
}

static long sys_writev(u64 *frame)
{
    /* musl stdio __stdout_write 走 writev(iov 链)=printf 真通路。
     * fd 1/2 走原始行打印面(与 sys_write 同律;fd 表的 resmgr
     * stdio=并发面,musl stdout 直挂原始面确定性优先)。
     * 站59:fd>=3 路由同 read/write 律(ztest 定谳:fwrite 落文件走
     * writev,首版无路由=文件数据进串口)。 */
    const u64 *iov = (const u64 *)FR(1);
    u64 cnt = FR(2);
    long total = 0;
    u32 i;

    if (cnt > 8)
        return -ES_EINVAL;
    if (cnt && !access_ok(iov, cnt * 16))
        return -ES_EFAULT;
    for (i = 0; i < (u32)cnt; i++) {
        const char *ub = (const char *)iov[i * 2];
        u64 len = iov[i * 2 + 1];
        char kbuf[256];

        if (len > sizeof(kbuf))
            len = sizeof(kbuf);
        if (len && !access_ok(ub, len))
            return -ES_EFAULT;
        for (u64 k = 0; k < len; k++)
            kbuf[k] = ub[k];
        if (len) {
            if ((long)FR(0) >= 3) {    /* 站59:真 fd 写(同 sys_write 律) */
                long r = rt_fd_write(rt_cur->proc, (int)FR(0), kbuf,
                                      (u32)len);

                if (r < 0)
                    return total > 0 ? total : r;
                total += r;
                if ((u32)r < (u32)len)
                    return total;      /* 短写:窗尽 */
            } else {
                rt_puts_n(kbuf, (u32)len);
                total += (long)len;
            }
        }
    }
    return total;
}

static long sys_ioctl(u64 *frame)
{
    /* isatty 探测恒"非 tty"(=-ENOTTY):musl 判 stdout 非 tty=
     * 全缓冲+exit 冲刷——输出时机确定(v0 判据面)。 */
    (void)frame;
    return -25;
}

static long sys_set_tid_address(u64 *frame)
{
    /* 站35:每任务 tid+clear_tid 地址(Linux 同位:线程退出时
     * 内核写 0+futex 唤醒;leader tid=pid 在 proc 建立时落)。 */
    rt_cur->ctid = FR(0);
    return (long)rt_cur->tid;
}

static long sys_rt_sigprocmask(u64 *frame)
{
    /* 站32 位图掩码的 syscall 兑现面:musl sigset=8B,v0 位图 32 位
     * (上 32 清零);how 面 POSIX 三态;old 先于 how 判出(POSIX:
     * how 非法=EINVAL,old 未定义——先判更严)。 */
    struct rt_proc *p = rt_cur->proc;
    u32 how = (u32)FR(0);
    const u64 *uset = (const u64 *)FR(1);
    u64 *uold = (u64 *)FR(2);

    if (!p)
        return -ES_EINVAL;
    if (uset) {
        if (how > 2)
            return -ES_EINVAL;
        if (!access_ok(uset, 8))
            return -ES_EFAULT;
    }
    if (uold) {
        if (!access_ok(uold, 8))
            return -ES_EFAULT;
        *uold = (u64)p->sig_blocked;
    }
    if (uset) {
        u32 set = (u32)(*uset & 0xFFFFFFFFULL);

        if (how == 0)
            p->sig_blocked |= set;      /* SIG_BLOCK */
        else if (how == 1)
            p->sig_blocked &= ~set;     /* SIG_UNBLOCK */
        else
            p->sig_blocked = set;       /* SIG_SETMASK */
    }
    return 0;
}

static long sys_lseek(u64 *frame)
{
    /* 站39:whence 三档全脸(SET/CUR/END 透传,服务器 rsv 槽承载;
     * 站33 loader 的 SEEK_SET 面不变);offset 截 32 位(ramfs/裸盘
     * 域内)。 */
    if (!rt_cur->proc)
        return -ES_EINVAL;
    if (FR(1) > 0xFFFFFFFFULL)
        return -ES_EINVAL;
    return rt_fd_seek_whence(rt_cur->proc, (int)FR(0), FR(1), (u32)FR(2));
}

/* 站三十五:线程面(musl pthread 的克隆契约,体在 rt_thr.c)。 */
static long sys_clone(u64 *frame)
{
    u32 flags = (u32)FR(0);
    u64 stack = FR(1), ptid = FR(2), tls = FR(3), ctid = FR(4);
    long tid;

    if (!rt_cur->proc)
        return -ES_EINVAL;
    if (!(rt_cur->proc->caps & CAP_SPAWN))
        return -1;                     /* 站41:CAP_SPAWN fail-closed */
    if ((flags & RT_CLONE_PARENT_SETTID) && ptid &&
        !access_ok((void *)ptid, 8))
        return -ES_EFAULT;             /* 父语境写,先校 */
    if ((flags & RT_CLONE_CHILD_CLEARTID) && ctid &&
        !access_ok((void *)ctid, 8))
        return -ES_EFAULT;             /* 线程退出时本人语境写 */
    if ((flags & RT_CLONE_SETTLS) && tls &&
        tls < 0x40200000UL)
        return -ES_EINVAL;             /* tp 须落用户窗(粗判) */
    tid = sys_clone_impl(frame, flags, stack, ptid, tls, ctid);
    if (tid >= 0 && (flags & RT_CLONE_PARENT_SETTID) && ptid)
        *(u64 *)ptid = (u64)tid;
    return tid;
}

static long sys_gettid(u64 *frame)
{
    (void)frame;
    return (long)rt_cur->tid;         /* 站35:leader=pid,线程=发号 */
}

/* ---- 站四十一:CAPR 面(参考实现五面的最小站内形) ---- */

static long sys_cap_drop(u64 *frame)
{
    u64 mask = FR(0);

    if (!rt_cur->proc)
        return -ES_EINVAL;
    rt_cur->proc->caps &= ~mask;      /* 自削单向:无加能力路径 */
    return 0;
}

static long sys_cap_get(u64 *frame)
{
    (void)frame;
    if (!rt_cur->proc)
        return -ES_EINVAL;
    return (long)rt_cur->proc->caps;
}

static long sys_uuid_get(u64 *frame)
{
    (void)frame;
    if (!rt_cur->proc)
        return -ES_EINVAL;
    return (long)rt_cur->proc->uuid;   /* 低 64 位截断打面(身份
                                       * 判据=互异,非全值) */
}

static long sys_rlimit_self(u64 *frame)
{
    u32 fd = (u32)FR(0), ch = (u32)FR(1);

    if (!rt_cur->proc)
        return -ES_EINVAL;
    if (fd && fd < rt_cur->proc->max_fd)
        rt_cur->proc->max_fd = fd;    /* 只降不升(0=不动) */
    if (ch && ch < rt_cur->proc->max_child)
        rt_cur->proc->max_child = ch;
    return 0;
}

/* ---- 站四十:busybox/服务收编薄面(qemu-user -strace 需求定谳) ---- */

static long sys_openat(u64 *frame)
{
    char kpath[48];
    long r;

    if (!rt_cur->proc)
        return -ES_EINVAL;
    if (copy_in_str(kpath, FR(1), sizeof(kpath)) < 0)
        return -ES_EFAULT;             /* dirfd v0 不读(AT_FDCWD) */
    r = rt_fd_open(rt_cur->proc, kpath);   /* 目录回退在 fd 层 */
    /* 站59:musl 建文件面(fopen(w)/touch/重定向)全走 O_CREAT=0x40
     * (ztest 定谳:纯 open 对新文件 -ENOENT);v0 语义=open 失败即
     * create(create 即开);O_EXCL/O_TRUNC 旗语不读(ramfs 现有面
     * 记边界)。 */
    if (r == -2 && (FR(2) & 0x40)) {
        r = rt_fd_create(rt_cur->proc, kpath);
    }
    return r;
}

static long sys_close(u64 *frame)
{
    if (!rt_cur->proc)
        return -ES_EINVAL;
    return rt_fd_close(rt_cur->proc, (int)FR(0));
}

static long sys_getdents64(u64 *frame)
{
    char *ub = (char *)FR(1);
    u32 len = (u32)FR(2);

    if (!rt_cur->proc)
        return -ES_EINVAL;
    if (!len || !access_ok(ub, len))
        return -ES_EFAULT;
    return rt_fd_getdents64(rt_cur->proc, (int)FR(0), ub, len);
}

/* musl aarch64 struct stat 144B(inst/include/bits/stat.h 定谳):
 * mode@16/size@48/blksize@56/blocks@72;零填+定点写(busybox
 * ls 分类面只吃 mode;d_type 已真给,stat=深脸兜底)。 */
static long sys_newfstatat(u64 *frame)
{
    char kpath[48];
    char st[144];
    u64 *up = (u64 *)FR(2);
    u32 mode = 0, size = 0;
    long r;
    u32 i;

    if (!rt_cur->proc)
        return -ES_EINVAL;
    if (copy_in_str(kpath, FR(1), sizeof(kpath)) < 0)
        return -ES_EFAULT;
    if (!access_ok(up, sizeof(st)))
        return -ES_EFAULT;
    if ((r = rt_fd_fstatat(rt_cur->proc, kpath, &mode, &size)) < 0)
        return r;
    for (i = 0; i < sizeof(st); i++)
        st[i] = 0;
    *(u64 *)(void *)(st + 0) = 1;                  /* st_dev */
    *(u64 *)(void *)(st + 8) = 2;                  /* st_ino */
    *(u32 *)(void *)(st + 16) = mode;
    *(u32 *)(void *)(st + 20) = 1;                 /* st_nlink */
    *(u64 *)(void *)(st + 48) = size;
    *(u64 *)(void *)(st + 56) = 4096;              /* st_blksize(long) */
    *(u64 *)(void *)(st + 72) = (size + 511) / 512; /* st_blocks */
    for (i = 0; i < sizeof(st); i++)
        ((char *)up)[i] = st[i];
    return 0;
}

static long sys_dup3(u64 *frame)
{
    if (!rt_cur->proc)
        return -ES_EINVAL;
    return rt_fd_dup3(rt_cur->proc, (int)FR(0), (int)FR(1));
}

static long sys_fcntl(u64 *frame)
{
    /* 站42:FD_CLOEXEC 真落位(F_SETFD=2 记/F_GETFD=1 读);其余
     * 命令(dup 旗/status 旗等)v0 仍回 0——exec 摘除面见
     * sys_exec_setup。 */
    struct rt_proc *p = rt_cur->proc;
    int fd = (int)FR(0);

    if (!p || fd < 0 || (u32)fd >= RT_FD_MAX || !p->fds[fd].used)
        return -9;
    if ((long)FR(1) == 2)              /* F_SETFD */
        p->fds[fd].cloexec = (FR(2) & FD_CLOEXEC) ? 1 : 0;
    return (long)FR(1) == 1 ? (long)p->fds[fd].cloexec : 0;
}

static long sys_clock_gettime(u64 *frame)
{
    u64 *ts = (u64 *)FR(1);
    u64 ns;

    if (FR(0) > 1u)
        return -ES_EINVAL;             /* 0=REALTIME 1=MONOTONIC */
    if (!access_ok(ts, 16))
        return -ES_EFAULT;
    ns = FR(0) == 0 ? rt_time_real_ns() : rt_time_mono_ns();
    ts[0] = ns / 1000000000ULL;
    ts[1] = ns % 1000000000ULL;
    return 0;
}

static long sys_clock_getres(u64 *frame)
{
    u64 *ts = (u64 *)FR(1);
    if (FR(0) > 1u)
        return -ES_EINVAL;
    if (!access_ok(ts, 16))
        return -ES_EFAULT;
    ts[0] = 0;
    ts[1] = 1;                          /* 1ns(计数器面) */
    return 0;
}

/* 站60:nanosleep(真号 35)——EL0 周期睡眠面(cyclictest 消费者)。
 * POSIX 至少语义:醒拍界=ceil(ns/T)+1(拍界醒来最短 (k-1)T+拍内相位,
 * +1 拍界才保证 ≥ns);代价=最多超睡一拍(10ms,tick 量子的诚实超睡,
 * cyclictest 基线直方图的分布本源;hrtimer 亚拍精度=后弧)。 */
static long sys_nanosleep(u64 *frame)
{
    u64 *ts = (u64 *)FR(0);
    u64 ns, period = 1000000000ULL / RT_HZ;

    if (!access_ok(ts, 16))
        return -ES_EFAULT;
    ns = ts[0] * 1000000000ULL + ts[1];
    if (ns == 0)
        return 0;
    rt_sleep_ticks((ns + period - 1) / period + 1);
    return 0;
}

static long sys_gettimeofday(u64 *frame)
{
    u64 *tv = (u64 *)FR(0);
    u64 ns;

    if (!tv)
        return 0;
    if (!access_ok(tv, 16))
        return -ES_EFAULT;
    ns = rt_time_real_ns();
    tv[0] = ns / 1000000000ULL;
    tv[1] = (ns % 1000000000ULL) / 1000ULL;   /* usec;tz 恒 NULL 面 */
    return 0;
}

static long sys_brk(u64 *frame)
{
    struct rt_proc *p = rt_cur->proc;
    u64 addr = FR(0);

    if (!p)
        return -ES_EINVAL;
    if (!p->brk)
        return 0;                      /* 未 exec 语境(mallocng 探测自洽) */
    if (!addr)
        return (long)p->brk;
    if (addr < 0x40200000UL || addr >= 0x40E00000UL)
        return (long)p->brk;           /* 窗外=原值(Linux 契约:失败回当前) */
    if (addr > p->brk) {
        if (!rt_as_mmap(p, p->brk, addr - p->brk))
            return (long)p->brk;       /* 增量 hint-mmap=窗内堆页 */
    }
    p->brk = addr;
    return (long)p->brk;              /* 收缩=只挪游标不收页(v0 记档) */
}

static long sys_getrandom(u64 *frame)
{
    char *ub = (char *)FR(0);
    u32 len = (u32)FR(1);
    char kb[512];
    u32 i;

    if (len > sizeof(kb))
        return -ES_EINVAL;             /* v0:每调 ≤512B */
    if (len && !access_ok(ub, len))
        return -ES_EFAULT;
    rt_rng_fill(kb, len);
    for (i = 0; i < len; i++)
        ub[i] = kb[i];
    return (long)len;
}

static long sys_uid0(u64 *frame)
{
    (void)frame;
    return 0;                          /* uid 族/setuid/fcntl=0(root 世界) */
}

static long sys_hvc(u64 *frame)
{
    long fn = (long)FR(0);          /* 首参 x0(壳 sys1 映射) */

    switch (fn) {
    case 1:
        hvc_yield();                 /* EL1 语境发 HVC,EL2 往返 */
        return 0;
    case 2:
        hvc_host();
        return 0;
    case 3:
        hvc_exit();                  /* noreturn:VM_DEAD 内核收尸 */
        __builtin_unreachable();
    default:
        return -ES_EINVAL;
    }
}

/* ---- 内核镜像命令体(自 rt_shell.c 原样移植:判据字节不变) ---- */

static void k_selftest(void)
{
    u64 sum = 0;
    unsigned int i;

    for (i = 0; i < 100; i++) {
        sum += i;
        __asm__ volatile("" : "+r"(sum));   /* 挡常量折叠:寄存器存活性判据 */
        uart_putc('.');   /* 100 次 MMIO 陷阱(同裸机判据) */
    }
    if (sum == 4950)
        rt_puts("\nselftest OK\n");
    else
        rt_puts("\nselftest FAIL\n");
}

static const char *wsrc_name(u32 w)
{
    static const char *const n[] = { "-", "rx", "sem", "mut",
                                     "futx", "msg", "slep" };

    return w < 7 ? n[w] : "?";
}

static void k_ps(void)
{
    unsigned int idx;
    rt_task_t *t = rt_ps_first(&idx);
    rt_puts("ps: task table (prio 0 high)\n");
    while (t) {
        if (t->state != RT_FREE && t->state != RT_DEAD) {
            rt_puts("  ");
            rt_puts(t->name);
            rt_puts(" prio ");
            rt_puts_u64("", t->prio, "");
            rt_puts("  ");
            rt_puts(rt_state_name(t->state));
            rt_puts(" blk ");
            rt_puts(wsrc_name(t->wait_src));       /* 站43:归属列 */
            rt_puts(" stk ");
            if (idx < RT_POOL_MAX)
                rt_puts_u64("", rt_cur_stack_margin(idx), "");
            else
                rt_puts("-");
            rt_puts("\n");
        }
        t = rt_ps_next(&idx);
    }
}

static void k_stackcheck(void)
{
    unsigned int idx = 0;
    int bad = 0;
    rt_task_t *t = rt_ps_first(&idx);

    while (t) {
        if (t->state != RT_FREE && t->state != RT_DEAD &&
            idx < RT_POOL_MAX) {
            rt_puts("stk: ");
            rt_puts(t->name);
            rt_puts_u64(" margin ", rt_cur_stack_margin(idx), "\n");
            if (rt_stack_slot_ok(idx) == 0) {
                rt_puts("stk: magic BAD in ");
                rt_puts(t->name);
                rt_puts("\n");
                bad = 1;
            }
        }
        t = rt_ps_next(&idx);
    }
    if (!bad)
        rt_puts("stk: magic ok\n");
}

static void k_spawned_task(void *arg)
{
    rt_puts((const char *)arg);
    rt_puts(": hello from task\n");
}

static void k_spawn(const char *uname)
{
    static char spawn_name[12];
    const char *nm = uname;
    unsigned int i;

    for (i = 0; i < sizeof(spawn_name) - 1 && nm[i]; i++)
        spawn_name[i] = nm[i];
    spawn_name[i] = '\0';
    if (i == 0) {
        rt_puts("usage: spawn <name>\n");
    } else if (!rt_task_create(spawn_name, k_spawned_task, spawn_name, 8)) {
        rt_puts("spawn: pool full\n");
    }
    /* 成功静默:问候行由任务自己打 */
}

static void k_fpstat(void)
{
    rt_puts_u64("lazy: ", rt_fp_stat_total(), " switches, ");
    rt_puts_u64("", rt_fp_saves, " fp-saves\n");
}

static long sys_demo(u64 *frame)
{
    long id = (long)FR(0);          /* 首参 x0:壳 sys2(nr,id,0) 映射
                                     * id→x0(冒烟定谳:曾读 x1 恒 0,
                                     * 全部命令静默 EINVAL) */

    switch (id) {
    case RT_U_SELFTEST:  k_selftest();   break;
    case RT_U_PS:         k_ps();         break;
    case RT_U_SPAWN:      k_spawn((const char *)FR(1)); break;
    case RT_U_FP:
        if (rt_task_create("fptest", rt_fp_task, NULL, 12))
            rt_puts("fp: task spawned\n");
        else
            rt_puts("fp: pool full\n");
        break;
    case RT_U_SEMDEMO:    rt_demo_sem();   break;
    case RT_U_MUTEXDEMO:  rt_demo_mutex(); break;
    case RT_U_PIDEMO:     rt_demo_pi();    break;
    case RT_U_FPDUAL:     rt_demo_fpdual();break;
    case RT_U_EVENTDEMO:  rt_demo_event(); break;
    case RT_U_QUEUEDEMO:  rt_demo_queue(); break;
    case RT_U_SMPDEMO:    rt_demo_smp();   break;
    case RT_U_IPIDEMO:    rt_demo_ipi();   break;
    case RT_U_FPSMP:      rt_demo_fpsmp(); break;
    case RT_U_TMODEMO:    rt_demo_tmo();   break;
    case RT_U_SWTDEMO:    rt_demo_swt();   break;
    case RT_U_IRQDEMO:    rt_demo_irq();   break;
    case RT_U_LOWDEMO:    rt_demo_low();   break;
    case RT_U_STKDEMO:    rt_demo_stk();   break;
    case RT_U_STACKCHECK: k_stackcheck(); break;
    case RT_U_OVERFLOW:  rt_demo_overflow(); break;
    case RT_U_FPSTAT:     k_fpstat();      break;
    case RT_U_HB:
        rt_puts_u64("rtos: alive tick=", rt_ticks, "\n");
        break;
    case RT_U_SLEEP:
        rt_sleep_ticks((u64)FR(1) * RT_HZ);
        break;
    case RT_U_IPCDEMO:
        /* 站30:服务器(prio 20)+占先干扰(prio 6,窗 30 拍)。
         * 干扰不给让路服务器就轮不到——提权让 recv 者越它而行,
         * 客户端回复快返=传播活证(门禁 Δ 判据)。 */
        if (!rt_task_create("ipcspin", rt_msg_demo_spinner,
                            (void *)30UL, 6))
            rt_puts("ipc: spinner full\n");
        if (!rt_task_create("ipcsrv", rt_msg_demo_server, NULL, 20))
            rt_puts("ipc: server full\n");
        break;
    case RT_U_PULSEDEMO:
        if (!rt_task_create("pulsesrv", rt_msg_pulse_server, NULL, 8))
            rt_puts("ipc: pulse server full\n");
        break;
    case RT_U_LONGDEMO:
        if (!rt_task_create("longsrv", rt_msg_long_server, NULL, 8))
            rt_puts("ipc: long server full\n");
        break;
    case RT_U_CALLTRACE:           /* 站43:观测面活证(蹦床语境=壳
                                    * 任务内核栈,帧链直达 dispatch) */
        rt_calltrace();
        break;
    case RT_U_ABORTDEMO:
        if (!rt_task_create("abortsrv", rt_msg_abort_server, NULL, 8))
            rt_puts("ipc: abort server full\n");
        break;
    case RT_U_FUPOKE:
        /* 站32:扑克=同址空间第二执行流替身(只做队列唤醒;
         * FR(1)=用户字 VA=key,FR(2)=延迟拍;真 threads=站 33+) */
        rt_futex_poke(rt_cur->proc, FR(1), FR(2) ? FR(2) : 30);
        break;
    case RT_U_APS:                      /* 站45:CPU 分区判据体(自含:
                                          * 对齐+争跑窗+偷闲窗+表) */
        rt_aps_demo();
        break;
    case RT_U_MPART_ALLOC:              /* 站45:内存分区(FL0 面:
                                          * fork 子自 attach,壳不绑) */
        return rt_mpart_alloc("mpdemo", (u32)FR(1));
    case RT_U_MPART_ATTACH:
        rt_mpart_attach(rt_cur->proc, (int)FR(1));
        break;
    case RT_U_MPART_FREE:
        rt_mpart_free((int)FR(1));
        break;
    case RT_U_LOGDEMO:    k_logdemo(FR(1)); break;
    case RT_U_LOGLEVEL:   k_loglevel_args((const char *)FR(1)); break;
    case RT_U_LOGMODS:    k_logmods();      break;
    case RT_U_LOGDUMP:    k_logdump();      break;
    case RT_U_LOGCTRL:    k_logctrl((const char *)FR(1)); break;
    case RT_U_HAM_ADD:    k_ham_add((const char *)FR(1), (u32)FR(2)); break;
    case RT_U_HAM_BEAT:    k_ham_beat((const char *)FR(1)); break;
    case RT_U_HAM_LS:      k_ham_ls();    break;
    case RT_U_DBGDEMO:     k_dbgdemo();   break;
    case RT_U_PSCIDEMO:    k_pscidemo();  break;
    case RT_U_PSCIOFF:     k_pscioff();   break;
    case RT_U_PSCIRESET:   k_pscireset(); break;
    case RT_U_PARTS:
        rt_cpart_show();                /* k_aps_show 镜像(参考实现白描) */
        rt_mpart_show();
        break;
    case RT_U_CRASHINFO:               /* 站46:crash 环倾倒(僵尸面) */
        rt_crash_show();
        break;
    case RT_U_HEALTH:                 /* 站48:批四面合卷健康面板 */
        rt_health_show();
        break;

    default:
        return -ES_EINVAL;
    }
    return 0;
}

/* ---------------- 分发表 ---------------- */

typedef long (*rt_sysfn_t)(u64 *frame);

static long sys_ni(u64 *frame)
{
    (void)frame;
    return -ES_ENOSYS;
}

static rt_sysfn_t table[ES_NR_MAX];

/* ---- 站四十二:syscall 面补全(参考实现终审计补齐弧;号面从
 *      musl-1.2.5 aarch64 头钉死零臆测)。 ---- */

/* 调度器管理面目标解析:pid<=0=自己;他 pid=该进程 leader。 */
static rt_task_t *sched_target(long pid, struct rt_proc **pp)
{
    struct rt_proc *p = rt_cur->proc;

    if (!p)
        return NULL;
    if (pid <= 0) {
        if (pp)
            *pp = p;
        return p->leader;
    }
    p = rt_proc_lookup((int)pid);
    if (!p)
        return NULL;
    if (pp)
        *pp = p;
    return p->leader;
}

/* 优先级落位(站 18 PI 纪律:基准位全树唯一回收点——写 base_prio;
 * 被 boost 中(prio!=base)只动基准,boost 卸载自然归新位;就绪者
 * 摘队重排)。 */
static void sched_apply_prio(rt_task_t *t, u32 np)
{
    u64 d;

    if (np == t->base_prio)
        return;
    d = rt_daif_save();
    if (t->prio == t->base_prio) {  /* 未被 boost:两域齐动 */
        if (t->state == RT_READY) {
            rdy_remove(t);
            t->prio = np;
            t->base_prio = np;
            rt_ready_push(t);
        } else {
            t->prio = np;
            t->base_prio = np;
        }
    } else {
        t->base_prio = np;          /* boost 中:基准先行 */
    }
    rt_daif_load(d);
}

static u32 user_r32(u64 up)
{
    return access_ok((void *)up, 4) ? *(u32 *)(void *)up : 0;
}

static long sys_sched_getparam(u64 *frame)
{
    rt_task_t *t = sched_target((long)FR(0), NULL);

    if (!t || !access_ok((void *)FR(1), 4))
        return -ES_EINVAL;
    *(u32 *)(void *)FR(1) = t->base_prio;
    return 0;
}

static long sys_sched_setparam(u64 *frame)
{
    rt_task_t *t = sched_target((long)FR(0), NULL);
    u32 np;

    if (!t || !access_ok((void *)FR(1), 4))
        return -ES_EINVAL;
    np = *(u32 *)(void *)FR(1);
    if (np >= 32u)
        return -ES_EINVAL;
    sched_apply_prio(t, np);
    return 0;
}

static long sys_sched_setscheduler(u64 *frame)
{
    rt_task_t *t = sched_target((long)FR(0), NULL);
    u32 np;

    /* 站42 v0:策略字收下(FIFO/OTHER)但调度类不分档(位图调度
     * 即 FIFO 形;CFS=参考实现记档偏差)。 */
    if (!t || (long)FR(1) > SCHED_FIFO || !access_ok((void *)FR(2), 4))
        return -ES_EINVAL;
    np = *(u32 *)(void *)FR(2);
    if (np >= 32u)
        return -ES_EINVAL;
    sched_apply_prio(t, np);
    return 0;
}

static long sys_sched_getscheduler(u64 *frame)
{
    if (!sched_target((long)FR(0), NULL))
        return -ES_ESRCH;
    return SCHED_FIFO;                /* v0:位图调度即 FIFO 同形 */
}

static long sys_sched_yield(u64 *frame)
{
    (void)frame;
    rt_sched();                      /* 同级 FIFO 轮转=站 16 量子律
                                      * (hvc_yield=宿主暂停面,两物种) */
    return 0;
}

static long sys_sched_get_priority_max(u64 *frame)
{
    (void)frame;
    return 31;
}

static long sys_sched_get_priority_min(u64 *frame)
{
    (void)frame;
    return 0;
}

static long sys_sched_rr_get_interval(u64 *frame)
{
    if (!access_ok((void *)FR(1), 16))
        return -ES_EFAULT;
    if (!sched_target((long)FR(0), NULL))
        return -ES_ESRCH;
    *(u64 *)(void *)FR(1) = 0;
    *(u64 *)(void *)(FR(1) + 8) = (u64)RT_SLICE * 10 * 1000 * 1000;
    return 0;
}

static long sys_sched_setaffinity(u64 *frame)
{
    rt_task_t *t = sched_target((long)FR(0), NULL);
    u32 mask = user_r32(FR(2));

    if (!t || !mask)
        return -ES_EINVAL;
    mask &= 3u;                       /* 双核拓扑(RT_NCPU=2) */
    if (!mask)
        return -ES_EINVAL;
    t->cpu = (mask & 1u) ? 0 : 1;     /* v0:下次入队生效(就绪者
                                      * 不强迁,醒来各归其 cpu) */
    return 0;
}

static long sys_sched_getaffinity(u64 *frame)
{
    rt_task_t *t = sched_target((long)FR(0), NULL);

    if (!t || !access_ok((void *)FR(2), 4))
        return -ES_EFAULT;
    *(u32 *)(void *)FR(2) = 1u << t->cpu;
    return 4;
}

static long sys_getcpu(u64 *frame)
{
    if (FR(0) && access_ok((void *)FR(0), 4))
        *(u32 *)(void *)FR(0) = rt_cpu_id();
    if (FR(1) && access_ok((void *)FR(1), 4))
        *(u32 *)(void *)FR(1) = 0;   /* node v0 恒 0 */
    return 0;
}

/* 站42 nice 面:Linux nice[-20,19] ↔ 站内位图[1,31] 线性图
 * (prio=16+~n/2;两向一致记档,精确 Linux 语义=后弧)。 */
static long sys_setpriority(u64 *frame)
{
    rt_task_t *t = sched_target((long)FR(1), NULL);
    long n = (long)FR(2);

    if ((long)FR(0) != 0)             /* 仅 PRIO_PROCESS v0 */
        return -ES_EINVAL;
    if (!t || n < -20 || n > 19)
        return -ES_EINVAL;
    sched_apply_prio(t, (u32)(n < 0 ? 16 - ((-n + 1) >> 1)
                                    : 16 + (n >> 1) + (n & 1)));
    return 0;
}

static long sys_getpriority(u64 *frame)
{
    rt_task_t *t = sched_target((long)FR(1), NULL);

    if ((long)FR(0) != 0 || !t)
        return -ES_EINVAL;
    return (long)t->base_prio >= 16 ? ((long)t->base_prio - 16) * 2
                                    : -((17 - (long)t->base_prio) * 2);
}

static long sys_rt_sigpending(u64 *frame)
{
    struct rt_proc *p = rt_cur->proc;
    u64 set = (p ? ((u64)p->sig_pending | rt_cur->sig_pending) : 0);

    if (!access_ok((void *)FR(0), 8))
        return -ES_EFAULT;
    *(u64 *)(void *)FR(0) = set;
    return 0;
}

static long sys_rt_sigsuspend(u64 *frame)
{
    struct rt_proc *p = rt_cur->proc;
    u32 old, mask;

    if (!p || !access_ok((void *)FR(0), 8))
        return -ES_EINVAL;
    mask = (u32)*(u64 *)(void *)FR(0);
    old = p->sig_blocked;
    p->sig_blocked = mask;
    /* 站17 等待机器正牌形状(站24 rx 同律:判查+发布 BLOCK+摘队同
     * DAIF 窗,窗后挂起已可见,投递者唤醒走 sig_wake wait_mask 支);
     * 站41 教训反面教材=漏 BLOCK 态直调 rt_sched=调用者驻车。 */
    for (;;) {
        u64 d;

        if ((p->sig_pending | rt_cur->sig_pending) & ~p->sig_blocked)
            break;
        d = rt_daif_save();
        if ((p->sig_pending | rt_cur->sig_pending) & ~p->sig_blocked) {
            rt_daif_load(d);           /* 窗内竞得:不挂起,回环顶重查 */
            break;
        }
        rt_cur->wait_mask = RT_SIG_BIT;
        rt_cur->state = RT_BLOCK;
        rdy_remove(rt_cur);
        rt_daif_load(d);
        rt_sched();
        rt_cur->wait_mask = 0;        /* sig_wake 清后此处恒 0;防重
                                       * 挂窗口残值走读者清零 */
    }
    p->sig_retmask = old;            /* POSIX:掩码还原=handler 完成后
                                      * (sigreturn 兜),非本返回口 */
    p->sig_suspflag = 1;
    return -4;                        /* -EINTR(投递在 syscall 出口,
                                      * 挂起窗掩码=投递掩码) */
}

static long sys_sigaltstack(u64 *frame)
{
    struct rt_proc *p = rt_cur->proc;

    if (!p)
        return -ES_EINVAL;
    if (FR(1) && access_ok((void *)FR(1), 16)) {   /* 回读旧注册 */
        *(u64 *)(void *)FR(1) = p->sig_altsp;
        *(u64 *)(void *)(FR(1) + 8) = p->sig_altsize;
    }
    if (FR(0) && access_ok((void *)FR(0), 16)) {
        u64 sp = *(u64 *)(void *)FR(0);
        u64 sz = *(u64 *)(void *)(FR(0) + 8);

        p->sig_altsp = sp;
        p->sig_altsize = (u32)sz;
    }
    return 0;                         /* v0:注册+回读;切栈投递=后弧 */
}

static long sys_setsid(u64 *frame)
{
    struct rt_proc *p = rt_cur->proc;

    (void)frame;
    if (!p)
        return -ES_EPERM;
    p->sid = p->pid;                  /* 自成会话=自成组(参考实现白描) */
    p->pgid = p->pid;
    return (long)p->pid;
}

static long sys_getsid(u64 *frame)
{
    struct rt_proc *p = (long)FR(0) <= 0 ? rt_cur->proc
                                         : rt_proc_lookup((int)FR(0));

    return p ? (long)p->sid : -ES_ESRCH;
}

static long sys_setpgid(u64 *frame)
{
    struct rt_proc *p = (long)FR(0) <= 0 ? rt_cur->proc
                                         : rt_proc_lookup((int)FR(0));
    struct rt_proc *g;

    if (!p)
        return -ES_ESRCH;
    g = (long)FR(1) <= 0 ? NULL : rt_proc_lookup((int)FR(1));
    if (g)
        p->pgid = g->pgid;            /* 入组(组长为组内进程) */
    else
        p->pgid = p->pid;            /* pgid=0 面:自成组 */
    return 0;
}

static long sys_getpgid(u64 *frame)
{
    struct rt_proc *p = (long)FR(0) <= 0 ? rt_cur->proc
                                         : rt_proc_lookup((int)FR(0));

    return p ? (long)p->pgid : -ES_ESRCH;
}

static long sys_set_robust_list(u64 *frame)
{
    struct rt_proc *p = rt_cur->proc;

    if (!p)
        return -ES_EINVAL;
    p->robust_head = FR(0);
    p->robust_len = (u32)FR(1);
    return 0;
}

static long sys_get_robust_list(u64 *frame)
{
    struct rt_proc *p = (long)FR(0) <= 0 ? rt_cur->proc
                                         : rt_proc_lookup((int)FR(0));

    if (!p)
        return -ES_ESRCH;
    if (!access_ok((void *)FR(1), 8) ||
        (FR(2) && !access_ok((void *)FR(2), 4)))
        return -ES_EFAULT;
    *(u64 *)(void *)FR(1) = p->robust_head;
    if (FR(2))
        *(u32 *)(void *)FR(2) = p->robust_len;
    return 0;
}

static long sys_uname(u64 *frame)
{
    static const char *const f[6] = {
        "eshine-os", "eshine-os", "27.04", "eshine-rtos 27.04-dev",
        "aarch64", "(none)"
    };
    char *u = (char *)(void *)FR(0);
    u32 i, j;

    if (!access_ok(u, 6 * 65))
        return -ES_EFAULT;
    for (i = 0; i < 6; i++)
        for (j = 0; j < 65; j++)
            u[i * 65 + j] = (j == 64 || f[i][j]) ? f[i][j] : '\0';
    return 0;
}

/* ---- 站42:posix timer+itimer(真号 107-111/102/103) ---- */

static struct rt_ptimer *ptimer_find(struct rt_proc *p, u64 id)
{
    if (id == 0 || id > 8u)
        return NULL;
    if (ptimer_pool[id - 1].owner != p)
        return NULL;
    return &ptimer_pool[id - 1];
}

static long sys_timer_create(u64 *frame)
{
    struct rt_proc *p = rt_cur->proc;
    u64 sev = FR(1);
    struct rt_ptimer *pt = NULL;
    u32 i, signo, notify, tid;
    u64 value;

    if (!p || FR(0) > 1u)
        return -ES_EINVAL;
    if (!access_ok((void *)sev, 24) || !access_ok((void *)FR(2), 8))
        return -ES_EFAULT;
    value = *(u64 *)(void *)sev;                   /* sigev_value */
    signo = *(u32 *)(void *)(sev + 8);             /* sigev_signo */
    notify = *(u32 *)(void *)(sev + 12);           /* sigev_notify */
    tid = *(u32 *)(void *)(sev + 16);              /* SIGEV_THREAD_ID 目标 */
    if (notify != 4 || signo == 0 || signo >= RT_SIG_MAX)
        return -ES_EINVAL;            /* v0:仅 SIGEV_THREAD_ID 面单形 */
    for (i = 0; i < 8; i++)
        if (!ptimer_pool[i].owner) {
            pt = &ptimer_pool[i];
            break;
        }
    if (!pt)
        return -12;                   /* -ENOMEM */
    pt->owner = p;
    pt->slot = (int)i;
    pt->interval_ns = 0;
    pt->overrun = 0;
    pt->is_itimer = 0;
    pt->value = value;
    pt->signo = (int)signo;
    pt->target_tid = tid;
    rt_timer_init(&pt->tm, ptimer_fire, pt);
    *(u64 *)(void *)FR(2) = (u64)i + 1;
    return 0;
}

static long sys_timer_delete(u64 *frame)
{
    struct rt_proc *p = rt_cur->proc;
    struct rt_ptimer *pt = p ? ptimer_find(p, FR(0)) : NULL;

    if (!pt)
        return -ES_EINVAL;
    rt_timer_stop(&pt->tm);
    pt->owner = NULL;
    return 0;
}

static long sys_timer_settime(u64 *frame)
{
    struct rt_proc *p = rt_cur->proc;
    struct rt_ptimer *pt = p ? ptimer_find(p, FR(0)) : NULL;
    u64 nint, nval, oint = 0, orem = 0;
    u64 ticks;

    if (!pt)
        return -ES_EINVAL;
    if (FR(3) && access_ok((void *)FR(3), 32)) {  /* 回写旧值先取 */
        oint = pt->interval_ns;
        orem = pt->tm.deadline ?
               (pt->tm.deadline - rt_ticks) * 10 * 1000 * 1000 : 0;
    }
    if (!access_ok((void *)FR(2), 32))
        return -ES_EFAULT;
    nint = *(u64 *)(void *)(FR(2) + 8);            /* it_interval.nsec */
    nint += *(u64 *)(void *)FR(2) * 1000000000ULL;
    nval = *(u64 *)(void *)(FR(2) + 24);           /* it_value.nsec */
    nval += *(u64 *)(void *)(FR(2) + 16) * 1000000000ULL;
    rt_timer_stop(&pt->tm);
    pt->interval_ns = nint;
    if (nval) {
        ticks = (nval + 9999999ULL) / 10000000ULL;
        if (!ticks)
            ticks = 1;
        rt_timer_start(&pt->tm, ticks, nint != 0);
    }
    if (FR(3) && access_ok((void *)FR(3), 32)) {
        *(u64 *)(void *)FR(3) = oint / 1000000000ULL;
        *(u64 *)(void *)(FR(3) + 8) = oint % 1000000000ULL;
        *(u64 *)(void *)(FR(3) + 16) = orem / 1000000000ULL;
        *(u64 *)(void *)(FR(3) + 24) = orem % 1000000000ULL;
    }
    return 0;
}

static long sys_timer_gettime(u64 *frame)
{
    struct rt_proc *p = rt_cur->proc;
    struct rt_ptimer *pt = p ? ptimer_find(p, FR(0)) : NULL;
    u64 rem, ns;

    if (!pt || !access_ok((void *)FR(1), 32))
        return -ES_EINVAL;
    rem = pt->tm.deadline && pt->tm.deadline > rt_ticks
          ? pt->tm.deadline - rt_ticks : 0;
    ns = rem * 10 * 1000 * 1000;
    *(u64 *)(void *)FR(1) = pt->interval_ns / 1000000000ULL;
    *(u64 *)(void *)(FR(1) + 8) = pt->interval_ns % 1000000000ULL;
    *(u64 *)(void *)(FR(1) + 16) = ns / 1000000000ULL;
    *(u64 *)(void *)(FR(1) + 24) = ns % 1000000000ULL;
    return 0;
}

static long sys_timer_getoverrun(u64 *frame)
{
    struct rt_proc *p = rt_cur->proc;
    struct rt_ptimer *pt = p ? ptimer_find(p, FR(0)) : NULL;
    u32 r;

    if (!pt)
        return -ES_EINVAL;
    r = pt->overrun;
    pt->overrun = 0;
    return (long)r;
}

/* itimer:仅 ITIMER_REAL(0)——SIGALRM 到 leader,同池骑坐。 */
static struct rt_ptimer *itimer_of(struct rt_proc *p, int create)
{
    u32 i;

    for (i = 0; i < 8; i++)
        if (ptimer_pool[i].owner == p && ptimer_pool[i].is_itimer)
            return &ptimer_pool[i];
    if (!create)
        return NULL;
    for (i = 0; i < 8; i++)
        if (!ptimer_pool[i].owner) {
            ptimer_pool[i].owner = p;
            ptimer_pool[i].is_itimer = 1;
            ptimer_pool[i].signo = 14;             /* SIGALRM */
            ptimer_pool[i].target_tid = 0;
            ptimer_pool[i].value = 0;
            ptimer_pool[i].interval_ns = 0;
            ptimer_pool[i].overrun = 0;
            rt_timer_init(&ptimer_pool[i].tm, ptimer_fire, &ptimer_pool[i]);
            return &ptimer_pool[i];
        }
    return NULL;
}

static long sys_setitimer(u64 *frame)
{
    struct rt_proc *p = rt_cur->proc;
    struct rt_ptimer *pt;
    u64 nint, nval, oint = 0, orem = 0;

    if (!p || (long)FR(0) != 0)
        return -ES_EINVAL;             /* 仅 ITIMER_REAL v0 */
    if (FR(2) && access_ok((void *)FR(2), 32)) {
        pt = itimer_of(p, 0);
        if (pt) {
            oint = pt->interval_ns;
            orem = pt->tm.deadline ?
                   (pt->tm.deadline - rt_ticks) * 10 * 1000 * 1000 : 0;
        }
    }
    if (!access_ok((void *)FR(1), 32))
        return -ES_EFAULT;
    nint = *(u64 *)(void *)(FR(1) + 8);
    nint += *(u64 *)(void *)FR(1) * 1000000000ULL;
    nval = *(u64 *)(void *)(FR(1) + 24);
    nval += *(u64 *)(void *)(FR(1) + 16) * 1000000000ULL;
    pt = itimer_of(p, 1);
    if (!pt)
        return -12;
    rt_timer_stop(&pt->tm);
    pt->interval_ns = nint;
    if (nval) {
        u64 ticks = (nval + 9999999ULL) / 10000000ULL;

        rt_timer_start(&pt->tm, ticks ? ticks : 1, nint != 0);
    }
    if (FR(2) && access_ok((void *)FR(2), 32)) {
        *(u64 *)(void *)FR(2) = oint / 1000000000ULL;
        *(u64 *)(void *)(FR(2) + 8) = oint % 1000000000ULL;
        *(u64 *)(void *)(FR(2) + 16) = orem / 1000000000ULL;
        *(u64 *)(void *)(FR(2) + 24) = orem % 1000000000ULL;
    }
    return 0;
}

static long sys_getitimer(u64 *frame)
{
    struct rt_proc *p = rt_cur->proc;
    struct rt_ptimer *pt = p ? itimer_of(p, 0) : NULL;
    u64 rem, ns;

    if (!p || (long)FR(0) != 0 || !access_ok((void *)FR(1), 32))
        return -ES_EINVAL;
    rem = pt && pt->tm.deadline && pt->tm.deadline > rt_ticks
          ? pt->tm.deadline - rt_ticks : 0;
    ns = rem * 10 * 1000 * 1000;
    *(u64 *)(void *)FR(1) = (pt ? pt->interval_ns : 0) / 1000000000ULL;
    *(u64 *)(void *)(FR(1) + 8) = (pt ? pt->interval_ns : 0) % 1000000000ULL;
    *(u64 *)(void *)(FR(1) + 16) = ns / 1000000000ULL;
    *(u64 *)(void *)(FR(1) + 24) = ns % 1000000000ULL;
    return 0;
}

/* 站43:ppoll 73(musl poll 即走此;fd 串行 POLL 往返+tick 轮询
 * 节律——事件回调后弧)。pollfd={s32 fd,s16 ev,s16 rev}=8B;
 * ts NULL=无限;POLLOUT(4) v0 恒可写;fd 无=POLLNVAL(0x20)。 */
static long sys_ppoll(u64 *frame)
{
    struct rt_proc *p = rt_cur->proc;
    u64 ufds = FR(0), ts = FR(2);
    u32 n = (u32)FR(1), i;
    u64 dl = 0;                       /* 0=无限 */
    long ready;

    if (!p || !n || n > 16u)
        return -22;
    if (!access_ok((void *)ufds, n * 8))
        return -14;
    if (ts && access_ok((void *)ts, 16)) {
        u64 sec = *(u64 *)(void *)ts, nsec = *(u64 *)(void *)(ts + 8);

        dl = rt_ticks + sec * 100 + (nsec + 9999999ULL) / 10000000ULL;
        if (!dl)
            dl = 1;
    }
    for (;;) {
        ready = 0;
        for (i = 0; i < n; i++) {
            u32 ent = *(u32 *)(void *)(ufds + i * 8);
            u16 ev = *(u16 *)(void *)(ufds + i * 8 + 4);  /* fd 是
                                   * 4B int——events 在 +4 不在 fd 高半
                                   * (两次冒烟定谳:count=9 服务端全程
                                   * 在答 1,ev=0 才是过滤真身) */
            u16 rev = 0;
            long r;

            if ((int)ent < 0)
                rev = 0x20;            /* POLLNVAL:负 fd 忽略态 */
            else {
                r = rt_fd_poll(p, (int)ent);
                if (r == 1 && (ev & 1u))
                    rev |= 1u;         /* POLLIN */
                if (r >= 0 && (ev & 4u))
                    rev |= 4u;         /* POLLOUT v0 恒可写 */
                if (r < 0 && r != -9)
                    rev = 0x20;
                else if (r < 0)
                    rev = 0x20;        /* 坏 fd 面=NVAL(POSIX 软面) */
            }
            *(u16 *)(void *)(ufds + i * 8 + 6) = rev;
            if (rev)
                ready++;
        }
        if (ready || (dl && (long long)(rt_ticks - dl) >= 0))
            return ready;
        rt_sleep_ticks(1);             /* 让位一拍再查(v0 节律) */
    }
}

/* ---- 站五十七:AF_UNIX sock 真号面(aarch64 asm-generic 号面;musl
 * 直连零 patch)。控制面四门走 rt_sock 客户端层;sendto/recvfrom=
 * fd>=3 读写面路由同 read/write(addr/flags v0 不读)。 ---- */

static long sys_socket(u64 *frame)
{
    if (!rt_cur->proc)
        return -ES_EINVAL;
    if (FR(0) != 1)                   /* AF_UNIX=1(v0 唯一域) */
        return -ES_EAFNOSUPPORT;
    if (FR(1) != 1)                   /* SOCK_STREAM=1(v0 唯一型) */
        return -ES_EPROTONOSUPPORT;
    return rt_fd_socket(rt_cur->proc);
}

static long sys_sockpath(u64 *frame, int connect)   /* bind/connect 共面 */
{
    /* sockaddr_un 布局:sun_family(2B)+sun_path(108B);路径偏移 2 */
    const char *uaddr = (const char *)FR(1);
    char kpath[26];
    u32 i;

    if (!access_ok(uaddr, 2 + 1))
        return -ES_EFAULT;
    for (i = 0; i < sizeof(kpath) - 1; i++) {
        if (!access_ok(uaddr + 2 + i, 1))
            return -ES_EFAULT;
        kpath[i] = uaddr[2 + i];
        if (!kpath[i])
            return connect ? rt_fd_connect(rt_cur->proc, (int)FR(0), kpath)
                           : rt_fd_bind(rt_cur->proc, (int)FR(0), kpath);
    }
    return -ES_ENAMETOOLONG;          /* v0:路径窗 25 截界 */
}

static long sys_bind(u64 *frame)
{
    if (!rt_cur->proc)
        return -ES_EINVAL;
    return sys_sockpath(frame, 0);
}

static long sys_listen(u64 *frame)
{
    if (!rt_cur->proc)
        return -ES_EINVAL;
    return rt_fd_listen(rt_cur->proc, (int)FR(0), (long)FR(1));
}

static long sys_accept(u64 *frame)
{
    (void)frame;                      /* addr/addrlen v0 不回填 */
    if (!rt_cur->proc)
        return -ES_EINVAL;
    return rt_fd_accept(rt_cur->proc, (int)FR(0));
}

static long sys_connect(u64 *frame)
{
    if (!rt_cur->proc)
        return -ES_EINVAL;
    return sys_sockpath(frame, 1);
}

static long sys_sendto(u64 *frame)
{
    const char *ub = (const char *)FR(1);
    u64 len = FR(2);
    char kbuf[64];

    if (len > sizeof(kbuf))
        len = sizeof(kbuf);
    if (len && !access_ok(ub, (u32)len))
        return -ES_EFAULT;
    for (u64 i = 0; i < len; i++)
        kbuf[i] = ub[i];
    return rt_fd_write(rt_cur->proc, (int)FR(0), kbuf, (u32)len);
}

static long sys_recvfrom(u64 *frame)
{
    char *ub = (char *)FR(1);
    u64 len = FR(2);
    char kbuf[64];
    long r;

    if (len > sizeof(kbuf))
        len = sizeof(kbuf);
    if (!access_ok(ub, (u32)len))
        return -ES_EFAULT;
    r = rt_fd_read(rt_cur->proc, (int)FR(0), kbuf, (u32)len);
    if (r <= 0)
        return r;
    for (long i = 0; i < r; i++)
        ub[i] = kbuf[i];
    return r;
}

static long sys_shutdown(u64 *frame)
{
    if (!rt_cur->proc)
        return -ES_EINVAL;
    return rt_fd_shutdown(rt_cur->proc, (int)FR(0), (long)FR(1));
}

void rt_syscall_init(void)
{
    rt_task_pool_init();              /* 站35:素槽 RT_FREE 清池 */
    for (u32 i = 0; i < ES_NR_MAX; i++)
        table[i] = sys_ni;
    table[ES_NR_read]    = sys_read;
    table[ES_NR_write]   = sys_write;
    table[ES_NR_getpid]  = sys_getpid;
    table[ES_NR_sysinfo] = sys_sysinfo;
    table[ES_NR_hvc]     = sys_hvc;
    table[ES_NR_demo]    = sys_demo;
    /* 站29:进程+地址空间面 */
    table[ES_NR_exit]       = sys_exit;
    table[ES_NR_mmap]       = sys_mmap;
    table[ES_NR_munmap]     = sys_munmap;
    table[ES_NR_wait4]      = sys_wait4;
    table[ES_NR_fork]       = sys_fork;
    table[ES_NR_exec_setup] = sys_exec_setup;
    /* 站30:消息 IPC 面 */
    table[ES_NR_ch_create]   = sys_ch_create;
    table[ES_NR_ch_destroy]  = sys_ch_destroy;
    table[ES_NR_name_attach] = sys_name_attach;
    table[ES_NR_name_detach] = sys_name_detach;
    table[ES_NR_name_open]   = sys_name_open;
    table[ES_NR_conn_detach] = sys_conn_detach;
    table[ES_NR_msg_send]    = sys_msg_send;
    table[ES_NR_msg_recv]    = sys_msg_recv;
    table[ES_NR_msg_reply]   = sys_msg_reply;
    table[ES_NR_msg_pulse]   = sys_msg_pulse;
    /* 站31:fd=通道三元组面(fdtable->resmgr) */
    table[ES_NR_fdopen]      = sys_fdopen;
    table[ES_NR_fdclose]     = sys_fdclose;
    table[ES_NR_fdread]      = sys_fdread;
    table[ES_NR_fdwrite]     = sys_fdwrite;
    table[ES_NR_shm_create]  = sys_shm_create;
    table[ES_NR_shm_attach]  = sys_shm_attach;
    table[ES_NR_shm_detach]  = sys_shm_detach;
    table[ES_NR_shm_destroy] = sys_shm_destroy;
    table[ES_NR_mq_open]     = sys_mq_open;
    table[ES_NR_mq_send]     = sys_mq_send;
    table[ES_NR_mq_receive]  = sys_mq_receive;
    table[ES_NR_mq_close]    = sys_mq_close;
    table[ES_NR_mq_unlink]   = sys_mq_unlink;
    table[ES_NR_msg_read]    = sys_msg_read;
    table[ES_NR_pipe]        = sys_pipe;
    table[ES_NR_fdcreate]    = sys_fdcreate;
    table[ES_NR_fdunlink]    = sys_fdunlink;
    table[ES_NR_fddir]       = sys_fddir;
    /* 站32:signal+futex 面(真 Linux 号,musl 直连) */
    table[ES_NR_futex]        = sys_futex;
    table[ES_NR_kill]         = sys_kill;
    table[ES_NR_rt_sigaction] = sys_sigaction;
    table[ES_NR_rt_sigreturn] = sys_rt_sigreturn;
    table[ES_NR_rt_sigtimedwait] = sys_sigtimedwait;
    table[ES_NR_rt_sigqueueinfo] = sys_sigqueueinfo;
    /* 站33:musl 薄面(exit_group 与 exit 同路=帧判据共面) */
    table[ES_NR_readv]           = sys_readv;    /* 站59:musl stdio 读面 */
    table[ES_NR_writev]          = sys_writev;
    table[ES_NR_ioctl]           = sys_ioctl;
    table[ES_NR_exit_group]      = sys_exit;
    table[ES_NR_set_tid_address] = sys_set_tid_address;
    table[ES_NR_rt_sigprocmask]  = sys_rt_sigprocmask;
    table[ES_NR_lseek]           = sys_lseek;
    /* 站35:线程面(真 Linux 号,musl pthread 直连) */
    table[ES_NR_gettid]          = sys_gettid;
    table[ES_NR_clone]           = sys_clone;
    /* 站40:busybox/服务收编薄面(qemu-user -strace 定谳的需求面) */
    table[ES_NR_dup3]            = sys_dup3;
    table[ES_NR_fcntl]           = sys_fcntl;
    table[ES_NR_openat]          = sys_openat;
    table[ES_NR_close]           = sys_close;
    table[ES_NR_getdents64]      = sys_getdents64;
    table[ES_NR_newfstatat]      = sys_newfstatat;
    table[ES_NR_setuid]          = sys_uid0;
    table[ES_NR_setgid]          = sys_uid0;
    table[ES_NR_clock_gettime]   = sys_clock_gettime;
    table[ES_NR_nanosleep]       = sys_nanosleep;   /* 站60:cyclictest 面 */
    table[ES_NR_clock_getres]    = sys_clock_getres;
    table[ES_NR_gettimeofday]    = sys_gettimeofday;
    table[ES_NR_getuid]          = sys_uid0;
    table[ES_NR_geteuid]         = sys_uid0;
    table[ES_NR_getgid]          = sys_uid0;
    table[ES_NR_getegid]         = sys_uid0;
    table[ES_NR_brk]             = sys_brk;
    table[ES_NR_getrandom]       = sys_getrandom;
    table[ES_NR_ppoll]           = sys_ppoll;
    /* 站41:CAPR 面 */
    table[ES_NR_cap_drop]        = sys_cap_drop;
    table[ES_NR_cap_get]         = sys_cap_get;
    table[ES_NR_uuid_get]        = sys_uuid_get;
    table[ES_NR_rlimit_self]     = sys_rlimit_self;
    /* 站42:syscall 面补全 */
    table[ES_NR_sched_setparam]  = sys_sched_setparam;
    table[ES_NR_sched_setscheduler] = sys_sched_setscheduler;
    table[ES_NR_sched_getscheduler] = sys_sched_getscheduler;
    table[ES_NR_sched_getparam]  = sys_sched_getparam;
    table[ES_NR_sched_setaffinity] = sys_sched_setaffinity;
    table[ES_NR_sched_getaffinity] = sys_sched_getaffinity;
    table[ES_NR_sched_yield]      = sys_sched_yield;
    table[ES_NR_sched_get_priority_max] = sys_sched_get_priority_max;
    table[ES_NR_sched_get_priority_min] = sys_sched_get_priority_min;
    table[ES_NR_sched_rr_get_interval] = sys_sched_rr_get_interval;
    table[ES_NR_getcpu]          = sys_getcpu;
    table[ES_NR_setpriority]     = sys_setpriority;
    table[ES_NR_getpriority]     = sys_getpriority;
    table[ES_NR_rt_sigpending]   = sys_rt_sigpending;
    table[ES_NR_rt_sigsuspend]   = sys_rt_sigsuspend;
    table[ES_NR_sigaltstack]     = sys_sigaltstack;
    table[ES_NR_setsid]          = sys_setsid;
    table[ES_NR_getsid]          = sys_getsid;
    table[ES_NR_setpgid]         = sys_setpgid;
    table[ES_NR_getpgid]         = sys_getpgid;
    table[ES_NR_set_robust_list] = sys_set_robust_list;
    table[ES_NR_get_robust_list] = sys_get_robust_list;
    table[ES_NR_uname]           = sys_uname;
    table[ES_NR_timer_create]    = sys_timer_create;
    table[ES_NR_timer_gettime]   = sys_timer_gettime;
    table[ES_NR_timer_getoverrun] = sys_timer_getoverrun;
    table[ES_NR_timer_settime]   = sys_timer_settime;
    table[ES_NR_timer_delete]    = sys_timer_delete;
    table[ES_NR_getitimer]       = sys_getitimer;
    table[ES_NR_setitimer]       = sys_setitimer;
    /* 站57:AF_UNIX sock 真号面(musl 直连) */
    table[ES_NR_socket]          = sys_socket;
    table[ES_NR_bind]            = sys_bind;
    table[ES_NR_listen]          = sys_listen;
    table[ES_NR_accept]          = sys_accept;
    table[ES_NR_connect]          = sys_connect;
    table[ES_NR_sendto]          = sys_sendto;
    table[ES_NR_recvfrom]        = sys_recvfrom;
    table[ES_NR_shutdown]        = sys_shutdown;
}

void rt_syscall_dispatch(u64 *frame)
{
    u64 nr = FR(8);
    long r = (nr < ES_NR_MAX) ? (long)table[nr](frame)
                              : -(long)ES_ENOSYS;

    if (r == ES_ERESTARTSYS) {
        /* 站44:可重启语义帧手术——elr 回拨 4 重取 svc(ELR 本指
         * svc 下一指令);x0 槽不写保入参=重执行同参再入。随后的
         * 出口投递整帧存 sig_saved(含回拨 elr+原 x0),handler
         * 归还即重执行;无 handler=默认动作终局,回拨无害。
         * 帧手术完整性清单:动 elr 一处免写 x0 一处,帧外态不动。 */
        frame[0] -= 4;
    } else {
        FR(0) = (u64)r;
    }
    {   /* 站32:syscall 出口=投递点(参考实现 SWITCH_OUT 的站内形态;
         * 返回值已在 x0 槽,帧手术先整帧存后改写)。站38:无 handler
         * 的默认动作=终局——投递返 128+sig 即以退出码走 sys_exit
         * 同链(fd 清+线程群扫+进程退+收尸唤醒),帧永不再 eret。 */
        int term = rt_sig_deliver(frame);

        if (term) {
            frame[2] = (u64)(long)term;  /* x0 槽=退出码(sys_exit
                                          * 读 FR(0);帧不再 eret) */
            rt_crash_record(rt_cur->proc, frame, 0, 0, 0,
                            (u32)term);  /* 站46:信号路径记录
                                          * (无硬件三元=0 面) */
            sys_exit(frame);
            __builtin_unreachable();
        }
    }
}

/* EL0 非 SVC 异常的 v0 诚实终态面(29+ per-process 缺页接管后退役):
 * 裸打(站21B panic 语境取锁=同核自死锁教训)——打印 ESR/ELR/FAR
 * 后走 rt_panic 扫描面。 */
void rt_el0_fault(u64 *frame)
{
    u64 esr, far;

    __asm__ volatile("mrs %0, esr_el1" : "=r"(esr));
    __asm__ volatile("mrs %0, far_el1" : "=r"(far));
    /* 站33:FP 陷阱先缺页判——musl memcpy/printf 的 NEON 首触在
     * EL0(EC 0x07),落此必须走 per-task FP 轮转而非终态面
     * (冒烟定谳:只挂 EL1 向量时 musl 一触即 panic)。 */
    if ((esr >> 26) == 0x07) {
        rt_fp_trap_cur();
        return;                        /* RT_EL0_RET 原地重执行 */
    }
    /* 站29:翻译错(FSC 0x04..0x07)先走缺页接管——VMA 命中即补页,
     * 帧未动,调用侧 RT_EL0_RET 原地重执行即中;VMA 外野访与
     * 其余 FSC(权限/外部等)原样落终态面(v0 边界不松)。 */
    {
        u32 fsc = (u32)(esr & 0x3Fu);

        if (fsc >= 0x04 && fsc <= 0x07 && rt_pfault(far, esr) == 0)
            return;
    }
    rt_puts_hex("EL0 fault: esr=0x", esr,
                " (wild: no VMA covers)\n");
    rt_puts_hex("EL0 fault: elr=0x", frame[0], "\n");
    rt_puts_hex("EL0 fault: far=0x", far, "\n");
    /* 站46:站29"野访终态面留边"翻案——死者本人 exit 链+取证
     * 记录(崩溃不再连坐全 VM);EL1 panic 面不动(内核 bug 仍
     * 全驻车)。code=139=128+11(SIGSEGV 面),同信号终局机器。 */
    rt_crash_record(rt_cur->proc, frame, esr, frame[0], far, 139u);
    frame[2] = 139;                    /* x0 槽=退出码(sys_exit 读 FR(0)) */
    sys_exit(frame);
    __builtin_unreachable();
}
