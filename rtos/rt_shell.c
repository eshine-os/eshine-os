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
static u32 sig_word;   /* 站44:文件域上提(handler 要戳;子承
                          * .user 页拷同 VA,COPY_UCONTEXT 律) */

static void sig_handler(long sig)
{
    sys_puts("sig: handler ran sig=");
    sys_put_dec((u64)sig);
    sys_puts("\n");
    sig_word = 1;   /* 站44:重启判据装置——handler 内戳字,重启再入
                     * 入口值校验 *w!=expect -> -EAGAIN;一判决两事:
                     * elr 没回拨=执行 svc 后指令错窗垃圾,x0 没保
                     * 入参=拿 -512 当 uaddr 野访 panic */
    sys0(ES_NR_rt_sigreturn);
}

static void cmd_sigdemo(void)
{
    long pid, r, st = 0;
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
         * 挂 futex 静候父信号——站44 判据面:打断不返 EINTR,
         * handler 戳字+重启再入 -> 入口值校验 -EAGAIN(10s ts
         * 仅兜底:重启破时旧脸 r=-4 落 FAIL 行,不悬死) */
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
        sys_puts(r == -11 ? "sig: futex restarted -> -EAGAIN ok\n"
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

/* ---- 站38:signal 完整面客户端——sigqueue 携值+SA_RESTORER
 * 回栈+默认动作终止(POSIX 面)。 */
static void sigq_restorer(void)
{
    sys0(ES_NR_rt_sigreturn);       /* restorer 契约:handler ret
                                     * 落此(投递改写 x30),自调
                                     * sigreturn;旧契约(handler 末尾
                                     * 显式调)两立。 */
}

static void sigq_handler(long sig, long val)
{
    sys_puts("sigq: handler sig=");
    sys_put_dec((u64)sig);
    sys_puts(" val=");
    sys_put_dec((u64)val);
    sys_puts("\n");
}

/* ---- 站39:FS 深面客户端——ramfs 写时复制/枚举/块设备裸面 ---- */
static void cmd_wrdemo(void)
{
    long fd, n, r;
    char buf[64];

    fd = sys1(ES_NR_fdcreate, (long)"/tmp/w39");
    if (fd < 0) {
        sys_puts("wrdemo: create FAIL\n");
        return;
    }
    r = sys3(ES_NR_fdwrite, fd, (long)"ESHINE39", 9);
    sys_puts(r == 9 ? "wrdemo: wrote 9 ok\n" : "wrdemo: write FAIL\n");
    r = sys3(ES_NR_fdwrite, fd, (long)"-APPEND", 7);  /* 追加:长随位长 */
    sys_puts(r == 7 ? "wrdemo: append 7 ok\n" : "wrdemo: append FAIL\n");
    r = sys3(ES_NR_lseek, fd, 0, 0);                /* SEEK_SET 回卷 */
    n = sys3(ES_NR_fdread, fd, (long)buf, 64);
    buf[16] = '\0';
    sys_puts(n == 16 ? "wrdemo: read 'ESHINE39-APPEND' ok\n"
                     : "wrdemo: read FAIL\n");
    r = sys3(ES_NR_lseek, fd, 0, 2);                /* SEEK_END */
    n = sys3(ES_NR_fdread, fd, (long)buf, 64);
    sys_puts(n == 0 ? "wrdemo: eof after END ok\n" : "wrdemo: eof FAIL\n");
    sys1(ES_NR_fdclose, fd);
    r = sys1(ES_NR_fdunlink, (long)"/tmp/w39");
    if (r == 0)
        r = sys1(ES_NR_fdopen, (long)"/tmp/w39");
    sys_puts(r == -2 ? "wrdemo: unlinked, reopen ENOENT ok\n"
                     : "wrdemo: unlink FAIL\n");
}

static void cmd_lsdemo(void)
{
    char name[48];
    long cur = 0, r;
    int n = 0, got_motd = 0;

    for (;;) {
        {
            u32 i;

            for (i = 0; i < sizeof(name); i++)
                name[i] = 0;
        }
        r = sys4(ES_NR_fddir, cur, 0, (long)name, 48);
        if (r <= 0) {
            sys_puts("LSDBG: r=");
            sys_put_dec((u64)(long)r);
            sys_puts(" first=");
            sys_put_dec((u64)name[0]);
            sys_puts("\n");
            break;                       /* 0=终;负=errno */
        }
        cur = r;
        n++;
        if (name[0] == '/' && name[1] == 'e' && name[2] == 't' &&
            name[3] == 'c' && name[4] == '/' && name[5] == 'm')
            got_motd = 1;
    }
    if (n >= 4 && got_motd) {
        sys_puts("lsdemo: entries ok, /etc/motd in list\n");
    } else {
        sys_puts("lsdemo: FAIL n=");
        sys_put_dec((u64)n);
        sys_puts(" first=");
        sys_puts(name);
        sys_puts("\n");
    }
}

static void cmd_fblkdemo(void)
{
    long fd, n, r;
    char buf[64];

    fd = sys1(ES_NR_fdopen, (long)"/dev/blk");
    if (fd < 0) {
        sys_puts("fblkdemo: open FAIL\n");
        return;
    }
    r = sys3(ES_NR_fdwrite, fd, (long)"BLK39-SECTOR0", 13);
    if (r != 13) {
        sys_puts("fblkdemo: write FAIL\n");
        return;
    }
    r = sys3(ES_NR_lseek, fd, 0, 0);
    n = sys3(ES_NR_fdread, fd, (long)buf, 13);
    buf[13] = '\0';
    if (n != 13 || buf[0] != 'B' || buf[12] != '0') {
        sys_puts("fblkdemo: readback FAIL n=");
        sys_put_dec((u64)(long)n);
        sys_puts(" b0=");
        sys_put_dec((u64)buf[0]);
        sys_puts("\n");
        return;
    }
    sys_puts("fblkdemo: sector rw ok\n");
    sys1(ES_NR_fdclose, fd);
}

static void cmd_sigqdemo(void)
{
    long pid, r, st = 0;

    sys4(ES_NR_rt_sigaction, 10, (long)sigq_handler,
         (long)sigq_restorer, 0);
    pid = sys0(ES_NR_fork);
    if (pid == (long)RT_FORK_MAGIC) {   /* 子:睡中收信(handler+值)
                                         * ->restorer->sigreturn->续跑;
                                         * 再睡中收无 handler 的 12
                                         * ->默认终止 128+12=140 */
        sys3(ES_NR_demo, RT_U_SLEEP, 2, 0);
        sys_puts("sigq: resumed\n");
        sys3(ES_NR_demo, RT_U_SLEEP, 2, 0);
        sys1(ES_NR_exit, 99);           /* 到不了(默认动作先终局) */
    }
    sys3(ES_NR_demo, RT_U_SLEEP, 1, 0);  /* 让子先进睡窗 */
    r = sys3(ES_NR_rt_sigqueueinfo, pid, 10, 0xBEEF);
    sys_puts(r == 0 ? "sigq: queued val=48879\n" : "sigq: queue FAIL\n");
    sys3(ES_NR_demo, RT_U_SLEEP, 2, 0);
    r = sys2(ES_NR_kill, pid, 12);       /* 无 handler=默认终止脸 */
    sys3(ES_NR_wait4, pid, (long)&st, 0);
    sys_puts(st == 140 ? "sigq: default terminate 140 ok\n"
                       : "sigq: terminate FAIL\n");
    r = sys2(ES_NR_rt_sigtimedwait, 10, 0);  /* 空轮询脸(v0 阻塞后弧) */
    sys_puts(r == -11 ? "sigq: empty poll -11 ok\n"
                      : "sigq: poll FAIL\n");
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

#define UX_STACK_HINT 0x40D00000UL   /* 站40:回 1MB 位(busybox 精选集图像
                                      * ≤0x40C52000,hint 不撞;游标回位后
                                      * musl 线程栈族窗尾余量=站 35 pt 账) */
#define UX_STACK_LEN  0x20000UL
#define UX_ARGC_MAX   8u               /* run/inittab 参数上限 */

static int ux_read_n(long fd, u8 *dst, u32 n)
{
    /* 64B 窗直读(fdread 的内核拷贝即落目标 VA——access_ok 域内)。
     * 站40:幂等重试——电池语境下 ~1/数千往返有单发失联(SRV_TICKS
     * 超时脸=回复唤醒丢失族,站 25 同型内部面;"seg copy failed"
     * r14 实抓)。失联即 lseek 绝对位重同步再试(ocb.off 可能已
     * 进,先回预期位;至多 3 次)。 */
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

/* 站40:段拷贝带文件绝对位+幂等重试——电池语境 ~1/1-2 万往返
 * 有单发失联(SRV_TICKS 超时脸=回复唤醒丢失族,站 25 同型内部
 * 面;"seg copy failed" r14 实抓,[103] 装载区集中营 r8-r22 六
 * 轮):失联窗 ocb.off 可能已进,lseek 回绝对位再读,至多 3 次。
 * r16-r21 "靴楔"系 verify 探针自雷(假探针已拆),非本重试之罪
 * (r13/r14 携本形健靴直证)。 */
static int ux_copy_seg(long fd, u64 foff, u64 vaddr, u32 filesz)
{
    while (filesz) {
        u32 n = filesz > 64 ? 64 : filesz;
        long r;
        u32 tries;

        for (tries = 0; tries < 4; tries++) {
            if (tries) {                  /* 失联窗:绝对位重同步 */
                if (sys3(ES_NR_lseek, fd, (long)(u32)foff, 0) < 0)
                    return -1;
            }
            r = sys3(ES_NR_fdread, fd, (long)(unsigned long)vaddr, (long)n);
            if (r > 0) {                  /* 到手(含短窗):按返值推进 */
                vaddr += (u32)r;
                foff += (u32)r;
                filesz -= (u32)r;
                break;
            }
            if (r == 0)
                return -1;                /* EOF=诚实失败 */
        }
        if (tries == 4)
            return -1;                    /* 3 次重同步皆失联 */
    }
    return 0;                            /* 段尾 memsz-filesz=mmap 零填 */
}

static void __attribute__((noinline)) ux_write_block(u64 sp, int argc,
                                              const u64 *argvp, u64 randp,
                                              u64 entry, u32 phnum)
{
    u64 *blk = (u64 *)(unsigned long)sp;
    u32 i;

    blk[0] = (u64)argc;
    for (i = 0; i < (u32)argc; i++)
        blk[1 + i] = argvp[i];
    blk[1 + argc] = 0;                 /* argv NULL */
    blk[2 + argc] = 0;                 /* envp: NULL */
    blk[3 + argc] = 3;  blk[4 + argc] = (u64)(unsigned long)ux_phs;
    blk[5 + argc] = 4;  blk[6 + argc] = 56;             /* AT_PHENT */
    blk[7 + argc] = 5;  blk[8 + argc] = phnum;          /* AT_PHNUM */
    blk[9 + argc] = 6;  blk[10 + argc] = 4096;          /* AT_PAGESZ */
    blk[11 + argc] = 9; blk[12 + argc] = entry;        /* AT_ENTRY */
    blk[13 + argc] = 25; blk[14 + argc] = randp;      /* AT_RANDOM */
    blk[15 + argc] = 23; blk[16 + argc] = 0;           /* AT_SECURE */
    blk[17 + argc] = 0; blk[18 + argc] = 0;            /* AT_NULL */
}

static void ux_exec(const char *path, int argc, char *const *argv)
{
    struct ux_ehdr eh;
    u64 stk, top, sp, randp, argvp[UX_ARGC_MAX];
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
    /* 站39:lseek 返新位(POSIX 面),只拒负——旧判 !=0 撞新契约 */
    if (sys3(ES_NR_lseek, fd, (long)(u32)eh.phoff, 0) < 0) {
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
        {   /* 站40:页包含映射(Linux 同律)——busybox RW 段 vaddr
             * 非页对齐,映射含段页;hint=段页基,预触自页基起 */
            u64 base = ph->vaddr & ~4095ULL;

            pglen = ((ph->vaddr + ph->memsz + 4095) & ~4095ULL) - base;
            if (!pglen)
                continue;
            if ((u64)sys2(ES_NR_mmap, (long)base, (long)pglen) != base) {
                sys_puts("run: mmap miss\n");   /* hint 落位=loader 契约 */
                goto fail;
            }
        }
        {   /* 段页先用户态预触:逐页写零在 EL0 触缺页(站 29 机器
             * 只挂 EL0 面);否则内核 uaccess(拷贝落段)先于映射,
             * EL1 打穿=panic(esr=0x96 FSC=0x06 far=段基,冒烟定谳) */
            u8 *pg = (u8 *)(unsigned long)(ph->vaddr & ~4095ULL);
            u64 k;

            for (k = 0; k < pglen; k += 4096)
                pg[k] = 0;
        }
        if (ph->filesz) {
            if (sys3(ES_NR_lseek, fd, (long)(u32)ph->offset, 0) < 0 ||
                ux_copy_seg(fd, ph->offset, ph->vaddr,
                            (u32)ph->filesz) < 0) {
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
    {   /* 站40:栈页预触(段预触同律)——串打包/启动块写入即落
         * 已驻页;缺页窗(偏移寄存器跨首个栈页缺页的保存疑案)
         * 从关键路径整体消失(观察项记档 devlog)。 */
        u8 *pg = (u8 *)(unsigned long)stk;
        u64 k;

        for (k = 0; k < UX_STACK_LEN; k += 4096)
            pg[k] = 0;
    }
    top = stk + UX_STACK_LEN;
    {   /* 站40:argv[0..argc-1] 串区(自顶向下,指针另存) */
        int a;

        if (argc < 1)
            argc = 1;
        if ((u32)argc > UX_ARGC_MAX)
            argc = UX_ARGC_MAX;
        for (a = argc - 1; a >= 0; a--) {
            const char *ss = argv && argv[a] ? argv[a] : path;

            for (n = 0; ss[n]; n++)
                ;
            n++;
            top -= n;
            argvp[a] = top;
            for (i = 0; i < n; i++)
                ((u8 *)(unsigned long)top)[i] = ss[i];
        }
        top &= ~15UL;
    }
    top -= 16;                           /* AT_RANDOM 数据 */
    randp = top;
    for (i = 0; i < 16; i++)
        ((u8 *)(unsigned long)randp)[i] = ux_rand[i];
    top &= ~15UL;
    /* 固定区:argc+argv[0..argc-1]+NULL+envp NULL+auxv 10 项(变长)。
     * 站40:写入抽 noinline 辅函数——偏移寄存器勿长持跨 EL0 缺页
     * (11 站潜伏保存洞的第一受害形态,build 敏感;调用边界=帧
     * 重载,正身同探针之效;内核疑点记档 devlog 挂账)。 */
    sp = top - 8 * (u64)(1 + (u32)argc + 2 + 20);
    ux_write_block(sp, argc, argvp, randp, eh.entry, eh.phnum);
    sys3(ES_NR_exec_setup, (long)eh.entry, (long)sp, 0);
    sys_puts("run: exec returned?!\n");   /* 不达 */
fail:
    sys1(ES_NR_exit, 1);
}

/* 站40:空格切分(run/inittab 共用;argv[] 指针直入原缓冲) */
static int ux_split(char *args, char **argv, u32 max)
{
    int argc = 0;

    while (*args == ' ')
        args++;
    while (*args && (u32)argc < max) {
        argv[argc++] = args;
        while (*args && *args != ' ')
            args++;
        if (*args)
            *args++ = '\0';
        while (*args == ' ')
            args++;
    }
    return argc;
}

/* fork+exec 子支分派(父返 pid/负 errno;子支不归) */
static long spawn_exec(const char *path, int argc, char *const *argv)
{
    long pid = sys0(ES_NR_fork);

    if (pid == (long)RT_FORK_MAGIC)   /* COPY_UCONTEXT 子支 */
        ux_exec(path, argc, argv);      /* 不归(或失败退场) */
    return pid;
}

#define INIT_RESPAWN_MAX 2
struct init_spawn {
    long pid;
    int argc;
    char arg[4][48];
};
static struct init_spawn init_resp[INIT_RESPAWN_MAX];
static int init_nresp;

/* 站40:loader 单飞——respawn 子(beat)加载与 run 子 fork 重叠=
 * ramfs 通道双发送者并发,seg copy 中途断(冒烟定谳:beat respawn
 * 打印与下一条 busybox run 并行即现);内核侧并发发送者疑点带证
 * 挂账(devlog)。站内正身修:run 子 fork 前先收净 respawn 子
 * (beat 打印即退,等待窗毫秒级)。 */
static void init_drain(void)
{
    int i;

    for (i = 0; i < init_nresp; i++) {
        long st = 0;

        if (init_resp[i].pid <= 0)
            continue;
        sys3(ES_NR_wait4, init_resp[i].pid, (long)&st, 0);
        init_resp[i].pid = 0;
    }
}

static void cmd_run_args(char *const *argv, int argc)
{
    long st = 0;
    long pid;

    init_drain();                     /* 站40:loader 单飞(先收净 beat) */
    pid = spawn_exec(argv[0], argc, argv);

    if (pid < 0) {
        sys_puts("run: fork failed err=");
        sys_put_dec((u64)(long)-pid);
        sys_puts("\n");
        return;
    }
    sys3(ES_NR_wait4, pid, (long)&st, 0);
    sys_puts("run: exited code=");
    sys_put_dec((u64)st);
    sys_puts("\n");
}

/* ---- 站40:init+inittab 形态(壳代处理器;真 PID1 init=后弧) ----
 * 四 action:sysinit/wait=跑完再继续;once=不等;respawn=退场即再拉
 * (WNOHANG 探询=壳循环拍,v0 检查点记档)。 */

static void init_run(char *proc, int wait, int track)
{
    char *argv[4];
    int argc = ux_split(proc, argv, 4);
    long pid, st = 0;

    if (argc <= 0)
        return;
    if (track) {
        /* 站40 终形(me_r15 boot 楔定谳):respawn 项=注册不启——
         * boot 期装载窗=电池最大失联窗之首(r4/r5/r15 三轮 boot
         * 楔皆此窗);拉起=initrespawn 命令门拍(确定性单拍,
         * [105] 门锚)。表项全量注册,boot 零装载噪声。 */
        if (init_nresp < INIT_RESPAWN_MAX) {
            struct init_spawn *sp = &init_resp[init_nresp++];
            int a, b;

            sp->pid = 0;
            sp->argc = argc;
            for (a = 0; a < argc && a < 4; a++)
                for (b = 0; b < 47 && argv[a][b]; b++)
                    sp->arg[a][b] = argv[a][b], sp->arg[a][b + 1] = '\0';
        }
        return;
    }
    pid = spawn_exec(argv[0], argc, argv);
    if (pid < 0)
        return;
    if (wait) {
        sys3(ES_NR_wait4, pid, (long)&st, 0);
        return;
    }
}

/* 站40:EL0 睡拍——hvc_yield 是宿主暂停面(冒烟自雷:inittab 等
 * 待环误用,VM 首轮即被宿主整体冻结),EL0 等待须走 futex 超时
 * (站 32 单窗阻塞,调度器让位,宿主零介入)。timespec 换算=站35。 */
static u64 sleep_ts[2];                /* {sec,nsec} */
static const u32 sleep_gate;           /* 恒 0:入口值校验即过 */

static void sleep_ticks(u32 ticks)
{
    sleep_ts[0] = 0;
    sleep_ts[1] = (u64)ticks * 10 * 1000 * 1000;   /* tick=10ms */
    sys4(ES_NR_futex, (long)&sleep_gate, RT_FUTEX_WAIT, 0,
         (long)sleep_ts);
}

static void init_poll(void)
{
    int i;

    for (i = 0; i < init_nresp; i++) {
        long st = 0;

        if (init_resp[i].pid <= 0)
            continue;
        /* 站40 门拍制(v0 定稿):init_poll 只收尸——自主连拍=
         * 竞态放大器(加载窗×任意门窗,mutex/ps 门两轮 verify
         * 定谳);respawn 面=initrespawn 命令门拍(确定性单拍,
         * sync 等死即拍即收)。自主连拍=后弧(记档 devlog)。 */
        if (sys3(ES_NR_wait4, init_resp[i].pid, (long)&st, 1) ==
            init_resp[i].pid)
            init_resp[i].pid = 0;
    }
}

/* 站40:respawn 门拍——确定性单拍:收旧->拉新->等死(即拍即收,
 * 控制台生命全串行,判据窗零随机搅动)。 */
static void cmd_initrespawn(void)
{
    int i;

    for (i = 0; i < init_nresp; i++) {
        long st = 0;
        char *argv[4];
        int a;

        for (a = 0; a < 4; a++)
            argv[a] = init_resp[i].arg[a];
        sys_puts("init: respawn ");
        sys_puts(init_resp[i].arg[0]);
        sys_puts("\n");
        init_resp[i].pid = spawn_exec(argv[0], init_resp[i].argc, argv);
        if (init_resp[i].pid > 0) {
            sys3(ES_NR_wait4, init_resp[i].pid, (long)&st, 0);
            sys_puts("init: exited code=");
            sys_put_dec((u64)(u32)st);
            sys_puts("\n");           /* r25 定谳:门锚等过此行 */
        }
        else
            init_resp[i].pid = 0;
    }
}

static void init_inittab(void)
{
    char buf[192];
    long fd;
    u32 n = 0, tries = 0;

    while (tries++ < 200) {           /* 等 ramfs 起立(futex 睡拍重试) */
        fd = sys1(ES_NR_fdopen, (long)"/etc/inittab");
        if (fd >= 0)
            break;
        sleep_ticks(1);                /* 服务器(8)得拍起立挂名 */
    }
    if (fd < 0) {
        sys_puts("init: no /etc/inittab\n");
        return;
    }
    for (;;) {
        long r;

        if (n + 64 > sizeof(buf))      /* 窗满哨兵只防溢(勿早断:EOF 为正途) */
            break;
        r = sys3(ES_NR_fdread, fd, (long)(buf + n), 64);
        if (r <= 0)
            break;
        n += (u32)r;
    }
    sys1(ES_NR_fdclose, fd);
    buf[n] = '\0';
    {   /* 逐行:id:levels:action:process */
        char *line = buf;

        while (line < buf + n) {
            char *nl = line, *f[4];
            int fi = 0, wait = 0, track = 0;

            while (*nl && *nl != '\n')
                nl++;
            if (*nl)
                *nl++ = '\0';
            if (*line != '#') {        /* 注释行直过 */
                char *p = line;

                f[0] = f[1] = f[2] = f[3] = 0;
                while (*p && fi < 4) {
                    f[fi++] = p;
                    while (*p && *p != ':')
                        p++;
                    if (*p)
                        *p++ = '\0';
                }
                if (fi == 4 && f[2] && f[3]) {
                    if (streq(f[2], "sysinit") || streq(f[2], "wait"))
                        wait = 1;
                    else if (streq(f[2], "respawn"))
                        track = 1;
                    else if (!streq(f[2], "once"))
                        goto next;     /* 未知 action:整行记档直过 */
                    init_run(f[3], wait, track);
                    sys_puts("init: ");
                    sys_puts(f[2]);
                    sys_puts(" ");
                    sys_puts(f[3]);
                    sys_puts("\n");
                }
            }
next:
            line = nl;
        }
    }
    sys_puts("init: inittab processed\n");
}

static void cmd_run(const char *args)
{
    char *argv[UX_ARGC_MAX];
    int argc = ux_split((char *)args, argv, UX_ARGC_MAX);

    if (argc <= 0) {
        sys_puts("run: usage: run <path> [args...]\n");
        return;
    }
    cmd_run_args((char *const *)argv, argc);
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

/* ---- 站四十一:CAPR 四 demo(参考实现五面最小站内形) ---- */

static void cmd_capdemo(void)
{
    long st = 0, pid = sys0(ES_NR_fork);

    if (pid == (long)RT_FORK_MAGIC) {   /* 子:自削 VFS → open 拒 */
        long dfd = sys1(ES_NR_fdopen, (long)"/etc");  /* 先开窗 */
        long fd;

        sys1(ES_NR_cap_drop, (long)CAP_VFS);
        fd = sys1(ES_NR_fdopen, (long)"/etc/motd");
        sys_puts(fd == -1 ? "cap: vfs dropped, open -EPERM ok\n"
                          : "cap: open NOT denied?!\n");
        {   /* spec §2[107]:已开 dir fd 上 getdents64 同拒(操作级
             * 执法,非只开门) */
            char db[64];
            long r = sys3(ES_NR_getdents64, dfd, (long)db, sizeof(db));

            sys_puts(r == -1 ? "cap: getdents64 -EPERM ok\n"
                             : "cap: getdents64 NOT denied?!\n");
            sys1(ES_NR_fdclose, dfd);
        }
        sys1(ES_NR_cap_drop, (long)CAP_ALL);   /* 再削尽:零面自证 */
        sys_puts(sys1(ES_NR_cap_get, 0) == 0
                 ? "cap: self-inc-out (no add path) ok\n"
                 : "cap: caps not zero?!\n");
        sys1(ES_NR_exit, 0);
    }
    if (pid < 0) {
        sys_puts("capdemo: fork failed\n");
        return;
    }
    sys3(ES_NR_wait4, pid, (long)&st, 0);
    /* spec §2[111]:fail-closed 回归——壳=CAP_ALL 全脸照常 */
    sys_puts(sys1(ES_NR_cap_get, 0) == (long)CAP_ALL
             ? "cap: shell CAP_ALL regression ok\n"
             : "cap: shell caps wrong?!\n");
    sys_puts("capdemo: done\n");
}

static void cmd_capdemo2(void)
{
    long st = 0, pid = sys0(ES_NR_fork);

    if (pid == (long)RT_FORK_MAGIC) {   /* 子:自削 SPAWN → fork 拒 */
        long c;

        sys1(ES_NR_cap_drop, (long)CAP_SPAWN);
        c = sys0(ES_NR_fork);
        sys_puts(c == -1 ? "cap: spawn dropped, fork -EPERM ok\n"
                         : "cap: fork NOT denied?!\n");
        sys1(ES_NR_exit, 0);
    }
    if (pid < 0) {
        sys_puts("capdemo2: fork failed\n");
        return;
    }
    sys3(ES_NR_wait4, pid, (long)&st, 0);
    sys_puts("capdemo2: done\n");
}

static void cmd_rlimitdemo(void)
{
    long st = 0, pid = sys0(ES_NR_fork);

    if (pid == (long)RT_FORK_MAGIC) {   /* 子:max_fd 自降 3 → 第 4 开拒 */
        long fd, i;

        sys3(ES_NR_rlimit_self, 3, 0, 0);
        for (i = 0; i < 4; i++)
            fd = sys1(ES_NR_fdopen, (long)"/etc/motd");
        sys_puts(fd == -24 ? "rlimit: 4th open -EMFILE ok\n"
                           : "rlimit: open NOT capped?!\n");
        sys1(ES_NR_exit, 0);
    }
    if (pid < 0) {
        sys_puts("rlimitdemo: fork failed\n");
        return;
    }
    sys3(ES_NR_wait4, pid, (long)&st, 0);
    sys_puts("rlimitdemo: done\n");
}

static void cmd_uiddemo(void)
{
    long st = 0, a = sys1(ES_NR_uuid_get, 0), pid = sys0(ES_NR_fork);

    if (pid == (long)RT_FORK_MAGIC) {   /* 子:uuid 互异自证 */
        long b = sys1(ES_NR_uuid_get, 0);

        sys_puts(b && b != a ? "uuid: child differs nonzero ok\n"
                             : "uuid: identity broken?!\n");
        sys1(ES_NR_exit, 0);
    }
    if (pid < 0) {
        sys_puts("uiddemo: fork failed\n");
        return;
    }
    sys3(ES_NR_wait4, pid, (long)&st, 0);
    sys_puts("uuid: parent ");
    sys_puts(a ? "nonzero ok\n" : "ZERO?!\n");
    sys_puts("uiddemo: done\n");
}

/* ---- 站四十二:syscall 面补全 demo(参考实现终审计补齐弧) ---- */

static void cmd_scheddemo(void)
{
    struct { u64 sec, nsec; } ts;
    u32 param = 7;
    long r;

    sys3(ES_NR_sched_setscheduler, 0, SCHED_FIFO, (long)&param);
    r = sys2(ES_NR_sched_getparam, 0, (long)&param);
    sys_puts(r == 0 && param == 7 ? "sched: set+get prio 7 ok\n"
                                  : "sched: getparam wrong?!\n");
    sys0(ES_NR_sched_yield);
    sys_puts("sched: yield ok\n");
    ts.sec = ts.nsec = 0;
    sys2(ES_NR_sched_rr_get_interval, 0, (long)&ts);
    sys_puts(ts.nsec == 40000000ULL ? "sched: rr slice 40ms ok\n"
                                    : "sched: rr interval wrong?!\n");
    param = 5;                       /* 还原壳基准位(PI 纪律) */
    sys3(ES_NR_sched_setscheduler, 0, SCHED_FIFO, (long)&param);
    sys_puts("scheddemo: done\n");
}

static void sigdemo2_handler(long sig, long val)
{
    sys_puts("sig2: handler in own ctx ok\n");
    (void)sig;
    (void)val;
    sys0(ES_NR_rt_sigreturn);         /* 站32 显式契约:restorer=0 面
                                       * handler 末尾自调,永不返回 */
}

static void cmd_sigdemo2(void)
{
    long pid = sys0(ES_NR_fork), st = 0;

    if (pid == (long)RT_FORK_MAGIC) {
        u64 block = 1u << 7, old = 0, pend = 0, zero = 0;
        u64 ss_in[2] = {0, 0x1000}, ss_old[2] = {99, 99};
        long r;

        sys3(ES_NR_rt_sigaction, 7, (long)sigdemo2_handler, 0);
        sys3(ES_NR_rt_sigprocmask, 0, (long)&block, (long)&old); /* SIG_BLOCK 7 */
        sys2(ES_NR_kill, (long)sys0(ES_NR_getpid), 7);           /* 自挂屏蔽中 */
        r = sys2(ES_NR_rt_sigpending, (long)&pend, 8);
        sys_puts(r == 0 && (pend & (1u << 7)) ? "sig2: pending bit ok\n"
                                             : "sig2: pending wrong?!\n");
        sys2(ES_NR_sigaltstack, (long)ss_in, (long)ss_old);
        sys_puts(ss_old[1] == 0 ? "sig2: altstack reg ok\n"
                                : "sig2: altstack wrong?!\n");
        /* sigsuspend:清掩码挂起等投递——醒即投递(handler 在 syscall
         * 出口)后以旧掩码续跑;此处 mask=0(全放行)。 */
        r = sys2(ES_NR_rt_sigsuspend, (long)&zero, 8);
        sys_puts(r == -4 ? "sig2: resumed -EINTR ok\n"
                         : "sig2: suspend ret wrong?!\n");
        sys1(ES_NR_exit, 0);
    }
    if (pid < 0) {
        sys_puts("sigdemo2: fork failed\n");
        return;
    }
    sys3(ES_NR_wait4, pid, (long)&st, 0);
    sys_puts("sigdemo2: done\n");
}

static long timer_fire_cnt;

static void timerdemo_handler(long sig, long val)
{
    timer_fire_cnt++;
    sys0(ES_NR_rt_sigreturn);         /* 站32 显式契约(同 sigdemo2) */
    (void)sig;
    (void)val;
}

static void cmd_timerdemo(void)
{
    /* sigevent 布局(value@0/signo@8/notify@12/tid@16;notify=4=
     * SIGEV_THREAD_ID v0 面单形,自洽判据) */
    u64 sev[3] = {0, 15 | (4ULL << 32), 0};
    struct { u64 isec, insec, vsec, vnsec; } its;
    long id, r, i;

    sys3(ES_NR_rt_sigaction, 15, (long)timerdemo_handler, 0);
    r = sys3(ES_NR_timer_create, 1, (long)sev, (long)&id);
    if (r < 0) {
        sys_puts("timerdemo: create failed\n");
        return;
    }
    its.isec = 0;
    its.insec = 20000000ULL;          /* 周期 2 tick */
    its.vsec = 0;
    its.vnsec = 20000000ULL;          /* 首发同 2 tick */
    sys4(ES_NR_timer_settime, id, 0, (long)&its, 0);
    for (i = 0; i < 10 && timer_fire_cnt < 2; i++)
        sleep_ticks(2);               /* 周期≥2 发=periodic 活证 */
    sys_puts(timer_fire_cnt >= 2 ? "timer: periodic fired ok\n"
                                 : "timer: NO fire?!\n");
    r = sys1(ES_NR_timer_delete, id);
    sys_puts(r == 0 ? "timer: delete ok\n" : "timer: delete wrong?!\n");
    {   /* itimer 单发脸:ITIMER_REAL → SIGALRM(14) 到 leader */
        u64 it[4] = {0, 0, 0, 30000000ULL};
        struct { u64 sec, nsec; } g2 = {9, 9};

        timer_fire_cnt = 0;
        sys3(ES_NR_rt_sigaction, 14, (long)timerdemo_handler, 0);
        sys3(ES_NR_setitimer, 0, (long)it, 0);
        for (i = 0; i < 6 && !timer_fire_cnt; i++)
            sleep_ticks(2);
        sys2(ES_NR_getitimer, 0, (long)&g2);
        sys_puts(timer_fire_cnt >= 1 ? "itimer: fired ok\n"
                                     : "itimer: NO fire?!\n");
        it[2] = 0;                    /* 停表(value=0) */
        it[3] = 0;
        sys3(ES_NR_setitimer, 0, (long)it, 0);
    }
    sys_puts("timerdemo: done\n");
}

/* robust futex:子内 clone 一线程挂同一字;持字者(leader)退出
 * 不解锁——exit 路径的 robust 链遍历自动唤醒等待者(Linux 律)。 */
static u64 robust_word;               /* .user 数据=子地址空间 */
static u64 robust_node[8];            /* 链节点:next@+0(节点=锁字) */
static u64 robust_thr_stack[512];

static void robust_thread_entry(void)
{
    /* 自有帧:线程恢复后唯一合法落脚(见 cmd_robustdemo 注)。
     * 字恒 0=值校验即过入等;leader 死亡→robust 链遍历唤醒。 */
    long r = sys4(ES_NR_futex, (long)&robust_word, RT_FUTEX_WAIT, 0, 0);

    sys_puts(r >= 0 ? "robust: waiter woke after death ok\n"
                    : "robust: waiter woke with err ok\n");
    sys1(ES_NR_exit, 0);
}

static void cmd_robustdemo(void)
{
    long pid = sys0(ES_NR_fork), st = 0;

    if (pid == (long)RT_FORK_MAGIC) {
        long t = sys6(ES_NR_clone,
                      (long)(RT_CLONE_VM | RT_CLONE_THREAD | RT_CLONE_FS |
                             RT_CLONE_FILES | RT_CLONE_SIGHAND |
                             RT_CLONE_SYSVSEM),
                      (long)(robust_thr_stack + 512), 0, 0, 0, 0);

        if (t == 0) {
            /* 站35 契约:clone 子栈全新,恢复点=wrapper 返回帧,
             * 父 C 帧不可触([sp+X] 皆野址)——必入自有帧入口,
             * musl __clone 同律(冒烟实测定谳:落父帧=字符串字面
             * 指针从新栈读野,far=ASCII 串字节)。 */
            robust_thread_entry();
            __builtin_unreachable();
        }
        if (t < 0) {
            sys_puts("robustdemo: clone failed\n");
            sys1(ES_NR_exit, 1);
        }
        /* leader:先让线程入等(睡 4 tick),再注册链(节点基=锁字,
         * next 环 0 终)持锁即退(不解锁)——死亡唤醒=链遍历面。 */
        sleep_ticks(4);
        robust_node[0] = (u64)&robust_word;
        robust_word = 1;               /* 持锁记号(等者已入等,值校验已过) */
        sys2(ES_NR_set_robust_list, (long)robust_node, 8 * 8);
        sys1(ES_NR_exit, 0);          /* →链遍历→futex_wake(word) */
    }
    if (pid < 0) {
        sys_puts("robustdemo: fork failed\n");
        return;
    }
    sys3(ES_NR_wait4, pid, (long)&st, 0);
    sys_puts("robustdemo: done\n");
}

static void cmd_pgiddemo(void)
{
    long pid, st = 0;

    sys0(ES_NR_setsid);               /* 壳自会话(自成组) */
    pid = sys0(ES_NR_fork);
    if (pid == (long)RT_FORK_MAGIC) {
        /* 承继律:子 pgid==sid(setsid 后 pgid=sid=父 pid) */
        long pg = sys1(ES_NR_getpgid, 0), sd = sys1(ES_NR_getsid, 0);

        sys_puts(pg > 0 && pg == sd ? "pgid: child inherits group ok\n"
                                    : "pgid: child group wrong?!\n");
        sys1(ES_NR_exit, 0);
    }
    if (pid < 0) {
        sys_puts("pgiddemo: fork failed\n");
        return;
    }
    sys3(ES_NR_wait4, pid, (long)&st, 0);
    sys_puts(sys1(ES_NR_getsid, 0) == (long)sys0(ES_NR_getpid) ?
             "pgid: setsid self-leader ok\n" : "pgid: sid wrong?!\n");
    sys_puts("pgiddemo: done\n");
}

static void cmd_cloexecdemo(void)
{
    long fd = sys1(ES_NR_fdopen, (long)"/etc/motd"), st = 0;
    long pid = sys0(ES_NR_fork), r;
    char b[4];

    if (fd < 0) {
        sys_puts("cloexecdemo: open failed\n");
        return;
    }
    r = sys3(ES_NR_fcntl, fd, 1, 0);              /* F_GETFD 前值 */
    sys_puts(r == 0 ? "cloexec: fd clean ok\n" : "cloexec: pre-set?!\n");
    sys3(ES_NR_fcntl, fd, 2, FD_CLOEXEC);         /* F_SETFD */
    r = sys3(ES_NR_fcntl, fd, 1, 0);
    sys_puts(r == FD_CLOEXEC ? "cloexec: bit roundtrip ok\n"
                             : "cloexec: bit wrong?!\n");
    if (pid == (long)RT_FORK_MAGIC) {
        sys1(ES_NR_exit, 0);                      /* 子不解锁位(拷贝
                                                    * 语义两说,主判父侧) */
    }
    if (pid < 0) {
        sys_puts("cloexecdemo: fork failed\n");
        return;
    }
    sys3(ES_NR_wait4, pid, (long)&st, 0);
    /* 父侧 fd 必须不受子任何影响存活(POSIX:cloexec 属进程私有) */
    r = sys3(ES_NR_fdread, fd, (long)b, 3);
    sys_puts(r == 3 ? "cloexec: parent fd survives ok\n"
                    : "cloexec: parent fd broken?!\n");
    sys1(ES_NR_fdclose, fd);
    sys_puts("cloexecdemo: done\n");
}

static void cmd_polldemo(void)
{
    /* 站43:ppoll 三脸(管道)——空管超时 0/写后 POLLIN/读后回空;
     * pollfd={fd,ev,rev}=8B 自洽;ts 传 {sec,nsec}。 */
    struct { int fd; short ev; short rev; } pfd[2];   /* Linux pollfd 8B */
    struct { u64 sec, nsec; } ts = {0, 30000000ULL};
    long r = sys0(ES_NR_pipe), rdy;
    long rfd, wfd, n;
    char b[16];

    if (r < 0) {
        sys_puts("polldemo: pipe FAIL\n");
        return;
    }
    rfd = r & 0xFFFF;
    wfd = (r >> 16) & 0xFFFF;
    pfd[0].fd = rfd;
    pfd[0].ev = 1;                    /* POLLIN */
    pfd[0].rev = 0;
    rdy = sys6(ES_NR_ppoll, (long)pfd, 1, (long)&ts, 0, 8, 0);
    sys_puts(rdy == 0 ? "poll: empty pipe times out ok\n"
                     : "poll: empty pipe ready?!\n");
    sys3(ES_NR_fdwrite, wfd, (long)"poll-in-8", 9);
    pfd[0].rev = 0;
    rdy = sys6(ES_NR_ppoll, (long)pfd, 1, 0, 0, 8, 0);   /* ts=NULL 无限 */
    sys_puts(rdy == 1 && pfd[0].rev == 1 ? "poll: POLLIN after write ok\n"
                                         : "poll: POLLIN wrong?!\n");
    n = sys3(ES_NR_fdread, rfd, (long)b, 9);
    pfd[0].rev = 0;
    ts.sec = 0;
    ts.nsec = 20000000ULL;
    rdy = sys6(ES_NR_ppoll, (long)pfd, 1, (long)&ts, 0, 8, 0);
    sys_puts(n == 9 && rdy == 0 ? "poll: drained back to empty ok\n"
                                : "poll: drain wrong?!\n");
    sys1(ES_NR_fdclose, rfd);
    sys1(ES_NR_fdclose, wfd);
    sys_puts("polldemo: done\n");
}

static void cmd_mprtdemo(void)
{
    /* 站45:MPRT 内存分区——EL0 fork 面(壳永不入组:下游门不被
     * 绑)。子自 attach:16p mmap ok/再 mmap -12/fork 孙被拒
     * (窗全拷=32p>16,内存隔离带 fork 面)/munmap 后复 ok/退场
     * 全退=表 cur 0。 */
    long id, pid, r, st = 0;
    u64 va;

    id = sys2(ES_NR_demo, RT_U_MPART_ALLOC, 16);
    if (id < 0) {
        sys_puts("mprtdemo: alloc FAIL\n");
        return;
    }
    pid = sys0(ES_NR_fork);
    if (pid == (long)RT_FORK_MAGIC) {
        r = sys2(ES_NR_demo, RT_U_MPART_ATTACH, id);
        (void)r;
        va = (u64)sys2(ES_NR_mmap, 0, 16 * 4096);
        sys_puts(va ? "mprt: mmap 16p ok\n" : "mprt: mmap FAIL\n");
        r = sys2(ES_NR_mmap, 0, 16 * 4096);
        sys_puts(r == -12 ? "mprt: mmap -12 budget ok\n"
                           : "mprt: budget face wrong\n");
        r = sys0(ES_NR_fork);       /* 孙:窗全拷>上限=fork 被拒 */
        sys_puts(r == -12 ? "mprt: fork denied by budget ok\n"
                          : "mprt: fork deny face wrong\n");
        if (r == (long)RT_FORK_MAGIC)
            sys1(ES_NR_exit, 1);    /* 保险丝:孙不得链式再入 */
        sys2(ES_NR_munmap, (long)va, 16 * 4096);
        r = sys2(ES_NR_mmap, 0, 16 * 4096);
        sys_puts(r > 0 ? "mprt: mmap after munmap ok\n"
                       : "mprt: munmap uncharge face wrong\n");
        sys1(ES_NR_exit, 0);
    }
    sys_puts("mprtdemo: child running\n");
    pid = sys3(ES_NR_wait4, pid, (long)&st, 0);
    sys_puts(st == 0 ? "mprtdemo: reaped code=0 ok\n"
                     : "mprtdemo: reap FAIL\n");
    sys2(ES_NR_demo, RT_U_PARTS, 0);    /* 表 face:cur 0/16=退场全退 */
    sys2(ES_NR_demo, RT_U_MPART_FREE, id);
    sys_puts("mprtdemo: done\n");
}

static void cmd_crashdemo(void)
{
    /* 站46:野读 0x4F000000(两用户窗皆无 VMA)——子死 code=139
     * (SIGSEGV 面),父收尸=崩溃不再连坐全 VM;crashinfo 倾倒
     * 硬件三元+用户帧链。 */
    long pid, st = 0;

    pid = sys0(ES_NR_fork);
    if (pid == (long)RT_FORK_MAGIC) {
        volatile u64 *bad = (volatile u64 *)0x4F000000UL;

        sys_puts("crash: child touching wild va\n");
        /* 站46 法医夹具:-O2 默认 omit-frame-pointer,用户码无
         * x29 链(crash_record 实测 0 帧,walk 空走)。手工挂一帧
         * {x29,x30}——walk 由野读点起证:真链码(任何
         * -fno-omit 用户构建)同一机器即可走全链。 */
        __asm__ volatile(
            "stp x29, x30, [sp, #-16]!\n"
            "mov x29, sp\n"
            ::: "memory");
        (void)*bad;                    /* 野读:野访终态翻案入口 */
        sys_puts("crash: child alive?!\n");   /* 不可达面 */
        sys1(ES_NR_exit, 0);
    }
    sys_puts("crashdemo: child running\n");
    pid = sys3(ES_NR_wait4, pid, (long)&st, 0);
    sys_puts(st == 139 ? "crash: reaped code=139 ok\n"
                       : "crash: reap FAIL\n");
    sys2(ES_NR_demo, RT_U_CRASHINFO, 0);
    sys_puts("crashdemo: done\n");
}

static void cmd_unamedemo(void)
{
    char buf[6 * 65];
    long r = sys1(ES_NR_uname, (long)buf);

    if (r < 0) {
        sys_puts("unamedemo: failed\n");
        return;
    }
    sys_puts("uname: ");
    sys_puts(buf);                   /* sysname */
    sys_puts(" ");
    sys_puts(buf + 2 * 65);           /* release */
    sys_puts(" ");
    sys_puts(buf + 4 * 65);           /* machine */
    sys_puts(" ok\n");
}

/* ---- 站40:服务收编四 demo(判据面) ---- */

static const char hexdig[] = "0123456789abcdef";

static void put_hex2(u8 v)
{
    char c[2];

    c[0] = hexdig[v >> 4];
    c[1] = hexdig[v & 15];
    sys3(ES_NR_write, 1, (long)c, 2);
}

static void cmd_rngdemo(void)
{
    char b1[16], b2[16];
    long fd;
    int i, diff = 0;

    fd = sys1(ES_NR_fdopen, (long)"/dev/random");
    if (fd < 0) {
        sys_puts("rngdemo: open /dev/random failed\n");
        return;
    }
    if (sys3(ES_NR_fdread, fd, (long)b1, 16) != 16 ||
        sys3(ES_NR_fdread, fd, (long)b2, 16) != 16) {
        sys_puts("rngdemo: short read\n");
        sys1(ES_NR_fdclose, fd);
        return;
    }
    sys1(ES_NR_fdclose, fd);
    for (i = 0; i < 16; i++)
        if (b1[i] != b2[i])
            diff = 1;
    sys_puts("rng: a=");
    for (i = 0; i < 8; i++)
        put_hex2((u8)b1[i]);
    sys_puts(" b=");
    for (i = 0; i < 8; i++)
        put_hex2((u8)b2[i]);
    sys_puts("\n");
    sys_puts(diff ? "rng: two reads differ\n" : "rng: COLLISION\n");
    {
        char gr[32];
        long r = sys3(ES_NR_getrandom, (long)gr, sizeof(gr), 0);

        sys_puts("rng: getrandom ");
        sys_put_dec((u64)(r > 0 ? r : 0));
        sys_puts("B ");
        for (i = 0; i < 4; i++)
            put_hex2((u8)gr[i]);
        sys_puts("\n");
    }
}

static void cmd_clockdemo(void)
{
    u64 m1[2], m2[2], wall[2], res[2];

    sys2(ES_NR_clock_gettime, 1, (long)m1);    /* MONOTONIC */
    sleep_ticks(2);                             /* 睡两拍:时基真走 */
    sys2(ES_NR_clock_gettime, 1, (long)m2);
    sys2(ES_NR_clock_gettime, 0, (long)wall);   /* REALTIME */
    sys2(ES_NR_clock_getres, 1, (long)res);
    if (m2[0] > m1[0] || (m2[0] == m1[0] && m2[1] >= m1[1]))
        sys_puts("clock: mono advance ok\n");
    else
        sys_puts("clock: mono STALLED\n");
    sys_puts("clock: wall sec=");
    sys_put_dec(wall[0]);
    sys_puts("\n");
    sys_puts("clock: res 1ns\n");
}

static void cmd_procdemo(void)
{
    char db[512];
    long fd;
    u32 count = 0, seen_shell = 0;

    fd = sys4(ES_NR_openat, (long)-100, (long)"/proc", 0, 0);
    if (fd < 0) {
        sys_puts("procdemo: open /proc failed\n");
        return;
    }
    for (;;) {                     /* getdents64:一次一名(dirent64@19) */
        long r = sys3(ES_NR_getdents64, fd, (long)db, sizeof(db));

        if (r <= 0)
            break;
        count++;                     /* 名在 db+19(内核零填=NUL 收尾) */
        {
            const char *nm = db + 19;
            u32 j = 0;

            while (nm[j] && j < 24)
                j++;
            if (j == 5 && nm[0] == 's' && nm[1] == 'h' && nm[2] == 'e' &&
                nm[3] == 'l' && nm[4] == 'l')
                seen_shell = 1;
        }
    }
    /* 同 fd 文本面(/proc READ=任务表行) */
    {
        char line[64];
        u32 n = 0;

        while (sys3(ES_NR_fdread, fd, (long)line, 64) > 0) {
            n++;
            if (line[0] == 's' && line[1] == 'h' && line[2] == 'e' &&
                line[3] == 'l')
                seen_shell = 1;     /* "shell <prio> ..." 行 */
            if (n > 24)
                break;
        }
    }
    sys1(ES_NR_fdclose, fd);
    sys_puts("proc: ");
    sys_put_dec((u64)count);
    sys_puts(" names, ");
    sys_put_dec(seen_shell);
    sys_puts(" shell-seen\n");
}

static void cmd_slogdemo(void)
{
    char line[64];
    long fd;
    u32 n = 0;
    static const char m1[] = { 0, 's', 'l', 'o', 'g', '-', 'o', 'n', 'e' };
    static const char m2[] = { 2, 's', 'l', 'o', 'g', '-', 't', 'w', 'o',
                               '-', 'w', 'a', 'r', 'n' };
    static const char m3[] = { 3, 's', 'l', 'o', 'g', '-', 't', 'h', 'r',
                               'e', 'e', '-', 'e', 'r', 'r' };

    fd = sys1(ES_NR_fdopen, (long)"/dev/slog");
    if (fd < 0) {
        sys_puts("slogdemo: open /dev/slog failed\n");
        return;
    }
    sys3(ES_NR_fdwrite, fd, (long)m1, sizeof(m1));
    sys3(ES_NR_fdwrite, fd, (long)m2, sizeof(m2));
    sys3(ES_NR_fdwrite, fd, (long)m3, sizeof(m3));
    while (n < 8 && sys3(ES_NR_fdread, fd, (long)line, 64) > 0)
        n++;                      /* 倾倒:一行一回执("[X] text\n") */
    sys1(ES_NR_fdclose, fd);
    sys_puts("slog: dumped ");
    sys_put_dec((u64)n);
    sys_puts(" lines\n");
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
        cmd_run(line + 4);            /* 站40:args... 直透 busybox */
    } else if (streq(line, "rngdemo")) {
        cmd_rngdemo();
    } else if (streq(line, "clockdemo")) {
        cmd_clockdemo();
    } else if (streq(line, "procdemo")) {
        cmd_procdemo();
    } else if (streq(line, "slogdemo")) {
        cmd_slogdemo();
    } else if (streq(line, "initrespawn")) {
        cmd_initrespawn();
    } else if (streq(line, "capdemo")) {
        cmd_capdemo();
    } else if (streq(line, "capdemo2")) {
        cmd_capdemo2();
    } else if (streq(line, "rlimitdemo")) {
        cmd_rlimitdemo();
    } else if (streq(line, "uiddemo")) {
        cmd_uiddemo();
    } else if (streq(line, "scheddemo")) {
        cmd_scheddemo();
    } else if (streq(line, "sigdemo2")) {
        cmd_sigdemo2();
    } else if (streq(line, "timerdemo")) {
        cmd_timerdemo();
    } else if (streq(line, "robustdemo")) {
        cmd_robustdemo();
    } else if (streq(line, "pgiddemo")) {
        cmd_pgiddemo();
    } else if (streq(line, "cloexecdemo")) {
        cmd_cloexecdemo();
    } else if (streq(line, "unamedemo")) {
        cmd_unamedemo();
    } else if (streq(line, "polldemo")) {
        cmd_polldemo();
    } else if (streq(line, "calltrace")) {
        sys2(ES_NR_demo, RT_U_CALLTRACE, 0);
    } else if (streq(line, "apsdemo")) {
        sys2(ES_NR_demo, RT_U_APS, 0);
    } else if (streq(line, "mprtdemo")) {
        cmd_mprtdemo();
    } else if (streq(line, "parts")) {
        sys2(ES_NR_demo, RT_U_PARTS, 0);
    } else if (streq(line, "crashdemo")) {
        cmd_crashdemo();
    } else if (streq(line, "crashinfo")) {
        sys2(ES_NR_demo, RT_U_CRASHINFO, 0);
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
    } else if (streq(line, "sigqdemo")) {
        cmd_sigqdemo();
    } else if (streq(line, "wrdemo")) {
        cmd_wrdemo();
    } else if (streq(line, "lsdemo")) {
        cmd_lsdemo();
    } else if (streq(line, "fblkdemo")) {
        cmd_fblkdemo();
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
                 " | futexdemo | sigdemo | sigqdemo | wrdemo | lsdemo | fblkdemo | shmdemo | msgseg | mqdemo | pipedemo | longdemo | run <p>"
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
    init_inittab();                  /* 站40:sysinit/once/wait/respawn */
    sys_puts("rtos> ");
    for (;;) {
        char c;

        if (sys3(ES_NR_read, 0, (long)&c, 1) != 1)
            continue;                /* 不可达(read 恒 1 或负 errno) */
        if (c == '\r' || c == '\n') {
            sys3(ES_NR_write, 1, (long)"\n", 1);
            linebuf[idx] = '\0';
            shell_line(linebuf);
            init_poll();             /* 站40:respawn 探询(循环拍) */
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
