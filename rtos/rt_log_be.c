/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
/*
 * rt_log_be.c — eshine_log 契约的 RTOS 侧后端钩子(站 49)
 *
 * clock=cntvct/freq(us,guest 墙钟可读,rngsrv 同源);emit_str=
 * rt_puts_n(行级自旋锁+DAIF 屏,站 28 长度式行级打印);\n 不插 \r
 * (宿主 vuart TX 面转换,uart_putc 同律);panic_char=裸 uart_putc
 * (panic 路径,rt_panic 的 puts_uart 同律);ring_store=自旋锁窗内
 * raw(环完整性,双核多写者)。
 */
#include "rtos.h"
#include "miniuart.h"
#include "eshine_log.h"

unsigned long long log_clock_us(void)
{
    unsigned long long freq, now;

    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(now));
    return now / (freq / 1000000ULL);
}

void log_emit_char(char c)
{
    uart_putc(c);
}

void log_emit_str(const char *s, unsigned int len)
{
    rt_puts_n(s, len);
}

void log_emit_panic_char(char c)
{
    uart_putc(c);
}

static unsigned long log_ring_lk;

void log_ring_store(const char *buf, unsigned int len, unsigned int level,
                    const char *mod, unsigned long long ts_us)
{
    u64 d = rt_spin_lock(&log_ring_lk);

    (void)level; (void)mod; (void)ts_us;
    eshine_log_ring_raw(buf, len);
    rt_spin_unlock(&log_ring_lk, d);
}

/* ============================================================
 * 控制面(sys_demo 蹦床的命令体;本 TU 自注册模块 "elog")
 * ============================================================ */
LOG_MODULE_REGISTER("elog");

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

static void mod_count_cb(const char *name, unsigned int level, void *u)
{
    (void)name; (void)level;
    (*(unsigned int *)u)++;
}

unsigned int rt_log_mod_count(void)
{
    unsigned int n = 0;

    log_list_modules(mod_count_cb, &n);
    return n;
}

void k_logdemo(void)
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

/* "loglevel <lvl>"(全局) | "loglevel <mod> <lvl>";uargs=EL0 壳行
 * 内指针,界内直读拷贝(k_spawn 同律,v0 单窗静态) */
void k_loglevel_args(const char *uargs)
{
    char w1[16], w2[16];
    unsigned int n1 = 0, n2 = 0;

    while (*uargs == ' ')
        uargs++;
    while (uargs[n1] && uargs[n1] != ' ' && n1 < sizeof(w1) - 1)
        n1++;
    {
        unsigned int j;

        for (j = 0; j < n1; j++)
            w1[j] = uargs[j];
        w1[j] = 0;
    }
    uargs += n1;
    while (*uargs == ' ')
        uargs++;
    while (uargs[n2] && uargs[n2] != ' ' && n2 < sizeof(w2) - 1)
        n2++;
    {
        unsigned int j;

        for (j = 0; j < n2; j++)
            w2[j] = uargs[j];
        w2[j] = 0;
    }
    if (n1 == 0) {
        rt_puts("usage: loglevel <lvl> | loglevel <mod> <lvl>\n");
        return;
    }
    if (n2 == 0) {                          /* 单词面=全局级别 */
        int lvl = level_by_word(w1, n1);

        if (lvl < 0) {
            rt_puts("loglevel: unknown level\n");
            return;
        }
        log_set_global_level((eshine_log_level_t)lvl);
        rt_puts("loglevel: global ");
        rt_puts(eshine_log_level_str((eshine_log_level_t)lvl));
        rt_puts("\n");
        return;
    }
    {                                       /* 双词面=per 模块 */
        int lvl = level_by_word(w2, n2);

        if (lvl < 0) {
            rt_puts("loglevel: unknown level\n");
            return;
        }
        if (log_level_set(w1, (eshine_log_level_t)lvl) < 0) {
            rt_puts("loglevel: no such module\n");
            return;
        }
        rt_puts("loglevel: ");
        rt_puts(w1);
        rt_puts(" ");
        rt_puts(eshine_log_level_str((eshine_log_level_t)lvl));
        rt_puts("\n");
    }
}

static void logmod_cb(const char *name, unsigned int level, void *u)
{
    (void)u;
    rt_puts("  ");
    rt_puts(name);
    rt_puts(": ");
    rt_puts(eshine_log_level_str((eshine_log_level_t)level));
    rt_puts("\n");
}

void k_logmods(void)
{
    rt_puts("log mods:\n");
    log_list_modules(logmod_cb, NULL);
    rt_puts("  global: ");
    rt_puts(eshine_log_level_str(log_get_global_level()));
    rt_puts("\n");
}

static void dump_char(char c)
{
    uart_putc(c);
}

void k_logdump(void)
{
    rt_puts("logdump: --- ring start ---\n");
    log_dump_ring(dump_char);
    rt_puts("logdump: --- ring end ---\n");
}
