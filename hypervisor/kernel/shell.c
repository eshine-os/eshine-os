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
    console_puts("    (前台会话期 Ctrl-](0x1d)=宿主夺回控制台)\n");
    console_puts("guest:    selftest sleep [N] spin dwrite dread fault echo <text> host exit\n");
    console_puts("rtos:     selftest ps spawn <n> sleep [N] fp fpdual yield semdemo mutexdemo\n");
    console_puts("          pidemo host exit\n");
    console_puts("make verify walkthrough (62 步;与 make run 同一构建,可手动过;\n");
    console_puts("两核真并发:vm0/vm1 在 CPU0,vm2(Linux)在 CPU1 专泵):\n");
    console_puts("  echo ESHINEOKx      -> token 双次回显        [4]\n");
    console_puts("  cpu1 online         -> psci cpu_on 次核起立   [3]\n");
    console_puts("  tick x2             -> ticks 递增             [5]\n");
    console_puts("  vmls / vmcreate 0   -> 空表 / CREATED         [6-7]\n");
    console_puts("  vmconsole 0         -> denied: not running    [8]\n");
    console_puts("  vmrun 0             -> [vm0] rtos banner      [9]\n");
    console_puts("  (rtos 自报)         -> fdt ok ×3 / pl011 / gicd / gic / uart irq / virtio [10-16]\n");
    console_puts("  (rtos 自报)         -> rtos: up 位图+tick     [17]\n");
    console_puts("  vmconsole 0 + 回车   -> rtos> (RX 唤醒链)      [18]\n");
    console_puts("  selftest / ps / spawn / fp / sleep 3          [19-23]\n");
    console_puts("  yield / vmrun 0     -> PAUSED -> resumed      [24]\n");
    console_puts("  host / vmstat       -> console->host; RUNNING [25]\n");
    console_puts("  semdemo             -> 环 8/8(信号量阻塞乒乓)[26]\n");
    console_puts("  mutexdemo + ps      -> waiter BLOCK 活体      [27]\n");
    console_puts("  (mutexdemo 收尾)     -> 醒序+递归+非拥有者拒绝 [28]\n");
    console_puts("  pidemo              -> PI 反超(mid 饿死 low,high boost)[30]\n");
    console_puts("  fpdual              -> 双 FP 任务双精确(帧)[31]\n");
    console_puts("  vmcreate 1 / vmrun 1 / vmconsole 1          [32-33]\n");
    console_puts("  vm0 echo / vm1 dwrite+dread / vm1 echo     [34-36]\n");
    console_puts("  vm1 spin -> spun(preempts 差>=2)            [37]\n");
    console_puts("  vm0 sleep 公平+vm1 后台醒                    [38-39]\n");
    console_puts("  双睡醒序(rtos 2s 先于 vm1 one-shot 4s)      [40]\n");
    console_puts("  双醒后双活 echo                                [41]\n");
    console_puts("  持久化:重建后旧数据仍在                       [42]\n");
    console_puts("  fault -> vm1 隔离死,宿主无损                  [43]\n");
    console_puts("  vmls(rtos RUNNING 留活)/宿主完整/退场后 tick   [44-46]\n");
    console_puts("  vmkill 拒绝路径 / vmstat 收尾                 [47-48]\n");
    console_puts("  vmcreate 2 / vmrun 2 -> Booting Linux @CPU1    [49]\n");
    console_puts("  vm1 复活 spin 独占 CPU0,vm2 行进=真并行       [50]\n");
    console_puts("  Machine model / arch_timer / GICv3          [51-53]\n");
    console_puts("  心跳夹 boot 流      -> 双客并发时序证明        [54]\n");
    console_puts("  ttyAMA0 / vda 128 blocks / initramfs / busybox 壳 [55-58]\n");
    console_puts("    (0x1d Ctrl-] = 宿主夺回;owner 不再自动归还)\n");
    console_puts("  echo TOKEN / cat /proc/cpuinfo(cpuinfo 0xd07) [59-60]\n");
    console_puts("  vmconsole 0 / exit -> rtos 迟退 VM_DEAD     [61]\n");
    console_puts("  Ctrl-] / vmkill 2    -> 处决壳 VM+宿主完整     [62]\n");
}

static void cmd_version(void)
{
    console_puts(ESHINE_BANNER "\n");
    console_puts("  hypervisor:  " ESHINE_VERSION_STR "\n");
    console_puts("  rtos:        eshine-rtos on VM0 (bitmap+tick+sem/mutex+PI+per-task FP)\n");
    console_puts("  smp:         2 pCPUs (CPU0: host+vm0+vm1; CPU1: vm2 via PSCI cpu_on)\n");
    console_puts("  guest-linux: Linux 6.6 LTS + busybox PID 1 userland (slot 2)\n");
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

            /* 站十五:逃生字符 0x1d(Ctrl-])= 宿主夺回路由(垫 Xen/ssh
             * 惯例)。owner 改显式接管后(泵撤 TIMER 自动归还),被
             * 占控台的唯一解:裸机客有 host(HVC#4)自报归还,真 Linux
             * 没有——没有它,壳用户永远进不去(owner 总被 tick 睡眠
             * 收走);有了它,卡死的 owned 客也夺不宿主的面。 */
            if (sched_console_owner && c == 0x1d) {
                struct eshine_vm *vm = vm_of_vcpu(sched_console_owner);

                vuart_tx_flush(&vm->vu, (u32)vm->vmid);
                sched_console_owner = NULL;
                console_puts("console -> host (escape)\n");
                continue;
            }
            if (sched_console_owner) {
                struct vcpu *v = sched_console_owner;
                struct eshine_vm *vm = vm_of_vcpu(v);

                /* 前台 guest 的输入入其 vuart 环(RIS 置位);RXIM 使能
                 * 时泵入口见 irq_ready 即注 LR1,无需显式标志。
                 * RX 等待者即刻直通唤醒。 */
                vuart_rx_push(&vm->vu, c);
                if (v->wait_state == 1UL && v->wait_reason == VCPU_WAIT_RX)
                    v->wait_state = 0UL;    /* 直通 runnable:注入即取 */
                if (vm->vmid == 2)
                    sched_poke_cpu1();  /* 站十八D:RX 直通 CPU1 专泵 */
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
