/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
/*
 * elog_be.c — eshine_log 契约的 EL2 侧后端钩子(站 49)
 *
 * clock=cntpct/freq(us);emit_str=console 行级锁窗内整行(站 22A
 * 行级原子纪律——锁内 pl011 直写,\n→\r 与 console_putc 同律);
 * panic_char=裸 pl011 无锁(panic 路径,仅 panicking CPU);
 * ring_store=console 锁窗内 raw(环完整性,4 CPU 多写者)。
 * log_halt 用核心弱默认(wfi 环);log_vsnprintf 用核心自带实现。
 */
#include "kernel/include/types.h"
#include "kernel/include/console.h"
#include "drivers/include/pl011.h"
#include "eshine_log.h"

unsigned long long log_clock_us(void)
{
    static unsigned long long freq;
    unsigned long long now;

    if (!freq)
        __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(now));
    return now / (freq / 1000000ULL);
}

void log_emit_char(char c)
{
    console_putc(c);                  /* 原语持锁(站 22A) */
}

void log_emit_str(const char *s, unsigned int len)
{
    unsigned int i;

    console_lock();
    for (i = 0; s && i < len; i++) {
        if (s[i] == '\n')
            pl011_putc('\r');        /* 终端换行惯例,console_putc 同律 */
        pl011_putc(s[i]);
    }
    console_unlock();
}

void log_emit_panic_char(char c)
{
    if (c == '\n')
        pl011_putc('\r');
    pl011_putc(c);
}

void log_ring_store(const char *buf, unsigned int len, unsigned int level,
                    const char *mod, unsigned long long ts_us)
{
    (void)level; (void)mod; (void)ts_us;
    console_lock();
    eshine_log_ring_raw(buf, len);
    console_unlock();
}

/* ============================================================
 * 控制面(shell 四命令的命令体;本 TU 自注册模块 "elog")
 * ============================================================ */
LOG_MODULE_REGISTER("elog");

static void mod_count_cb(const char *name, unsigned int level, void *u)
{
    (void)name; (void)level;
    (*(unsigned int *)u)++;
}

unsigned int elog_mod_count(void)
{
    unsigned int n = 0;

    log_list_modules(mod_count_cb, &n);
    return n;
}

/* 级别词→枚举:off/err/wrn/inf/dbg/trc 或数字 0-5;-1=未识 */
static int level_by_word(const char *s, unsigned int len)
{
    static const char *names[6] = { "off", "err", "wrn", "inf", "dbg", "trc" };
    unsigned int i;

    if (len == 1 && s[0] >= '0' && s[0] <= '5')
        return s[0] - '0';
    for (i = 0; i < 6; i++) {
        const char *n = names[i];
        unsigned int j = 0;

        while (j < len && n[j] && n[j] == s[j])
            j++;
        if (j == len && n[j] == 0)
            return (int)i;
    }
    return -1;
}

void elog_logdemo(void)
{
    eshine_log_ratelimit_t rl = { 0, 0, 0 };
    unsigned int i, pass = 0;

    LOG_ERR("logdemo err line %d\n", 1);
    LOG_WRN("logdemo wrn line %d\n", 2);
    LOG_INF("logdemo inf line %d\n", 3);
    LOG_DBG("logdemo dbg line %d\n", 4);    /* 默认 INF:两行滤除面 */
    LOG_TRC("logdemo trc line %d\n", 5);
    for (i = 0; i < 10; i++)                /* burst=3/10 发:限流面 */
        if (eshine_log_ratelimit(&rl, 1000000, 3))
            pass++;
    LOG_INF("ratelimit pass=%u suppressed=%u\n", pass, rl.suppressed);
}

/* "loglevel <lvl>"(全局) | "loglevel <mod> <lvl>"(per 模块);
 * args 已剥命令词(壳层惯例) */
void elog_loglevel_args(const char *args)
{
    char w1[16], w2[16];
    unsigned int n1 = 0, n2 = 0;

    while (*args == ' ')
        args++;
    while (args[n1] && args[n1] != ' ' && n1 < sizeof(w1) - 1)
        n1++;
    /* 注:n1 递进在循环体内(先取长后拷贝,免二次走查) */
    {
        unsigned int j;

        for (j = 0; j < n1; j++)
            w1[j] = args[j];
        w1[j] = 0;
    }
    args += n1;
    while (*args == ' ')
        args++;
    while (args[n2] && args[n2] != ' ' && n2 < sizeof(w2) - 1)
        n2++;
    {
        unsigned int j;

        for (j = 0; j < n2; j++)
            w2[j] = args[j];
        w2[j] = 0;
    }
    if (n1 == 0) {
        console_puts("usage: loglevel <lvl> | loglevel <mod> <lvl>\n");
        return;
    }
    if (n2 == 0) {                          /* 单词面=全局级别 */
        int lvl = level_by_word(w1, n1);

        if (lvl < 0) {
            console_puts("loglevel: unknown level\n");
            return;
        }
        log_set_global_level((eshine_log_level_t)lvl);
        console_puts("loglevel: global ");
        console_puts(eshine_log_level_str((eshine_log_level_t)lvl));
        console_puts("\n");
        return;
    }
    {                                       /* 双词面=per 模块 */
        int lvl = level_by_word(w2, n2);

        if (lvl < 0) {
            console_puts("loglevel: unknown level\n");
            return;
        }
        if (log_level_set(w1, (eshine_log_level_t)lvl) < 0) {
            console_puts("loglevel: no such module\n");
            return;
        }
        console_puts("loglevel: ");
        console_puts(w1);
        console_puts(" ");
        console_puts(eshine_log_level_str((eshine_log_level_t)lvl));
        console_puts("\n");
    }
}

static void logmod_cb(const char *name, unsigned int level, void *u)
{
    (void)u;
    console_puts("  ");
    console_puts(name);
    console_puts(": ");
    console_puts(eshine_log_level_str((eshine_log_level_t)level));
    console_puts("\n");
}

void elog_logmods(void)
{
    console_puts("log mods:\n");
    log_list_modules(logmod_cb, NULL);
    console_puts("  global: ");
    console_puts(eshine_log_level_str(log_get_global_level()));
    console_puts("\n");
}

static void dump_char(char c)
{
    console_putc(c);
}

void elog_logdump(void)
{
    console_puts("logdump: --- ring start ---\n");
    log_dump_ring(dump_char);
    console_puts("logdump: --- ring end ---\n");
}
