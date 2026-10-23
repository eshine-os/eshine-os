/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "kernel/include/console.h"
#include "kernel/include/sched.h"
#include "kernel/include/shell.h"
#include "kernel/include/types.h"
#include "kernel/include/util.h"
#include "kernel/include/version.h"
#include "kernel/include/vm.h"
#include "drivers/include/pl011.h"
#include "drivers/include/timer.h"

#define SHELL_BUF_MAX 64

static char linebuf[SHELL_BUF_MAX];

static void cmd_help(void)
{
    console_puts("commands: help version echo <text> state tick vmls vmcreate <id>\n");
    console_puts("          vmrun <id> vmconsole <id|host> vmstat vmkill <id>\n");
}

static void cmd_version(void)
{
    console_puts(ESHINE_BANNER "\n");
    console_puts("  hypervisor:  " ESHINE_VERSION_STR "\n");
    console_puts("  rtos:        not-yet (VM0 management domain, planned)\n");
    console_puts("  guest-linux: not-yet (upstream LTS pinned per release)\n");
}

static void cmd_echo(const char *args)
{
    console_puts(args);
    console_putc('\n');
}

static void cmd_state(void)
{
    vm_state_demo();
}

static void cmd_tick(void)
{
    console_puts("ticks: ");
    util_putdec((u32)timer_ticks());
    console_puts("\n");
}

static void cmd_vmstat(void)
{
    vm_list();
    console_puts("preempts: ");
    util_putdec((u32)sched_preempts);
    console_puts("  ticks: ");
    util_putdec((u32)timer_ticks());
    console_puts("\n");
}

/* 单数字 id 解析("0"/"1");其余拒绝 */
static u32 parse_id(const char *s, bool *ok)
{
    if (s[0] >= '0' && s[0] <= '9' && s[1] == '\0') {
        *ok = true;
        return (u32)(s[0] - '0');
    }
    *ok = false;
    return 0;
}

static void dispatch(char *line)
{
    /* 就地拆出第一个词与参数 */
    char *args = line;

    while (*args && *args != ' ')
        args++;
    if (*args == ' ') {
        *args++ = '\0';
        while (*args == ' ')
            args++;
    } else {
        *args = '\0';
    }

    if (strncmp(line, "help", 64) == 0) {
        cmd_help();
    } else if (strncmp(line, "version", 64) == 0) {
        cmd_version();
    } else if (strncmp(line, "echo", 64) == 0) {
        cmd_echo(args);
    } else if (strncmp(line, "state", 64) == 0) {
        cmd_state();
    } else if (strncmp(line, "tick", 64) == 0) {
        cmd_tick();
    } else if (strncmp(line, "vmls", 64) == 0) {
        vm_list();
    } else if (strncmp(line, "vmcreate", 64) == 0) {
        bool ok;
        u32 id = parse_id(args, &ok);

        if (ok)
            vm_create(id);
        else
            console_puts("usage: vmcreate <id>\n");
    } else if (strncmp(line, "vmrun", 64) == 0) {
        bool ok;
        u32 id = parse_id(args, &ok);

        if (ok)
            vm_start(id);               /* 异步:置 RUNNING 立即回,泵接管 */
        else
            console_puts("usage: vmrun <id>\n");
    } else if (strncmp(line, "vmconsole", 64) == 0) {
        if (strncmp(args, "host", 64) == 0) {
            sched_console_owner = NULL;
            console_puts("console -> host\n");
        } else {
            bool ok;
            u32 id = parse_id(args, &ok);

            if (ok && id < ESHINE_MAX_VMS) {
                /* 死/暂停的 guest 由善后路径自动回收 owner */
                sched_console_owner = vm_vcpu(id);
                console_puts("console -> vm\n");
            } else {
                console_puts("usage: vmconsole <id|host>\n");
            }
        }
    } else if (strncmp(line, "vmstat", 64) == 0) {
        cmd_vmstat();
    } else if (strncmp(line, "vmkill", 64) == 0) {
        bool ok;
        u32 id = parse_id(args, &ok);

        if (ok)
            vm_kill(id);
        else
            console_puts("usage: vmkill <id>\n");
    } else if (*line) {
        console_puts("unknown command: ");
        console_puts(line);
        console_puts(" (try help)\n");
    }
}

/* 泵主循环:shell 即最高优先级任务——有输入则处理(命令=原子段,泵停摆),
 * 无输入则泵一轮(每个就绪 VM 一个量子);owner 让渡给 guest 时宿主不读
 * RX,纯泵。命令之间的输入响应性=泵的量子粒度。 */
void shell_run(void)
{
    u32 idx = 0;

    console_puts("eshine> ");
    for (;;) {
        int ch = (sched_console_owner == NULL)
                     ? pl011_getc_poll() : -1;

        if (ch >= 0) {
            char c = (char)ch;

            if (c == '\r' || c == '\n') {
                console_putc('\n');
                linebuf[idx] = '\0';
                dispatch(linebuf);
                idx = 0;
                console_puts("eshine> ");
            } else if ((c == 0x7f || c == '\b') && idx > 0) {
                idx--;
                console_puts("\b \b");
            } else if (idx < SHELL_BUF_MAX - 1) {
                linebuf[idx++] = c;
                console_putc(c);
            }
            continue;
        }
        (void)sched_pump();
    }
}
