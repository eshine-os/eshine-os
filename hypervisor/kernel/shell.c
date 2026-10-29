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
    console_puts("          vmrun <id> vmconsole <id|host> vmstat vmrx vmtrail vmkill <id>\n");
    console_puts("    (前台会话期 Ctrl-](0x1d)=宿主夺回控制台)\n");
    console_puts("guest:    selftest sleep [N] spin dwrite dread fault echo <text> host exit\n");
    console_puts("rtos:     selftest ps spawn <n> sleep [N] fp fpdual fpstat yield semdemo\n");
    console_puts("          mutexdemo pidemo eventdemo queuedemo smpdemo ipidemo fpsmp\n");
    console_puts("          tmodemo stkdemo stackcheck overflow host exit\n");
    console_puts("make verify walkthrough (79 步;与 make run 同一构建,可手动过;\n");
    console_puts("四核拓扑:vm0.v0/vm1 在 CPU0,vm2 双 vCPU 在 CPU1/2,vm0.v1 在 CPU3):\n");
    console_puts("  echo ESHINEOKx      -> token 双次回显        [4]\n");
    console_puts("  cpu1/2/3 online    -> psci cpu_on 三辅核起立  [3]\n");
    console_puts("  tick x2             -> ticks 递增             [5]\n");
    console_puts("  vmls / vmcreate 0   -> 空表 / CREATED         [6-7]\n");
    console_puts("  vmconsole 0         -> denied: not running    [8]\n");
    console_puts("  vmrun 0             -> [vm0] rtos banner      [9]\n");
    console_puts("  (rtos 自报)         -> fdt ok ×3 / pl011 / gicd / gic / uart irq / virtio [10-17]\n");
    console_puts("  (rtos 自报)         -> rtos: up 位图+tick     [18]\n");
    console_puts("  vmconsole 0 + 回车   -> rtos> (RX 唤醒链)      [19]\n");
    console_puts("  selftest / ps / spawn / fp / sleep 3          [20-24]\n");
    console_puts("  yield / vmrun 0     -> PAUSED -> resumed      [25]\n");
    console_puts("  host / vmstat       -> console->host; RUNNING [26]\n");
    console_puts("  semdemo             -> 环 8/8(信号量阻塞乒乓)[27]\n");
    console_puts("  mutexdemo + ps      -> waiter BLOCK 活体      [28]\n");
    console_puts("  (mutexdemo 收尾)     -> 醒序+递归+非拥有者拒绝 [29]\n");
    console_puts("  pidemo              -> PI 反超(mid 饿死 low,high boost)[30]\n");
    console_puts("  fpdual              -> 双 FP 任务双精确(帧)[31]\n");
    console_puts("  eventdemo           -> AND 双拍齐醒+OR 单拍即醒 [32]\n");
    console_puts("  queuedemo           -> 16 条环+满发阻塞        [33]\n");
    console_puts("  fpstat              -> lazy: M fp-saves << N switches [34]\n");
    console_puts("  cpu1 online(rtos psci) -> 次核自报+GIC 帧1 回读 [35]\n");
    console_puts("  smpdemo             -> 双核计数行交错(cpu1>=2)  [36]\n");
    console_puts("  ipidemo             -> SGI 往返>=20+跨核唤醒 3   [37]\n");
    console_puts("  fpsmp               -> per-CPU FP 帧双精确       [38]\n");
    console_puts("  ps / fpstat         -> 次核在线主核不回归       [39]\n");
    console_puts("  tmodemo             -> 超时全弧+PI 14->9->14    [40]\n");
    console_puts("  stkdemo/stackcheck  -> 水位收缩+magic ok+ps 列 [41]\n  swtdemo             -> 软定时器三面(单发/周期/取消)[42]\n");
    console_puts("  irqdemo            -> 注册表链式包装/计数/恢复  [43]\n");
    console_puts("  lowdemo            -> 贴线守卫恰一次 warn       [44]\n");
    console_puts("  vmcreate 1 / vmrun 1 / vmconsole 1          [45-46]\n");
    console_puts("  vm0 echo / vm1 dwrite+dread / vm1 echo     [47-49]\n");
    console_puts("  vm1 spin -> spun(preempts 差>=2)            [50]\n");
    console_puts("  vm0 sleep 公平+vm1 后台醒                    [51-52]\n");
    console_puts("  双睡醒序(rtos 2s 先于 vm1 one-shot 4s)      [53]\n");
    console_puts("  双醒后双活 echo                                [54]\n");
    console_puts("  持久化:重建后旧数据仍在                       [55]\n");
    console_puts("  fault -> vm1 隔离死,宿主无损                  [56]\n");
    console_puts("  vmls(rtos RUNNING 留活)/宿主完整/退场后 tick   [57-59]\n");
    console_puts("  vmkill 拒绝路径 / vmstat 收尾                 [60-61]\n");
    console_puts("  vmcreate 2 / vmrun 2 -> Booting Linux          [62]\n");
    console_puts("  smp: Brought up 1 node, 2 CPUs -> 第二 vCPU 上线 [63]\n");
    console_puts("  vm1 复活 spin 独占 CPU0,vm2 行进=真并行       [64]\n");
    console_puts("  echo storm 10/10  -> 延迟冲刷(vm2 靴流内)     [65]\n");
    console_puts("  Machine model / arch_timer / GICv3          [66-68]\n");
    console_puts("  前台心跳夹 boot 流  -> 双客并发时序证明        [69]\n");
    console_puts("  ttyAMA0 / vda 128 blocks / initramfs / busybox 壳 [70-73]\n");
    console_puts("    (0x1d Ctrl-] = 宿主夺回;owner 不再自动归还)\n");
    console_puts("  echo TOKEN / cat /proc/cpuinfo(cpuinfo 0xd07) [74-75]\n");
    console_puts("  cat /proc/cpuinfo -> processor 1(双核用户态)  [76]\n");
    console_puts("  vmconsole 0 / exit -> rtos 迟退 VM_DEAD     [77]\n");
    console_puts("  overflow -> rt_panic 指名(vm0 复活链)       [78]\n");
    console_puts("  Ctrl-] / vmkill 2    -> 处决壳 VM+宿主完整     [79]\n");
}

static void cmd_version(void)
{
    console_puts(ESHINE_BANNER "\n");
    console_puts("  hypervisor:  " ESHINE_VERSION_STR "\n");
    console_puts("  rtos:        eshine-rtos on VM0 (bitmap+tick+sync+per-task FP, dual-core SMP)\n");
    console_puts("  smp:         4 pCPUs (CPU0: host+vm0.v0+vm1; CPU1/2: vm2 via PSCI;\n");
    console_puts("                CPU3: vm0.v1 dedicated pump via PSCI cpu_on)\n");
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
    } else if (strncmp(line, "vmrx", 64) == 0) {
        vm_rx_dump();
    } else if (strncmp(line, "vmtrail", 64) == 0) {
        vm_trail_dump();
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

                vm->vu.flush_at = 0;    /* 站23C:即冲口同步熄武装(评审 c) */
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
                    sched_console_owner = vm_vcpu(id);
                    if (sched_console_owner->wait_state == 1UL)
                        sched_console_owner->wait_state = 0UL;
                    console_puts("console -> vm\n");
                    /* 站20:半行冲刷移到 owner 落位与切换消息之后——旧序
                     * (先冲再切)下,三核并发里新 owner 的在途半行会被
                     * "console -> vm" 消息从中间劈开(busybox 提示符
                     * "~ # " 无换行,恰跨切换即碎,门禁 [62] 锚实测)。
                     * 新序:消息先行,半行整体随冲刷(或首笔 fg 直写)在后
                     * 面世,字节序恒完整。 */
                    vm->vu.flush_at = 0;    /* 站23C:即冲口同步熄武装(评审 c) */
                    vuart_tx_flush(&vm->vu, id);
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

                vm->vu.flush_at = 0;    /* 站23C:即冲口同步熄武装(评审 c) */
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
                {
                    u64 rpr, ctl, pmr;

                    __asm__ volatile("mrs %0, icc_rpr_el1" : "=r"(rpr));
                    __asm__ volatile("mrs %0, icc_ctlr_el1" : "=r"(ctl));
                    __asm__ volatile("mrs %0, icc_pmr_el1" : "=r"(pmr));
                    trail_add(9, rpr, (ctl << 32) | (pmr << 16) | v->wait_reason);
                }
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
