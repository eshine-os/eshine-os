/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "rtos.h"

/* 站二十八:EL0 用户壳。整目标文件经 linker .user 段落用户窗
 * (0x40200000+,两区式 stage-1 的 EL0+EL1 面)——本文件不得引用
 * 任何内核体(rt_puts/uart/demo 体全在内核段,引用即 EL0 取指/
 * 访存缺页→诚实终态面):
 *   - I/O 全走 syscall:输出=sys_write(ES_NR_write),输入=
 *     sys_read(ES_NR_read,阻塞取 1 字节,内核侧复用站 17
 *     rt_block_rx 的 DAIF 窗+RT_RX_BIT 机器=站 24 输入⇒runnable
 *     不变量在新链原样成立);
 *   - yield/host/exit 走 sys_hvc(参考实现 超管调用 同
 *     位:用户态 HVC 不可达,内核代理);
 *   - 其余命令走 sys_demo 蹦床(内核语境跑既有体,80 门判据
 *     逐字节不变;30+ 各面 syscall 化后逐批迁本侧);
 *   - echo/utest 本侧直跑(纯用户代码)。
 * 原始 rt_shell.c 的命令面与输出串逐字保留;ps/selftest/stackcheck
 * 等体移 rt_syscall.c(内核镜像蹦床)。 */

#define RT_BUF_MAX 96

static char linebuf[RT_BUF_MAX];

static int streq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static int starts(const char *s, const char *pfx)
{
    while (*pfx) {
        if (*s++ != *pfx++)
            return 0;
    }
    return 1;
}

/* ---- syscall 薄壳(号 x8/参 x0-x5/返 x0,负 errno) ----
 * clobber 契约:内核侧分发是 C 调用链,合法踩遍 x0-x18
 * (caller-saved;x19-x30 由内核帧契约保存,x9-x11 另经蹦床
 * 回填保真)。wrapper 若只声明 x0/x1/x8,GCC 会把活值寄放
 * x12-x18 横跨 svc——站28 冒烟定谳:idx 野值(strb 落内核区
 * →EL0 权限故障→诚实收尸,确定性复现)。故 x3-x18 全列入
 * clobber,杜绝假存活。 */
#define ES_CLOBBERS "x3", "x4", "x5", "x6", "x7", "x9", "x10", \
                    "x11", "x12", "x13", "x14", "x15", "x16",   \
                    "x17", "x18", "memory"

static long sys0(long nr)
{
    register long x0 __asm__("x0");
    register long x8 __asm__("x8") = nr;

    __asm__ volatile("svc #0" : "=r"(x0) : "r"(x8) : ES_CLOBBERS);
    return x0;
}

static long sys1(long nr, long a)
{
    register long x0 __asm__("x0") = a;
    register long x8 __asm__("x8") = nr;

    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8) : ES_CLOBBERS);
    return x0;
}

static long sys2(long nr, long a, long b)
{
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    register long x8 __asm__("x8") = nr;

    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x8) : ES_CLOBBERS);
    return x0;
}

static long sys3(long nr, long a, long b, long c)
{
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    register long x2 __asm__("x2") = c;
    register long x8 __asm__("x8") = nr;

    __asm__ volatile("svc #0" : "+r"(x0)
                     : "r"(x1), "r"(x2), "r"(x8) : ES_CLOBBERS);
    return x0;
}

/* 站32:四参裹(futex 的 uaddr/op/val/timeout;x3 入参同 sys6
 * 纪律:入参不得再列 clobber,剩余 caller-saved x4-x18 照列) */
static long sys4(long nr, long a, long b, long c, long d)
{
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    register long x2 __asm__("x2") = c;
    register long x3 __asm__("x3") = d;
    register long x8 __asm__("x8") = nr;

    __asm__ volatile("svc #0" : "+r"(x0)
                     : "r"(x1), "r"(x2), "r"(x3), "r"(x8)
                     : "x4", "x5", "x6", "x7", "x9", "x10", "x11",
                       "x12", "x13", "x14", "x15", "x16", "x17",
                       "x18", "memory");
    return x0;
}

/* 站30:六参裹(make send 的 coid/sbuf/slen/rbuf/rlen/timeout 恰满
 * x0-x5——站 28 ABI 的六参上限,IPC 面即其首个满额用户) */
static long sys6(long nr, long a, long b, long c, long d, long e, long f)
{
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    register long x2 __asm__("x2") = c;
    register long x3 __asm__("x3") = d;
    register long x4 __asm__("x4") = e;
    register long x5 __asm__("x5") = f;
    register long x8 __asm__("x8") = nr;

    /* x3-x5 已是入参,不得再列 clobber(与入参冲突,gcc 报错);
     * 六参满额后剩余 caller-saved = x6-x18 照列 */
    __asm__ volatile("svc #0" : "+r"(x0)
                     : "r"(x1), "r"(x2), "r"(x3), "r"(x4),
                       "r"(x5), "r"(x8)
                     : "x6", "x7", "x9", "x10", "x11", "x12",
                       "x13", "x14", "x15", "x16", "x17", "x18",
                       "memory");
    return x0;
}

/* x9-x11 caller-saved:ABI 约定跨调用不保(帧契约见 vec.S 注) */
static void sys_puts(const char *s)
{
    unsigned long n = 0;

    while (s[n])
        n++;
    sys3(ES_NR_write, 1, (long)s, (long)n);
}

static void sys_put_dec(u64 v)
{
    char digits[24];
    unsigned int i = sizeof(digits) - 1;
    unsigned int n = 0, j;

    digits[i] = '\0';
    do {
        digits[--i] = (char)('0' + (v % 10));
        v /= 10;
    } while (v);
    for (j = i; digits[j] && n < sizeof(digits) - 1; j++)
        digits[n++] = digits[j];
    digits[n] = '\0';
    sys_puts(digits + i);           /* 就地起:digits+i 处已组好串 */
}

/* ---- utest:EL0 syscall 面一门全验 ---- */

static void cmd_utest(void)
{
    long pid, ni, r;
    struct rt_sysinfo si;
    int ok = 1;

    /* EL0 自证改道(站28 收口定谳):首版 mrs CurrentEL 在 EL0 落
     * UNDEF(EC 0;ARMv8 ID 类寄存器 EL1+ 方可读,a57 诚实行刑,
     * EL0 fault far=0 现场实证)——EL0 存在性证词改由 syscall 面
     * 本身出:SVC 仅 EL0 有正当通道(EL1 的 0x15 走 rt_sync 未知
     * EC 停车面),getpid>0/NI -ENOSYS/sysinfo 回填三判据在此面
     * 即自证。 */
    pid = sys1(ES_NR_getpid, 0);
    ni = sys0(650);                 /* 未注册号:ni 面期望 -ENOSYS */
    r = sys2(ES_NR_sysinfo, (long)&si, (long)sizeof(si));
    if (pid <= 0 || ni != -ES_ENOSYS || r != 0 ||
        si.tick == 0 || si.ver[0] == '\0')
        ok = 0;
    sys_puts("utest: pid=");
    sys_put_dec((u64)pid);
    sys_puts(" ni=");
    sys_put_dec((u64)(-ni));         /* 38=|ENOSYS|,展示值 */
    sys_puts(" tick=");
    sys_put_dec(si.tick);
    sys_puts(" ver=");
    sys_puts(si.ver);
    sys_puts("\n");
    if (ok)
        sys_puts("EL0 syscall OK\n");
    else
        sys_puts("EL0 syscall FAIL\n");
}

/* ---- 站29:进程三 demo(fork/mmap/exec) ---- */

static void cmd_forkdemo(void)
{
    long r = sys0(ES_NR_fork);

    if (r == (long)RT_FORK_MAGIC) {        /* COPY_UCONTEXT 子支 */
        sys_puts("fork: child alive\n");
        sys1(ES_NR_exit, 42);              /* 进程退场,noreturn */
    }
    if (r < 0) {
        sys_puts("forkdemo: fork failed\n");
        return;
    }
    {
        u64 st = 0;
        long pid = sys3(ES_NR_wait4, r, (long)&st, 0);

        sys_puts("fork: reaped pid=");
        sys_put_dec((u64)pid);
        sys_puts(" code=");
        sys_put_dec(st);
        sys_puts("\n");
    }
}

static void cmd_mmapdemo(void)
{
    long va = sys2(ES_NR_mmap, 0, 4096);
    volatile u64 *p;

    if (va <= 0) {
        sys_puts("mmapdemo: mmap failed\n");
        return;
    }
    p = (volatile u64 *)va;               /* 首写=缺页零填自证 */
    *p = 0x5A5A1234ULL;
    if (*p == 0x5A5A1234ULL)
        sys_puts("mmap: lazy zero+rw ok\n");
    else
        sys_puts("mmap: rw FAIL\n");
    sys2(ES_NR_munmap, va, 4096);
    sys_puts("mmapdemo: unmapped\n");
}

/* exec 二相演示体:帧改写后的新入口(x0=arg 落点自证);不经
 * return——x30 语义已随帧改写退役,演毕 exec 回壳任务续行。 */
static void exe_phase2(void *arg)
{
    sys_puts("exec: phase2 entered arg=");
    sys_put_dec((u64)arg);
    sys_puts("\n");
    sys3(ES_NR_exec_setup, (u64)rt_shell_task,
         (u64)(rt_shell_ustack + sizeof(rt_shell_ustack)), 0);
}

static void cmd_exedemo(void)
{
    volatile u64 anchor = 0;

    sys3(ES_NR_exec_setup, (u64)exe_phase2,
         (u64)&anchor - 256, 0x2A);
    sys_puts("exec: NOT REACHED\n");     /* 帧已改写,此行不达 */
}

/* ---- 站30:IPC 客户端三命令(壳=用户态客户端,微内核形状本尊) ---- */

static long ipc_open_wait(const char *path)
{
    long coid;
    int i;

    for (i = 0; i < 10; i++) {
        coid = sys1(ES_NR_name_open, (long)path);
        if (coid > 0)
            return coid;
        sys2(ES_NR_demo, RT_U_SLEEP, 1);
    }
    return coid;
}

static void cmd_ipcdemo(void)
{
    struct rt_sysinfo si;
    long coid, v = 41, rv = 0, r;
    u64 t0, t1;

    sys2(ES_NR_demo, RT_U_IPCDEMO, 0);        /* spawn 服务器+干扰 */
    coid = ipc_open_wait("/ipc/demo");
    if (coid <= 0) {
        sys_puts("ipcdemo: server not found\n");
        return;
    }
    sys2(ES_NR_sysinfo, (long)&si, (long)sizeof(si));
    t0 = si.tick;
    /* 三段语义:返回值=REPLY status(0),回执数据落 rv——两者分家
     * (曾共用一变量,内核回写 rv 后又被返回值覆写,reply 0 定谳) */
    r = sys6(ES_NR_msg_send, coid, (long)&v, sizeof(v),
             (long)&rv, sizeof(rv), 100);
    sys2(ES_NR_sysinfo, (long)&si, (long)sizeof(si));
    t1 = si.tick;
    if (r == 0) {
        sys_puts("ipcdemo: reply ");
        sys_put_dec((u64)rv);
        sys_puts(" dt=");
        sys_put_dec(t1 - t0);
        sys_puts(" ticks\n");
    } else {
        sys_puts("ipcdemo: send failed ");
        sys_put_dec((u64)r);
        sys_puts("\n");
    }
    sys1(ES_NR_conn_detach, coid);
}

static void cmd_pulsedemo(void)
{
    long coid;
    int i;

    sys2(ES_NR_demo, RT_U_PULSEDEMO, 0);     /* spawn 脉冲服务器 */
    coid = ipc_open_wait("/ipc/pulse");
    if (coid <= 0) {
        sys_puts("pulsedemo: server not found\n");
        return;
    }
    for (i = 0; i < 5; i++)
        sys3(ES_NR_msg_pulse, coid, i, (long)i * 10);
    sys_puts("pulsedemo: fired 5\n");
    sys2(ES_NR_demo, RT_U_SLEEP, 1);        /* 让服务器排空+自报 */
    sys1(ES_NR_conn_detach, coid);
}

static void cmd_abortdemo(void)
{
    long coid, r = 0, v = 1;

    sys2(ES_NR_demo, RT_U_ABORTDEMO, 0);     /* spawn 占住服务器 */
    coid = ipc_open_wait("/ipc/abort");
    if (coid <= 0) {
        sys_puts("abortdemo: server not found\n");
        return;
    }
    r = sys6(ES_NR_msg_send, coid, (long)&v, sizeof(v),
             (long)&r, sizeof(r), 30);        /* 30 拍无回执=超时面 */
    if (r == -110)
        sys_puts("abortdemo: send timed out (-110)\n");
    else
        sys_puts("abortdemo: unexpected reply\n");
    sys2(ES_NR_demo, RT_U_SLEEP, 1);        /* 服务器收 UNBLOCK 自报 */
    sys1(ES_NR_conn_detach, coid);
}

/* ---- 命令面(输出串与原壳逐字一致——门禁判据锚) ---- */

/* ---- 站32:signal+futex 客户端 ---- */

static u32 fx_word = 1;     /* futex 键字(.user 数据,fork 时随页拷) */
static struct { long sec, nsec; } fx_ts;   /* 站35 契约:timespec 面 */

static void cmd_futexdemo(void)
{
    struct rt_sysinfo si;
    long r;
    u64 t0, t1;

    /* 站35:mismatch 面(NULL=无限等,值错入口即 -EAGAIN 不涉窗) */
    r = sys4(ES_NR_futex, (long)&fx_word, RT_FUTEX_WAIT, 2, 0);
    sys_puts(r == -11 ? "futexdemo: mismatch -> -EAGAIN ok\n"
                      : "futexdemo: mismatch FAIL\n");
    r = sys4(ES_NR_futex, (long)&fx_word, RT_FUTEX_WAKE, 1, 0);
    sys_puts(r == 0 ? "futexdemo: wake no-waiters -> 0 ok\n"
                    : "futexdemo: wake FAIL\n");
    /* 真实等待->唤醒对:扑克代演同址空间第二执行流(真 threads=
     * 站 33+);扑克只做队列唤醒,不碰用户字 */
    sys2(ES_NR_sysinfo, (long)&si, (long)sizeof(si));
    t0 = si.tick;
    sys3(ES_NR_demo, RT_U_FUPOKE, (long)&fx_word, 30);
    r = sys4(ES_NR_futex, (long)&fx_word, RT_FUTEX_WAIT, 1, 0); /* NULL=无限 */
    sys2(ES_NR_sysinfo, (long)&si, (long)sizeof(si));
    t1 = si.tick;
    if (r == 0) {
        sys_puts("futexdemo: poke pair r=0 dt=");
        sys_put_dec(t1 - t0);
        sys_puts("\n");
    } else {
        sys_puts("futexdemo: poke pair FAIL\n");
    }
    /* 超时真阻塞:无人唤醒,600ms(timespec)到点=60 拍 */
    fx_ts.sec = 0;
    fx_ts.nsec = 600000000L;
    sys2(ES_NR_sysinfo, (long)&si, (long)sizeof(si));
    t0 = si.tick;
    r = sys4(ES_NR_futex, (long)&fx_word, RT_FUTEX_WAIT, 1, (long)&fx_ts);
    sys2(ES_NR_sysinfo, (long)&si, (long)sizeof(si));
    t1 = si.tick;
    if (r == -110) {
        sys_puts("futexdemo: timeout r=-110 dt=");
        sys_put_dec(t1 - t0);
        sys_puts("\n");
    } else {
        sys_puts("futexdemo: timeout FAIL\n");
    }
}

/* handler:在子进程自身用户上下文执行(投递帧手术),末尾显式
 * rt_sigreturn——svc 不归:内核整帧回拷,eret 直落回打断点。 */
static void sig_handler(long sig)
{
    sys_puts("sig: handler ran sig=");
    sys_put_dec((u64)sig);
    sys_puts("\n");
    sys0(ES_NR_rt_sigreturn);
}

static void cmd_sigdemo(void)
{
    long pid, r, st = 0;
    static u32 sig_word;
    /* 站35 契约垫背:x3=timespec 指针(旧 400=ticks 字面在新契约下
     * 被当指针解,access_ok 拒->-EFAULT,-EINTR 脸必破)。10s/1s
     * 父 kill 十倍余;fork 前置值,子承 .user 页拷同 VA。 */
    static struct { long sec, nsec; } sig_ts;

    sys2(ES_NR_rt_sigaction, 10, (long)sig_handler);
    sig_ts.sec = 10;
    sig_ts.nsec = 0;
    pid = sys0(ES_NR_fork);
    if (pid == (long)RT_FORK_MAGIC) {   /* COPY_UCONTEXT 子支(站 29
                                         * 契约:子返魔数非 0——首版
                                         * 写 pid==0 子落穿父路径,
                                         * 魔数当 pid 永挂,冒烟定谳) */
        /* 子:handler 已继承(eager 拷贝同 VA,地址语义红利);
         * 挂 futex 静候父信号——跨进程打断=EINTR 判据面 */
        r = sys4(ES_NR_futex, (long)&sig_word, RT_FUTEX_WAIT, 0, (long)&sig_ts);
        /* 取证:数值面先于三元(r 似被搅,字面非本函数任一支) */
        if (r < 0) {
            sys_puts("sig: futex r=-");
            sys_put_dec((u64)(-r));
            sys_puts("\n");
        } else {
            sys_puts("sig: futex r=");
            sys_put_dec((u64)r);
            sys_puts("\n");
        }
        sys_puts(r == -4 ? "sig: futex -> -EINTR ok\n"
                         : "sig: futex FAIL\n");
        sys1(ES_NR_exit, 7);
    }
    sys_puts("sigdemo: child waiting\n");
    sys3(ES_NR_demo, RT_U_SLEEP, 1, 0);      /* 1s:子先挂上 */
    sys2(ES_NR_kill, pid, 10);
    pid = sys3(ES_NR_wait4, pid, (long)&st, 0);
    sys_puts(st == 7 ? "sigdemo: reaped code=7 ok\n"
                     : "sigdemo: reap FAIL\n");
}

/* ---- 站36:shm+xspace 客户端——共享段四面+固定窗+fork 共享律。
 * shmdemo:fork 共享判别器(子写同 VA,父读回=eager 拷会把子
 * 写私有化,父读 0=FAIL);msgsegdemo:VM_SET 单拷脸(1KB 载荷
 * 走共享窗=一拷,脉冲 8B 只带段引用,父 attach 后原地零拷读)。 */
static void cmd_shmdemo(void)
{
    long seg, va, pid, r;
    int st = 0;
    volatile u64 *w;

    seg = sys1(ES_NR_shm_create, 0x2000);
    if (seg <= 0) {
        sys_puts("shmdemo: create FAIL\n");
        return;
    }
    va = sys2(ES_NR_shm_attach, seg, 0);
    if (va != (long)RT_XSPACE_BASE) {
        sys_puts("shmdemo: attach FAIL\n");
        return;
    }
    sys_puts("shmdemo: va=0x41000000\n");
    w = (volatile u64 *)(unsigned long)va;
    w[0] = 0xABCDEF01UL;                    /* 父标记 */
    pid = sys0(ES_NR_fork);
    if (pid == (long)RT_FORK_MAGIC) {       /* 子:继承 VMA 同 VA 写 */
        w[1] = 0x12345678UL;
        sys1(ES_NR_exit, 0);
    }
    sys3(ES_NR_wait4, pid, (long)&st, 0);
    sys_puts(w[0] == 0xABCDEF01UL && w[1] == 0x12345678UL
             ? "shmdemo: fork shared ok, p2=305419896\n"
             : "shmdemo: share FAIL\n");
    r = sys1(ES_NR_shm_detach, seg);
    if (r == 0)
        r = sys1(ES_NR_shm_destroy, seg);
    sys_puts(r == 0 ? "shmdemo: destroyed ok\n"
                    : "shmdemo: teardown FAIL\n");
}

static void cmd_msgsegdemo(void)
{
    long chid, coid, pid, rcv, seg, va, r;
    int st = 0, i, ok = 1;
    volatile u64 *w;
    struct rt_recvinfo info;

    chid = sys1(ES_NR_ch_create, 0);
    if (chid <= 0) {
        sys_puts("msgseg: ch_create FAIL\n");
        return;
    }
    r = sys2(ES_NR_name_attach, (long)"/shm/msg", (long)chid);
    if (r < 0) {
        sys_puts("msgseg: name_attach FAIL\n");
        return;
    }
    coid = sys1(ES_NR_name_open, (long)"/shm/msg");
    if (coid <= 0) {
        sys_puts("msgseg: name_open FAIL\n");
        return;
    }
    pid = sys0(ES_NR_fork);
    if (pid == (long)RT_FORK_MAGIC) {       /* 子:自建段+写 1KB+
                                             * 继承 coid 发 8B 脉冲
                                             * 只带段引用(单拷窗) */
        seg = sys1(ES_NR_shm_create, 0x400);
        if (seg <= 0)
            sys1(ES_NR_exit, 1);
        va = sys2(ES_NR_shm_attach, seg, 0);
        if (va != (long)(RT_XSPACE_BASE + (u64)(seg - 1) * RT_XSPACE_SPAN))
            sys1(ES_NR_exit, 2);
        w = (volatile u64 *)(unsigned long)va;
        for (i = 0; i < 256; i++)
            w[i] = 0x5AA50000UL + (u64)i;
        sys3(ES_NR_msg_pulse, coid, 0x36, seg);
        sys1(ES_NR_exit, 0);
    }
    rcv = sys6(ES_NR_msg_recv, (long)chid, 0, 0, 100, (long)&info, 0);
    if (rcv != 0 || info.rcvid != 0 || info.code != 0x36) {
        sys_puts("msgseg: recv FAIL\n");
        return;
    }
    seg = (long)info.value;
    va = sys2(ES_NR_shm_attach, seg, 0);    /* 按引用挂:跨进程同 VA */
    w = (volatile u64 *)(unsigned long)va;
    for (i = 0; i < 256; i++)
        if (w[i] != 0x5AA50000UL + (u64)i)
            ok = 0;
    sys_puts(ok ? "msgseg: 1024B via window, wire=8B ok\n"
                : "msgseg: pattern FAIL\n");
    /* wait4 先于 detach/destroy:子退场走 fd-close 消息往返
     * (慢于父验证路),未死即 destroy=refs 仍 1->-EBUSY 竞面
     * (首轮冒烟实抓)。 */
    sys3(ES_NR_wait4, pid, (long)&st, 0);
    r = sys1(ES_NR_shm_detach, seg);
    if (r == 0)
        r = sys1(ES_NR_shm_destroy, seg);
    sys_puts(r == 0 ? "msgseg: destroyed ok\n"
                    : "msgseg: teardown FAIL\n");
    sys1(ES_NR_conn_detach, coid);
    sys2(ES_NR_name_detach, (long)"/shm/msg", (long)chid);
    sys1(ES_NR_ch_destroy, chid);
}

/* ---- 站37:posix_queue 弧客户端——mqueue 优先级/pipe 字节流
 * /长消息+MsgRead 镜像。 */
static void cmd_mqdemo(void)
{
    long mqd, pid, r;
    u64 v = 0;
    int st = 0;

    mqd = sys1(ES_NR_mq_open, (long)"/mq/demo");
    if (mqd <= 0) {
        sys_puts("mqdemo: open FAIL\n");
        return;
    }
    r = sys3(ES_NR_mq_receive, (long)&v, mqd, 0);   /* 空队 no-wait */
    sys_puts(r == -110 ? "mqdemo: empty no-wait -110 ok\n"
                       : "mqdemo: no-wait FAIL\n");
    pid = sys0(ES_NR_fork);
    if (pid == (long)RT_FORK_MAGIC) {
        sys3(ES_NR_mq_send, mqd, 5, 5);
        sys3(ES_NR_mq_send, mqd, 1, 1);
        sys3(ES_NR_mq_send, mqd, 3, 3);
        sys1(ES_NR_exit, 0);
    }
    sys3(ES_NR_wait4, pid, (long)&st, 0);
    {
        u64 v1 = 0, v2 = 0, v3 = 0;

        sys3(ES_NR_mq_receive, (long)&v1, mqd, 100);
        sys3(ES_NR_mq_receive, (long)&v2, mqd, 100);
        sys3(ES_NR_mq_receive, (long)&v3, mqd, 100);
        sys_puts(v1 == 5 && v2 == 3 && v3 == 1
                 ? "mqdemo: order 5 3 1 ok\n" : "mqdemo: order FAIL\n");
    }
    sys1(ES_NR_mq_close, mqd);
    r = sys1(ES_NR_mq_unlink, (long)"/mq/demo");
    sys_puts(r == 0 ? "mqdemo: unlinked ok\n"
                    : "mqdemo: unlink FAIL\n");
}

static void cmd_pipedemo(void)
{
    long r, wfd, rfd, n, pid;
    int st = 0;
    char buf[64];

    r = sys0(ES_NR_pipe);
    if (r < 0) {
        sys_puts("pipedemo: pipe FAIL\n");
        return;
    }
    rfd = r & 0xFFFF;
    wfd = (r >> 16) & 0xFFFF;
    sys_puts("pipedemo: fds ok\n");
    pid = sys0(ES_NR_fork);
    if (pid == (long)RT_FORK_MAGIC) {   /* 子:写 16B+双端关 */
        r = sys3(ES_NR_fdwrite, wfd, (long)"through the pipe", 16);
        if (r != 16) {
            sys_puts("pipedemo: child write r=");
            sys_put_dec((u64)(long)r);
            sys_puts("\n");
        }
        sys1(ES_NR_fdclose, wfd);
        sys1(ES_NR_fdclose, rfd);
        sys1(ES_NR_exit, 0);
    }
    sys1(ES_NR_fdclose, wfd);           /* 父关自持写端=EOF 可判 */
    sys3(ES_NR_wait4, pid, (long)&st, 0);
    n = sys3(ES_NR_fdread, rfd, (long)buf, 64);
    buf[16] = '\0';
    sys_puts(n == 16 ? "pipedemo: got 'through the pipe' ok\n"
                     : "pipedemo: read FAIL\n");
    n = sys3(ES_NR_fdread, rfd, (long)buf, 64);
    sys_puts(n == 0 ? "pipedemo: eof ok\n" : "pipedemo: eof FAIL\n");
    sys1(ES_NR_fdclose, rfd);
    sys_puts("pipedemo: closed ok\n");
}

static void cmd_longdemo(void)
{
    static char pat[1024];
    long coid, r;
    int i;

    for (i = 0; i < 1024; i++)
        pat[i] = (char)(i & 0xFF);
    sys2(ES_NR_demo, RT_U_LONGDEMO, 0);          /* spawn 服务器 */
    coid = ipc_open_wait("/ipc/long");
    if (coid <= 0) {
        sys_puts("longdemo: server not found\n");
        return;
    }
    r = sys6(ES_NR_msg_send, coid, (long)pat, 1024, 0, 0, 100);
    sys_puts(r == 42 ? "longdemo: reply 42 ok\n"
                     : "longdemo: reply FAIL\n");
    sys1(ES_NR_conn_detach, coid);
}

/* ---- 站33:用户态 loader(参考实现 "loader 在用户态"机制;内核零镜像
 * 字节红线保全)——`run <path>`:fork->子全用户态装 ELF(fd=通道
 * 三元组读 ramfs)hint-mmap 段+拷->栈块(argc/argv/envp/auxv)
 * ->exec_setup 七参帧改写;父 wait4 收 code。musl 零 patch:号面
 * =站 28-32 真 asm-generic,启动期薄面(writev/exit_group/
 * set_tid_address/ioctl ENOTTY/sigprocmask/lseek)在内核侧。 */

struct ux_ehdr {
    u8 e_ident[16];
    u16 type, machine;
    u32 version;
    u64 entry, phoff, shoff;
    u32 flags;
    u16 ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
};
struct ux_phdr {
    u32 type, flags;
    u64 offset, vaddr, paddr, filesz, memsz, align;
};

/* 段头驻 .user 静态区:AT_PHDR 指此(exec 后旧像恒驻=站33 v0
 * 边界,不撤销旧映射;phdr 在文件内偏移、段数据 0x10000 起,
 * 两不相属,故须在位副本)。 */
static struct ux_phdr ux_phs[8];

/* AT_RANDOM:无栈保护编译,值仅须可读——ELF 魔数头即"随机" */
static const u8 ux_rand[16] = { 0x7f, 0x45, 0x4c, 0x46, 1, 2, 3, 4,
                               5, 6, 7, 8, 9, 10, 11, 12 };

#define UX_STACK_HINT 0x40D00000UL   /* mmap 窗内,与段基 0x40C00000 不撞 */
#define UX_STACK_LEN  0x20000UL

static int ux_read_n(long fd, u8 *dst, u32 n)
{
    /* 64B 窗直读(fdread 的内核拷贝即落目标 VA——access_ok 域内) */
    while (n) {
        u32 want = n > 64 ? 64 : n;
        long r = sys3(ES_NR_fdread, fd, (long)dst, (long)want);

        if (r <= 0)
            return -1;
        dst += r;
        n -= (u32)r;
    }
    return 0;
}

static int ux_copy_seg(long fd, u64 vaddr, u32 filesz)
{
    while (filesz) {
        u32 n = filesz > 64 ? 64 : filesz;

        if (ux_read_n(fd, (u8 *)(unsigned long)vaddr, n) < 0)
            return -1;
        vaddr += n;
        filesz -= n;
    }
    return 0;                            /* 段尾 memsz-filesz=mmap 零填 */
}

static void ux_exec(const char *path)
{
    struct ux_ehdr eh;
    u64 stk, top, sp, randp, argv0p;
    u64 *blk;
    u32 i, n;
    long fd;

    fd = sys1(ES_NR_fdopen, (long)path);
    if (fd < 0) {
        sys_puts("run: open failed\n");
        goto fail;
    }
    if (ux_read_n(fd, (u8 *)&eh, sizeof(eh)) < 0 ||
        eh.e_ident[0] != 0x7f || eh.e_ident[1] != 'E' ||
        eh.e_ident[2] != 'L' || eh.e_ident[3] != 'F' ||
        eh.e_ident[4] != 2 || eh.machine != 0xb7 || eh.type != 2) {
        sys_puts("run: bad elf\n");
        goto fail;
    }
    if (eh.phnum < 1 || eh.phnum > 8 || eh.phentsize != 56) {
        sys_puts("run: bad phdrs\n");
        goto fail;
    }
    if (sys3(ES_NR_lseek, fd, (long)(u32)eh.phoff, 0) != 0) {
        sys_puts("run: seek phoff failed\n");
        goto fail;
    }
    for (i = 0; i < eh.phnum; i++) {
        if (ux_read_n(fd, (u8 *)&ux_phs[i], sizeof(ux_phs[0])) < 0) {
            sys_puts("run: phdr read failed\n");
            goto fail;
        }
    }
    for (i = 0; i < eh.phnum; i++) {
        struct ux_phdr *ph = &ux_phs[i];
        u64 pglen;

        if (ph->type != 1)              /* PT_LOAD */
            continue;
        pglen = (ph->memsz + 4095) & ~4095ULL;
        if (!pglen)
            continue;
        if ((u64)sys2(ES_NR_mmap, (long)ph->vaddr, (long)pglen) !=
            ph->vaddr) {
            sys_puts("run: mmap miss\n");   /* hint 落位=loader 契约 */
            goto fail;
        }
        {   /* 段页先用户态预触:逐页写零在 EL0 触缺页(站 29 机器
             * 只挂 EL0 面);否则内核 uaccess(拷贝落段)先于映射,
             * EL1 打穿=panic(esr=0x96 FSC=0x06 far=段基,冒烟定谳) */
            u8 *pg = (u8 *)(unsigned long)ph->vaddr;
            u64 k;

            for (k = 0; k < pglen; k += 4096)
                pg[k] = 0;
        }
        if (ph->filesz) {
            if (sys3(ES_NR_lseek, fd, (long)(u32)ph->offset, 0) != 0 ||
                ux_copy_seg(fd, ph->vaddr, (u32)ph->filesz) < 0) {
                sys_puts("run: seg copy failed\n");
                goto fail;
            }
        }
    }
    sys1(ES_NR_fdclose, fd);
    /* 栈+启动块(自顶向下:串/AT_RANDOM/auxv/argv/envp/argc) */
    stk = (u64)sys2(ES_NR_mmap, (long)UX_STACK_HINT, (long)UX_STACK_LEN);
    if (stk != UX_STACK_HINT) {
        sys_puts("run: stack mmap miss\n");
        goto fail;
    }
    top = stk + UX_STACK_LEN;
    {   /* argv[0] 串 */
        const char *s = path;

        for (n = 0; s[n]; n++)
            ;
        n++;
        top -= n;
        argv0p = top;
        for (i = 0; i < n; i++)
            ((u8 *)(unsigned long)top)[i] = s[i];
        top &= ~15UL;
    }
    top -= 16;                           /* AT_RANDOM 数据 */
    randp = top;
    for (i = 0; i < 16; i++)
        ((u8 *)(unsigned long)randp)[i] = ux_rand[i];
    top &= ~15UL;
    /* 固定区:argc(8)+argv[0]+NULL(16)+envp NULL(8)+auxv(128) */
    sp = top - 160;
    blk = (u64 *)(unsigned long)sp;
    blk[0] = 1;                          /* argc */
    blk[1] = argv0p;
    blk[2] = 0;                         /* argv NULL */
    blk[3] = 0;                         /* envp: NULL */
    blk[4] = 3; blk[5] = (u64)(unsigned long)ux_phs;   /* AT_PHDR */
    blk[6] = 4; blk[7] = 56;                            /* AT_PHENT */
    blk[8] = 5; blk[9] = eh.phnum;                       /* AT_PHNUM */
    blk[10] = 6; blk[11] = 4096;                        /* AT_PAGESZ */
    blk[12] = 9; blk[13] = eh.entry;                     /* AT_ENTRY */
    blk[14] = 25; blk[15] = randp;                       /* AT_RANDOM */
    blk[16] = 23; blk[17] = 0;                          /* AT_SECURE */
    blk[18] = 0; blk[19] = 0;                           /* AT_NULL */
    sys3(ES_NR_exec_setup, (long)eh.entry, (long)sp, 0);
    sys_puts("run: exec returned?!\n");   /* 不达 */
fail:
    sys1(ES_NR_exit, 1);
}

static void cmd_run(const char *path)
{
    long st = 0, pid;

    pid = sys0(ES_NR_fork);
    if (pid == (long)RT_FORK_MAGIC) {   /* COPY_UCONTEXT 子支 */
        ux_exec(path);                  /* 不归(或失败退场) */
    }
    if (pid < 0) {
        sys_puts("run: fork failed\n");
        return;
    }
    pid = sys3(ES_NR_wait4, pid, (long)&st, 0);
    sys_puts("run: exited code=");
    sys_put_dec((u64)st);
    sys_puts("\n");
}

/* 站31:fd=通道三元组走查——open/read/close 全走 resmgr 服务器
 * (fd 层->三段消息->ramfs);EOF(status=0)收口,字节数自证。 */
static void fd_dump(const char *path, const char *tag)
{
    long fd, n, total = 0;
    char buf[64];

    fd = sys1(ES_NR_fdopen, (long)path);
    if (fd < 0) {
        sys_puts("fddemo: open ");
        sys_puts(path);
        sys_puts(" failed\n");
        return;
    }
    for (;;) {
        n = sys3(ES_NR_fdread, fd, (long)buf, sizeof(buf));
        if (n <= 0)
            break;                    /* 0=EOF;负=趟输错 */
        sys3(ES_NR_write, 1, (long)buf, (long)n);
        total += n;
    }
    sys1(ES_NR_fdclose, fd);
    sys_puts(tag);
    sys_put_dec((u64)total);
    sys_puts(" bytes\n");
}

static void cmd_fddemo(void)
{
    long n;

    /* fd 1(=stdio,PM 开机绑的 /dev/tty 通道)写字面——全链
     * fd->连接->服务器通道->resmgr WRITE->串口,此行即活证。 */
    n = sys3(ES_NR_fdwrite, 1, (long)"fddemo: via fd tty\n", 19);
    sys_puts("fddemo: tty wrote ");
    sys_put_dec((u64)n);
    sys_puts("\n");
    fd_dump("/etc/motd", "fddemo: motd ");
    fd_dump("/etc/version", "fddemo: version ");
}

static void shell_line(char *line)
{
    if (streq(line, "exit")) {
        sys1(ES_NR_hvc, 3);          /* noreturn:内核收尸 */
    } else if (streq(line, "yield")) {
        sys_puts("yielding...\n");
        sys1(ES_NR_hvc, 1);
        sys_puts("resumed\n");
    } else if (streq(line, "selftest")) {
        sys2(ES_NR_demo, RT_U_SELFTEST, 0);
    } else if (streq(line, "ps")) {
        sys2(ES_NR_demo, RT_U_PS, 0);
    } else if (streq(line, "fp")) {
        sys2(ES_NR_demo, RT_U_FP, 0);
    } else if (streq(line, "semdemo")) {
        sys2(ES_NR_demo, RT_U_SEMDEMO, 0);
    } else if (streq(line, "mutexdemo")) {
        sys2(ES_NR_demo, RT_U_MUTEXDEMO, 0);
    } else if (streq(line, "pidemo")) {
        sys2(ES_NR_demo, RT_U_PIDEMO, 0);
    } else if (streq(line, "fpdual")) {
        sys2(ES_NR_demo, RT_U_FPDUAL, 0);
    } else if (streq(line, "eventdemo")) {
        sys2(ES_NR_demo, RT_U_EVENTDEMO, 0);
    } else if (streq(line, "queuedemo")) {
        sys2(ES_NR_demo, RT_U_QUEUEDEMO, 0);
    } else if (streq(line, "smpdemo")) {
        sys2(ES_NR_demo, RT_U_SMPDEMO, 0);
    } else if (streq(line, "ipidemo")) {
        sys2(ES_NR_demo, RT_U_IPIDEMO, 0);
    } else if (streq(line, "fpsmp")) {
        sys2(ES_NR_demo, RT_U_FPSMP, 0);
    } else if (streq(line, "tmodemo")) {
        sys2(ES_NR_demo, RT_U_TMODEMO, 0);
    } else if (streq(line, "swtdemo")) {
        sys2(ES_NR_demo, RT_U_SWTDEMO, 0);
    } else if (streq(line, "irqdemo")) {
        sys2(ES_NR_demo, RT_U_IRQDEMO, 0);
    } else if (streq(line, "lowdemo")) {
        sys2(ES_NR_demo, RT_U_LOWDEMO, 0);
    } else if (streq(line, "stkdemo")) {
        sys2(ES_NR_demo, RT_U_STKDEMO, 0);
    } else if (streq(line, "stackcheck")) {
        sys2(ES_NR_demo, RT_U_STACKCHECK, 0);
    } else if (streq(line, "overflow")) {
        sys2(ES_NR_demo, RT_U_OVERFLOW, 0);
    } else if (streq(line, "fpstat")) {
        sys2(ES_NR_demo, RT_U_FPSTAT, 0);
    } else if (streq(line, "hb")) {
        sys2(ES_NR_demo, RT_U_HB, 0);
    } else if (streq(line, "utest")) {
        cmd_utest();
    } else if (streq(line, "forkdemo")) {
        cmd_forkdemo();
    } else if (streq(line, "mmapdemo")) {
        cmd_mmapdemo();
    } else if (streq(line, "exedemo")) {
        cmd_exedemo();
    } else if (streq(line, "ipcdemo")) {
        cmd_ipcdemo();
    } else if (streq(line, "pulsedemo")) {
        cmd_pulsedemo();
    } else if (streq(line, "abortdemo")) {
        cmd_abortdemo();
    } else if (streq(line, "fddemo")) {
        cmd_fddemo();
    } else if (starts(line, "run ")) {
        cmd_run(line + 4);
    } else if (streq(line, "futexdemo")) {
        cmd_futexdemo();
    } else if (streq(line, "sigdemo")) {
        cmd_sigdemo();
    } else if (streq(line, "shmdemo")) {
        cmd_shmdemo();
    } else if (streq(line, "msgseg")) {
        cmd_msgsegdemo();
    } else if (streq(line, "mqdemo")) {
        cmd_mqdemo();
    } else if (streq(line, "pipedemo")) {
        cmd_pipedemo();
    } else if (streq(line, "longdemo")) {
        cmd_longdemo();
    } else if (starts(line, "spawn ")) {
        sys3(ES_NR_demo, RT_U_SPAWN, (long)(line + 6), 0);
    } else if (starts(line, "sleep")) {
        unsigned int secs = 2;

        if (line[5] == ' ' && line[6] >= '1' && line[6] <= '9'
            && line[7] == '\0')
            secs = (unsigned int)(line[6] - '0');
        else if (line[5] != '\0')
            secs = 0;
        if (secs == 0) {
            sys_puts("usage: sleep [1-9]\n");
        } else {
            sys_puts("sleeping...\n");
            sys3(ES_NR_demo, RT_U_SLEEP, (long)secs, 0);
            sys_puts("slept (rtos sleep)\n");
        }
    } else if (streq(line, "host")) {
        sys_puts("releasing console...\n");
        sys1(ES_NR_hvc, 2);
    } else if (starts(line, "echo ")) {
        sys_puts(line + 5);
        sys_puts("\n");
    } else if (line[0]) {
        sys_puts("rtos: try echo <t> | selftest | utest | ps | spawn <n> | sleep [1-9]"
                 " | forkdemo | mmapdemo | exedemo | ipcdemo | pulsedemo | abortdemo | fddemo"
                 " | futexdemo | sigdemo | shmdemo | msgseg | mqdemo | pipedemo | longdemo | run <p>"
                 " | fp | fpdual | fpstat | hb | yield | semdemo | mutexdemo"
                 " | pidemo | eventdemo | queuedemo | smpdemo | ipidemo | fpsmp | tmodemo | swtdemo | irqdemo | lowdemo"
                 " | stkdemo | stackcheck | overflow | host | exit\n");
    }
}

/* 用户栈(boot 锻帧用;落 .user 段=用户窗)。 */
char rt_shell_ustack[16384] __attribute__((aligned(16)));

void rt_shell_task(void *arg)
{
    unsigned int idx = 0;

    (void)arg;
    sys_puts("rtos> ");
    for (;;) {
        char c;

        if (sys3(ES_NR_read, 0, (long)&c, 1) != 1)
            continue;                /* 不可达(read 恒 1 或负 errno) */
        if (c == '\r' || c == '\n') {
            sys3(ES_NR_write, 1, (long)"\n", 1);
            linebuf[idx] = '\0';
            shell_line(linebuf);
            idx = 0;
            sys_puts("rtos> ");
        } else if ((c == 0x7f || c == '\b') && idx > 0) {
            idx--;
            sys3(ES_NR_write, 1, (long)"\b \b", 3);
        } else if (idx < RT_BUF_MAX - 1 && c >= 0x20 && c < 0x7f) {
            linebuf[idx++] = c;
            sys3(ES_NR_write, 1, (long)&c, 1);
        }
    }
}
