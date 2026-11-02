/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "kernel/include/console.h"
#include "kernel/include/elog.h"
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
    console_puts("          vmrun <id> vmconsole <id|host> vmstat vmrx vmtrail [N] vmkill <id>\n");
    console_puts("          logdemo loglevel <lvl|mod lvl> logmods logdump\n");
    console_puts("rtos 追加: logctrl [flush|stat|echo] logdemo [N] cat /proc/* /var/log/*\n");
    console_puts("          svcdemo(站51 清单治理域交互复跑)\n");
    console_puts("    (前台会话期 Ctrl-](0x1d)=宿主夺回控制台)\n");
    console_puts("    (会话期 Ctrl-^(0x1e) 前缀=控制台快捷键:0|1|2 切 VM,\n");
    console_puts("     h 回宿主,Ctrl-^ 双击=字面直递;未绑定键=取消照常)\n");
    console_puts("guest:    selftest sleep [N] spin dwrite dread fault echo <text> host exit\n");
    console_puts("rtos:     selftest ps spawn <n> sleep [N] fp fpdual fpstat yield semdemo\n");
    console_puts("          mutexdemo pidemo eventdemo queuedemo smpdemo ipidemo fpsmp\n");
    console_puts("          tmodemo stkdemo stackcheck overflow host exit\n");
    console_puts("make verify walkthrough (154 步;与 make run 同一构建,可手动过;\n");
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
    console_puts("  vm0 echo(提示符零刷屏)+vm1 dwrite+dread/vm1 echo [47-49]\n");
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
    console_puts("  hb 注入(站24)  -> tick 递增夹 boot 流=双客并发 [69]\n");
    console_puts("  ttyAMA0 / vda 128 blocks / initramfs / busybox 壳 [70-73]\n");
    console_puts("    (0x1d Ctrl-] = 宿主夺回;owner 不再自动归还)\n");
    console_puts("  echo TOKEN / cat /proc/cpuinfo(cpuinfo 0xd07) [75-76]\n");
    console_puts("  cat /proc/cpuinfo -> processor 1(双核用户态)  [76]\n");
    console_puts("  /bin/hello -> 同一 musl ELF 双世界右翼   [77]\n");
    console_puts("  utest               -> EL0 syscall 面(pid/NI/sysinfo)  [78]\n");
    console_puts("  forkdemo            -> COPY_UCONTEXT 全生命周期    [79]\n");
    console_puts("  mmapdemo            -> 缺页零填+rw+munmap         [80]\n");
    console_puts("  exedemo             -> EXEC_SETUP 帧改写(pc/sp/x0) [81]\n");
    console_puts("  ipcdemo             -> 三段消息+优先级传播       [82]\n");
    console_puts("  pulsedemo           -> 异步脉冲 5 连发/排空     [83]\n");
    console_puts("  abortdemo           -> 发送超时+UNBLOCK 脉冲    [84]\n");
    console_puts("  fddemo              -> fd=通道三元组四脸走服务器 [85]\n");
    console_puts("  futexdemo           -> WAIT/WAKE 四脸+真阻塞   [86]\n");
    console_puts("  sigdemo             -> kill 打断重启(ERESTARTSYS)[87]\n");
    console_puts("  run /bin/hello      -> musl 双世界左翼(用户态 loader) [88]\n");
    console_puts("  run /bin/pt         -> pthread 4 线程 join(clone/TLS/CLEARTID)[89]\n");
    console_puts("  shmdemo/msgseg      -> 共享段+固定窗+fork 共享/单拷窗 [90-91]\n");
    console_puts("  mqdemo/pipedemo/longdemo -> mqueue 优先级/管道 EOF/长消息镜像 [92-94]\n");
    console_puts("  sigqdemo            -> sigqueue 携值/restorer/默认终止 [95]\n");
    console_puts("  wrdemo/lsdemo/fblkdemo -> ramfs 写时复制/枚举/块设备 [96-98]\n");
    console_puts("  rngdemo/clockdemo   -> /dev/random 两脸+时钟真号   [99-100]\n");
    console_puts("  procdemo/slogdemo   -> /proc 任务表+slogd 环     [101-102]\n");
    console_puts("  run /bin/busybox echo/cat/ls/date/md5sum -> musl 精选集双世界 [103-106]\n");
    console_puts("  initrespawn         -> inittab sysinit 落标+门拍 respawn [105]\n");
    console_puts("  capdemo/capdemo2    -> CAPR fail-closed:VFS/SPAWN 自削拒 [107-108]\n");
    console_puts("  rlimitdemo/uiddemo  -> max_fd 自降 EMFILE+uuid 身份 [109-110]\n");
    console_puts("  capdemo(复跑)      -> 壳 CAP_ALL 回归(fail-closed 隐式) [111]\n");
    console_puts("  scheddemo/unamedemo -> 调度管理面真号+utsname [112-113]\n");
    console_puts("  pgiddemo/sigdemo2   -> 会话进程组+信号补全三脸 [114-115]\n");
    console_puts("  timerdemo/robustdemo-> posix timer+itimer+robust 链 [116-117]\n");
    console_puts("  cloexecdemo         -> F_SETFD 往返+父 fd 存活 [118]\n");
    console_puts("  calltrace           -> x29 FP 链走查+panic 钩子  [119]\n");
    console_puts("  polldemo            -> ppoll 超时/POLLIN/排空三脸 [120]\n");
    console_puts("  ps                  -> blk 归属列(挂起原语名)     [121]\n");
    console_puts("  apsdemo             -> CPU 分区 20%保证+偷闲     [122]\n");
    console_puts("  mprtdemo            -> 内存分区上限(fork 也拒)  [123]\n");
    console_puts("  parts               -> 两分区表(k_aps_show 镜像)[124]\n");
    console_puts("  crashdemo           -> 野访=进程死非全灭+取证  [125]\n");
    console_puts("  crashinfo           -> crash 环倾倒(三元+帧链)  [126]\n");
    console_puts("  xvm(客)/xvmsrv      -> 跨 VM safeshm 窗交换      [127]\n");
    console_puts("  health               -> 批四面合卷健康面板       [128]\n");
    console_puts("  ctrl-^ 0/1/2/h      -> 快捷键三台直切+回宿主   [129]\n");
    console_puts("  vmconsole 0 / exit -> rtos 迟退 VM_DEAD     [130]\n");
    console_puts("  overflow -> rt_panic 指名(vm0 复活链)       [131]\n");
    console_puts("  Ctrl-] / vmkill 2    -> 处决壳 VM+宿主完整     [132]\n");
    console_puts("  yocto bitbake -> 部署产物可靴(musl run)[133]\n");
    console_puts("  logdemo             -> 三级现身+滤除+限流面    [134]\n");
    console_puts("  loglevel elog dbg    -> 复跑 dbg/trc 现身(调级) [135]\n");
    console_puts("  logmods             -> 双模块表(段聚合)       [136]\n");
    console_puts("  logdump             -> 环倾倒+boot 行环审计    [137]\n");
    console_puts("  (vm0) logdemo       -> rtos 契约三级+滤除+限流 [138]\n");
    console_puts("  (vm0) loglevel/logmods -> 调级生效+模块表     [139]\n");
    console_puts("  (vm0) logctrl flush+cat -> klog 环收割入 /var/log [140]\n");
    console_puts("  (vm0) slogdemo+cat  -> 用户源 'S [X]' 行入卷    [141]\n");
    console_puts("  (vm0) logdemo 16+stat -> 双世代轮转(slog+.1)   [142]\n");
    console_puts("  (vm0) cat /proc/meminfo -> 页池账(前缀路由)    [143]\n");
    console_puts("  (vm0) cat /proc/cpuinfo -> 双核行              [144]\n");
    console_puts("  (vm0) cat /proc/interrupts -> 逐中断计数行     [145]\n");
    console_puts("  (vm0) svcdemo          -> 清单治理域(有序+门)  [146]\n");
    console_puts("  (vm0) svcdemo(续)       -> respawn 监控+上限   [147]\n");
    console_puts("  (vm0) svcdemo(收)       -> 治理链收口          [148]\n");
    console_puts("  (vm0) ham add/beat/ls  -> 心跳实体 HEALTHY      [149]\n");
    console_puts("  (vm0) sleep4+beat      -> MISSED->recovered   [150]\n");
    console_puts("  (vm0) cat /var/log     -> ham warn 入卷审计    [151]\n");
    console_puts("  (vm0) dbgdemo          -> debug/PMU 平台探针    [152]\n");
    console_puts("  vmdbg                 -> per-VM mdscr 槽倾倒    [153]\n");
    console_puts("  (vm1) mdscr probe      -> 第二客视图(TCG 存根)  [154]\n");
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

/* 站26:console 切换公共体——vmconsole 命令与 Ctrl-^ 快捷键共用
 * 同一函数=零行为差异(状态门/owner 落位/唤醒/消息/冲刷次序全同;
 * 被拒不夺台=当前 owner 原位)。 */
static void console_to_host(void)
{
    if (sched_console_owner) {
        struct eshine_vm *vm = vm_of_vcpu(sched_console_owner);

        vm->vu.flush_at = 0;        /* 站23C:即冲口同步熄武装(评审 c) */
        vuart_tx_flush(&vm->vu, (u32)vm->vmid);
    }
    sched_console_owner = NULL;
    console_puts("console -> host\n");
}

static void console_to_vm(u32 id)
{
    struct eshine_vm *vm = vm_of_vcpu(vm_vcpu(id));

    /* 状态门(第六站 bug 修复):只有 RUNNING 的 VM 消费输入,
     * owner 指给非 RUNNING 的 VM = 所有输入入环无人读,且
     * vmconsole host 自救同样被路由——终端永久锁死。默认拒绝,
     * 宿主命令面永远可用(vmconsole host 无条件)。 */
    if (vm->state != VM_RUNNING) {
        console_puts("vm");
        util_putdec(id);
        console_puts(": denied: not running (vmrun first)\n");
        return;
    }
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
    vm->vu.flush_at = 0;            /* 站23C:即冲口同步熄武装(评审 c) */
    vuart_tx_flush(&vm->vu, id);
}

/* 站26:控制台快捷键前缀态——Ctrl-^(0x1e)+ 0|1|2(切 vm)/h(回
 * 宿主);双击=字面 0x1e 递当前 owner。选型见站 spec:0x1d 逃生
 * 契约不动,0x1c 是 SIGQUIT 不可偷,0x1e 与 0x1d 字节相邻且无
 * TTY 信号绑定(Linux tty/readline/vi 全不用,vm0 壳 0x20-0x7e
 * 过滤器零成本),前缀键有 QEMU Ctrl-A/tmux/screen 先例。
 * 语义(QEMU 同律):未绑定键=前缀取消、本键照常处理(误触零
 * 损坏);0x1d 逃生优先级恒高(前缀态随夺回清零)。 */
static char vmkey_pending;

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
    } else if (strncmp(line, "vmtrail", 7) == 0 &&
               (line[7] == '\0' || line[7] == ' ')) {
        /* 站25C:可选 N(最后 N 条,默认 512;0=全量 16K)——
         * 全量倾倒是串口级灾难,取证只需死线附近窗。评审修真×3:
         * ①分发层原是 exact strncmp——"vmtrail 1000" 整串不等
         * 落 unknown(站二十四"vmtrail 40"同型坑的二连),改前缀
         * 匹配+词界守卫;②parse_id 只吃单字符,多位 N 静默
         * 回退 512——补多位解析;③非数字参数原静默顶替默认值
         * (dispatch 表中唯一不报 usage 者,站23 "静默语义错位"
         * 同律)——补 usage 拒绝:空参=默认,数字=解析,其余拒绝。 */
        u32 n = 512;

        if (args[0] != '\0') {
            if (args[0] >= '0' && args[0] <= '9') {
                const char *p = args;

                n = 0;
                while (*p >= '0' && *p <= '9' && n < 100000) {
                    n = n * 10 + (u32)(*p - '0');
                    p++;
                }
                vm_trail_dump(n);
            } else {
                console_puts("usage: vmtrail [N] (default 512, 0=full)\n");
            }
        } else {
            vm_trail_dump(n);
        }
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
            console_to_host();
        } else {
            bool ok;
            u32 id = parse_id(args, &ok);

            if (ok && id < ESHINE_MAX_VMS)
                console_to_vm(id);      /* 站26:公共体(快捷键同源) */
            else
                console_puts("usage: vmconsole <id|host>\n");
        }
    } else if (strncmp(line, "vmstat", 64) == 0) {
        cmd_vmstat();
    } else if (strncmp(line, "vmdbg", 64) == 0) {
        vm_dbg_dump();                /* 站53:per-VM mdscr 槽倾倒 */
    } else if (strncmp(line, "logdemo", 64) == 0) {
        elog_logdemo();
    } else if (strncmp(line, "loglevel", 64) == 0) {
        elog_loglevel_args(args);
    } else if (strncmp(line, "logmods", 64) == 0) {
        elog_logmods();
    } else if (strncmp(line, "logdump", 64) == 0) {
        elog_logdump();
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
             * 收走);有了它,卡死的 owned 客也夺不宿主的面。
             * 站26:逃生优先级恒高于快捷键前缀态(前缀态随夺回清零)。 */
            if (sched_console_owner && c == 0x1d) {
                struct eshine_vm *vm = vm_of_vcpu(sched_console_owner);

                vm->vu.flush_at = 0;    /* 站23C:即冲口同步熄武装(评审 c) */
                vuart_tx_flush(&vm->vu, (u32)vm->vmid);
                sched_console_owner = NULL;
                console_puts("console -> host (escape)\n");
                /* 站26 补:夺回即印提示符——回宿主必有 eshine> 的统一
                 * 心智模型(旧版逃生后无提示符,交互者无从判活;
                 * verify 六处 eshine> 锚均不落逃生窗后,加印零判据风险) */
                console_puts("eshine> ");
                vmkey_pending = 0;
                continue;
            }
            /* 站26:控制台快捷键前缀机(语义 QEMU Ctrl-A 同律)——
             * pending:0|1|2 切 vm,h 回宿主;其余一律清 pending 后
             * fall-through 本键照常处理(未绑定=取消;双击 0x1e=
             * 字面,落回下方 owner 推环路径,复用站 24 推环唤醒
             * 不变量全路径零复制;owner 缺席时字面无处递,按未绑定
             * 同律进宿主行编辑——误触零损坏,不吞字节)。 */
            if (vmkey_pending) {
                vmkey_pending = 0;
                if (c >= '0' && c < (char)('0' + ESHINE_MAX_VMS)) {
                    console_to_vm((u32)(c - '0'));
                    continue;
                }
                if (c == 'h') {
                    console_to_host();
                    /* 站26 补:与逃生路同律,回宿主即印提示符
                     *(用户巡壳实测报告"最后界面卡住"——逃生/h 两路
                     * 无提示符的静止屏是卡死观感的主因之一;命令路径
                     * 的提示符由 dispatch 流程补,勿入公共函数免双印) */
                    console_puts("eshine> ");
                    continue;
                }
            } else if (c == 0x1e) {
                vmkey_pending = 1;
                continue;
            }
            if (sched_console_owner) {
                struct vcpu *v = sched_console_owner;
                struct eshine_vm *vm = vm_of_vcpu(v);

                /* 前台 guest 的输入入其 vuart 环(RIS 置位);RXIM 使能
                 * 时泵入口见 irq_ready 即注 LR1,无需显式标志。
                 * 站24B:输入⇒runnable 不变量——唤醒去 reason 门
                 * (旧门只认 WAIT_RX;真 Linux 恒 vtimer armed,泊
                 * 位分类恒 TIMER,输入只能等客自家 vtimer=深睡秒级
                 * ~永不,[41]/[76] 输入蒸发家族+用户卡顿座报告定
                 * 谳)。WFI "pending 即醒"语义补全:环有数据即有
                 * 活,TIMER 泊亦醒,醒后泵入口注入 LR1 消费。 */
                vuart_rx_push(&vm->vu, c);
                /* 评审 Dekker 补:环写(锁内,release 出锁)与
                 * 停泊侧 wait_state 写皆普通存取——对侧载可先行满足
                 * 读到旧值=双侧皆漏(推环读得"未泊"不醒+泊位侧读
                 * 得"环空"不返 runnable,同拍即丢唤醒)。两侧各补
                 * dmb ish:存后读前立障,至少一侧必见对侧写。
                 * push 侧锁内 release 只序本侧先前访存,不序本侧
                 * 后续的 wait_state 读——独立补障,勿倚锁。 */
                __asm__ volatile("dmb ish");
                if (v->wait_state == 1UL)
                    v->wait_state = 0UL;
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
