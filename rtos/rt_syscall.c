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

static long sys_exit(u64 *frame)
{
    long code = (long)FR(0);
    struct rt_proc *p = rt_cur->proc;

    if (p) {
        /* 站31:逐 fd CLOSE(服务器 ocb 引用减+断连)——死亡关流;
         * rt_cur=死者本人,阻塞往返合法(仅此处调,ISR 无涉) */
        rt_fd_close_all(p);
        rt_proc_exit(p, (int)code);       /* 页池回收+L2 清+TLB 冲 */
        rt_task_wake_all(RT_PROC_BIT);    /* 父 wait4 的收尸唤醒 */
    }
    rt_task_exit();                      /* 任务 DEAD+切走,noreturn */
    __builtin_unreachable();
}

static long sys_mmap(u64 *frame)
{
    u64 len = FR(1);                     /* x0=addr hint(v0 忽略) */
    u64 va;

    if (!rt_cur->proc)
        return -ES_EINVAL;
    va = rt_as_mmap(rt_cur->proc, len);
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
    for (;;) {
        z = rt_proc_zombie_find((int)pid, &code);
        if (z)
            break;
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
    if (rt_proc_create(NULL, &cp) < 0)
        return -ES_EAGAIN;
    if (rt_proc_fork_as(pp, cp) < 0) {
        rt_proc_reap(cp);                /* 表无映射,槽回收不伤人 */
        return -ES_ENOMEM;
    }
    cp->state = RT_PROC_RUN;
    cp->parent = pp->pid;
    rt_fd_fork(pp, cp);                /* 站31:fdtable 组拷+双侧引用 */
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
    cp->leader = ct;
    return (long)cp->pid;
}

static long sys_exec_setup(u64 *frame)
{
    /* 帧改写(参考实现 EXEC_SETUP 形状,内核不读任何镜像字节):
     * v0 入参 x0=entry x1=sp x2=arg;x3-x5 不读(壳 wrapper 只
     * 3 参;impl 侧整帧卫生清零,残值不进新帧)。 */
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
    char kbuf[56];
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

static void k_ps(void)
{
    unsigned int idx;
    rt_task_t *t = rt_ps_first(&idx);
    rt_puts("ps: task table (prio 0 high)\n");
    while (t) {
        if (t->entry && t->state != RT_DEAD) {
            rt_puts("  ");
            rt_puts(t->name);
            rt_puts(" prio ");
            rt_puts_u64("", t->prio, "");
            rt_puts("  ");
            rt_puts(rt_state_name(t->state));
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
        if (t->entry && t->state != RT_DEAD && idx < RT_POOL_MAX) {
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
    case RT_U_ABORTDEMO:
        if (!rt_task_create("abortsrv", rt_msg_abort_server, NULL, 8))
            rt_puts("ipc: abort server full\n");
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

void rt_syscall_init(void)
{
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
}

void rt_syscall_dispatch(u64 *frame)
{
    u64 nr = FR(8);

    FR(0) = (nr < ES_NR_MAX) ? (u64)table[nr](frame) : (u64)-ES_ENOSYS;
}

/* EL0 非 SVC 异常的 v0 诚实终态面(29+ per-process 缺页接管后退役):
 * 裸打(站21B panic 语境取锁=同核自死锁教训)——打印 ESR/ELR/FAR
 * 后走 rt_panic 扫描面。 */
void rt_el0_fault(u64 *frame)
{
    u64 esr, far;

    __asm__ volatile("mrs %0, esr_el1" : "=r"(esr));
    __asm__ volatile("mrs %0, far_el1" : "=r"(far));
    /* 站29:翻译错(FSC 0x04..0x07)先走缺页接管——VMA 命中即补页,
     * 帧未动,调用侧 RT_EL0_RET 原地重执行即中;VMA 外野访与
     * 其余 FSC(权限/外部等)原样落终态面(v0 边界不松)。 */
    {
        u32 fsc = (u32)(esr & 0x3Fu);

        if (fsc >= 0x04 && fsc <= 0x07 && rt_pfault(far, esr) == 0)
            return;
    }
    rt_puts_hex("EL0 fault: esr=0x", esr,
                " (unmapped/wild: no VMA covers, terminal)\n");
    rt_puts_hex("EL0 fault: elr=0x", frame[0], "\n");
    rt_puts_hex("EL0 fault: far=0x", far, "\n");
    rt_panic("EL0 sync fault (v0 terminal face)",
             (unsigned int)(rt_pid_of(rt_cur) - 1));
}
