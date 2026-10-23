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
    console_puts("guest:    selftest sleep [N] spin echo <text> host exit\n");
    console_puts("make verify walkthrough (31 步;与 make run 同一构建,可手动过):\n");
    console_puts("  echo ESHINEOKx      -> token 双次回显        [3]\n");
    console_puts("  tick x2             -> ticks 递增             [4]\n");
    console_puts("  vmls / vmcreate 0   -> 空表 / CREATED         [5-6]\n");
    console_puts("  vmconsole 0         -> denied: not running    [7]\n");
    console_puts("  vmrun 0             -> [vm0] banner (vuart)   [8]\n");
    console_puts("  vmconsole 0 + 回车   -> vm0> (RX 唤醒链)       [9]\n");
    console_puts("  selftest            -> 100 MMIO 陷阱 OK       [10]\n");
    console_puts("  host / vmstat       -> console->host; RUNNING [11]\n");
    console_puts("  vmcreate 1 / vmrun 1 / vmconsole 1 + 回车 -> [vm1] / vm1> [12-13]\n");
    console_puts("  vmconsole host / vmstat -> both RUNNING      [13]\n");
    console_puts("  vmconsole 0+回车 / echo GOKAx / host         [14]\n");
    console_puts("  vmconsole 1+回车 / echo GOKBx / host         [15]\n");
    console_puts("  vmstat / vmconsole 0+回车 / spin -> spun 3G   [16]\n");
    console_puts("    (preempts 前后差>=2:跨量子无损转写)\n");
    console_puts("  vmconsole 0+回车 / sleep -> 公平+后台 slept   [17-18]\n");
    console_puts("  vmconsole 0+回车 / exit    -> VM_DEAD         [19]\n");
    console_puts("  vmconsole 1+回车 / fault   -> 隔离死          [20]\n");
    console_puts("  vmls / echo 复验 / tick x2 -> 双 DEAD/仍递增   [21-23]\n");
    console_puts("  vmkill 1 / vmstat   -> denied / 收尾          [24-25]\n");
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
            if (sched_console_owner) {
                /* 切换前冲原 owner 半行(按其积累时 tag),免前缀串味 */
                struct eshine_vm *vm = vm_of_vcpu(sched_console_owner);

                vuart_tx_flush(&vm->vu, (u32)vm->vmid);
            }
            sched_console_owner = NULL;
            console_puts("console -> host\n");
        } else {
            bool ok;
            u32 id = parse_id(args, &ok);

            if (ok && id < ESHINE_MAX_VMS) {
                struct eshine_vm *vm = vm_of_vcpu(vm_vcpu(id));

                /* 状态门(第六站 bug 修复):只有 RUNNING 的 VM 消费输入,
                 * owner 指给非 RUNNING 的 VM = 所有输入入环无人读,且
                 * vmconsole host 自救同样被路由——终端永久锁死。默认拒绝,
                 * 宿主命令面永远可用(vmconsole host 无条件)。 */
                if (vm->state != VM_RUNNING) {
                    console_puts("vm");
                    util_putdec(id);
                    console_puts(": denied: not running (vmrun first)\n");
                } else {
                    /* 新 owner 若在 WAIT 即唤醒(RX 等待者输入已可入环;
                     * TIMER 等待者醒后自查 wake_count,空转一轮无害)。 */
                    vuart_tx_flush(&vm->vu, id);   /* 先冲半行再切 owner */
                    sched_console_owner = vm_vcpu(id);
                    if (sched_console_owner->wait_state == 1UL)
                        sched_console_owner->wait_state = 0UL;
                    console_puts("console -> vm\n");
                }
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

/* 泵主循环:shell 即最高优先级任务——owner=host 时有输入则处理(命令=
 * 原子段,泵停摆),无输入则泵一轮(每个就绪 VM 一个量子);owner=guest
 * 时宿主恒排空 RX 入 owner 的 vuart 环(shell 即仲裁者,第六站)。命令
 * 之间的输入响应性=泵的量子粒度。 */
void shell_run(void)
{
    u32 idx = 0;

    console_puts("eshine> ");
    for (;;) {
        int ch = pl011_getc_poll();    /* 恒排空:按 owner 路由,不再门控 */

        if (ch >= 0) {
            char c = (char)ch;

            if (sched_console_owner) {
                struct vcpu *v = sched_console_owner;
                struct eshine_vm *vm = vm_of_vcpu(v);

                /* 前台 guest 的输入入其 vuart 环(RIS 置位);RXIM 使能
                 * 时泵入口见 irq_ready 即注 LR1,无需显式标志。
                 * RX 等待者即刻直通唤醒。 */
                vuart_rx_push(&vm->vu, c);
                if (v->wait_state == 1UL && v->wait_reason == VCPU_WAIT_RX)
                    v->wait_state = 0UL;    /* 直通 runnable:注入即取 */
                continue;
            }
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
