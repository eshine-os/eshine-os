/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "rtos.h"
#include "miniuart.h"
#include "hvc.h"

/* 壳任务:RX 阻塞-唤醒(IRQ 驱动,人格分水岭——裸机客是轮询循环)。
 * HVC 兼容面:yield/host/exit 与裸机同语义(门禁存量不破)。 */

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

static void rt_selftest(void)
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

static void rt_ps(void)
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
            rt_puts("\n");
        }
        t = rt_ps_next(&idx);
    }
}

/* spawn 任务体:名字由任务自己打(池槽 name 已存,arg 再传一份串) */
static void spawned_task(void *arg)
{
    rt_puts((const char *)arg);
    rt_puts(": hello from task\n");
}

static void shell_line(char *line)
{
    if (streq(line, "exit")) {
        hvc_exit();                     /* noreturn */
    } else if (streq(line, "yield")) {
        rt_puts("yielding...\n");
        hvc_yield();
        rt_puts("resumed\n");
    } else if (streq(line, "selftest")) {
        rt_selftest();
    } else if (streq(line, "ps")) {
        rt_ps();
    } else if (streq(line, "fp")) {
        if (rt_task_create("fptest", rt_fp_task, NULL, 12))
            rt_puts("fp: task spawned\n");
        else
            rt_puts("fp: pool full\n");
    } else if (streq(line, "semdemo")) {
        rt_demo_sem();               /* 站十七:演示即判据 */
    } else if (streq(line, "mutexdemo")) {
        rt_demo_mutex();
    } else if (starts(line, "spawn ")) {
        static char spawn_name[12];     /* 单缓冲:并发 spawn 会互踩,
                                         * demo 尺度一次一个(记残余) */
        const char *nm = line + 6;
        unsigned int i;

        for (i = 0; i < sizeof(spawn_name) - 1 && nm[i]; i++)
            spawn_name[i] = nm[i];
        spawn_name[i] = '\0';
        if (i == 0) {
            rt_puts("usage: spawn <name>\n");
        } else if (!rt_task_create(spawn_name, spawned_task, spawn_name, 8)) {
            rt_puts("spawn: pool full\n");
        }
        /* 成功静默:问候行由任务自己打 */
    } else if (starts(line, "sleep")) {
        unsigned int secs = 2;

        if (line[5] == ' ' && line[6] >= '1' && line[6] <= '9' && line[7] == '\0')
            secs = (unsigned int)(line[6] - '0');
        else if (line[5] != '\0')
            secs = 0;                     /* 参数不合法:走 usage */
        if (secs == 0) {
            rt_puts("usage: sleep [1-9]\n");
        } else {
            rt_puts("sleeping...\n");
            rt_sleep_ticks((u64)secs * RT_HZ);   /* 任务睡,心跳不睡 */
            rt_puts("slept (rtos sleep)\n");
        }
    } else if (streq(line, "host")) {
        rt_puts("releasing console...\n");
        hvc_host();
    } else if (starts(line, "echo ")) {
        rt_puts(line + 5);
        rt_puts("\n");
    } else if (line[0]) {
        rt_puts("rtos: try echo <t> | selftest | ps | spawn <n> | sleep [1-9]"
                " | fp | yield | semdemo | mutexdemo | host | exit\n");
    }
}

void rt_shell_task(void *arg)
{
    unsigned int idx = 0;

    (void)arg;
    rt_puts("rtos> ");
    for (;;) {
        int c = rt_getc_nb();

        if (c < 0) {
            rt_block_rx();               /* 无键:阻塞等 RX(IRQ 唤醒) */
            continue;
        }
        if (c == '\r' || c == '\n') {
            uart_putc('\n');
            linebuf[idx] = '\0';
            shell_line(linebuf);
            idx = 0;
            rt_puts("rtos> ");
        } else if ((c == 0x7f || c == '\b') && idx > 0) {
            idx--;
            uart_putc('\b');
            uart_putc(' ');
            uart_putc('\b');
        } else if (idx < RT_BUF_MAX - 1 && c >= 0x20 && c < 0x7f) {
            linebuf[idx++] = (char)c;
            uart_putc((char)c);
        }
    }
}
