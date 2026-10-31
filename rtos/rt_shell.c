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
                 " | forkdemo | mmapdemo | exedemo | ipcdemo | pulsedemo | abortdemo"
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
