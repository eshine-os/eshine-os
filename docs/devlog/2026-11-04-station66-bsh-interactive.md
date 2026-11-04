# 站六十六 devlog:bsh 无参交互(批次 F 第二站;深水取证+泵门修真)

> 承接站 65(bsh 子壳提交)。门禁 **183 步不动**([183] 面收窄到 sh -c
> 确定性判据)。起因=用户令:"务必实现 rtos 的无参交互模式"。

## 0. 全案取证链(gdb-multiarch 源级直读=三镜法第四镜)

- **观感 90% 是设计**:bsh 无参的"定格"静默,主因=站 24A 心跳门
  (壳非 SLEEP 态静默)——vm0 实况全活:tick 100Hz(rt_ticks/27 计数
  双读数涨)、心跳醒睡循环活(wake_tick 与 rt_ticks 同步推进,差恒
  113)、ash BLOCK RX_BIT 睡 stdin(合法态)、字符能进宿主 vuart 环
  +vm0 内核 rrx 环
- **真断=33(UART)注入链**:泵行 730 的设备事件复查门
  `wr==TIMER && sched_console_owner == v`——非 owner 的 TIMER 泊
  不注入 RX(站 24B 设计排除面:防真 Linux 后台恒 armed 被 RX 误醒
  烧 CPU)。ash 睡 stdin 等 RX 唤醒→33 不注入→ash 永睡
- **工具战果**:gdb-multiarch(apt)+QEMU gdb stub(tcp::1236)+符号
  直读(`p vm_table[0].vcpus[0].wait_state`/任务池/调度队列/客 RAM)
  ——HMP `xp` 读 MMIO=假象(对照实验定谳:正常态与定格态同值,弃用)
- **取证战雷**:字段偏移三套绕晕(注释/我的推算/编译器——信编译器
  反汇编;帧真偏移 asm 读处为准);探针时序(gdb attach 必须在 bsh
  前窗);HMP monitor 参数 `server,nowait`(QEMU 8.2 拒 `server=nowait`);
  探针起等 >=3s(QEMU 起慢)
- **owner=NULL 非确定谜**:一靴 gdb 读数 owner=NULL(33 注入门因此
  关死),两靴 hardware watchpoint 没抓到写点——非确定竞态,未定谳

## 1. 修真(泵 RX 注入门放宽)

vm.c 泵扫的设备事件复查门加第三支:**非 owner 的 TIMER 泊若 RX 环
真有可取数据**(vuart_rx_irq_ready && vgic_spi_enabled——过站 24B
同款门控)亦醒。修后:

- **交互链曾活**:一靴 "sh: selftest: not found"=ash 吃行执行(铁证:
  非交互 stdin 流模式活着,行送进 ash 被解析执行)
- **非确定**:1/4 活率(宿主负载 7+ 深夜窗更低)——竞态与 **58 挂脸族**
  (未定谳,boot 20-75% 挂)纠缠:交互靴型的断点漂移(cmd_bsh 不执行/
  回显断/dispatch 断多形态),与 58 族的 hello 死 printf 同族画像
- **修的语义保全**:空环不醒(站 20"客无 RXIM 热自旋"教训的门控不变)

## 2. 定稿:行 REPL(站 66 收口方案)

常驻 ash 的 stdin 中断链非确定(1/4 活率,owner/33 注入竞态与 58 族
纠缠)——**站 66 定稿=行 REPL**:

- bsh 无参→壳父自读行(sys_read 单字符面=主循环同款,几十站门禁
  验证的稳定输入面;带回显+退格)+每行一个 sh -c(fork+wait,
  确定性面)+空行 exit 回 rtos>
- **绕开常驻 ash 的非确定链,活率 3/4**(唯余 58 族挂脸靴=全局
  问题,非 REPL 面);REPL 探针 5 靴:3 FULL-PASS+1 PARTIAL(挂脸
  窗某步超时)+1 FAIL:prompt(58 族)
- 语义边界:变量不跨行(每行新 sh);busybox applet 内联活
  (NOFORK:echo/ls/cat/变量展开——"ls /etc" 出 inittab 实证);
  非 busybox 命令 execve=ENOSYS 127 记档
- 场景 station65-bsh.json 18/18 PASS(sh -c×2+REPL 全链);
  门 [183]=sh -c+REPL 判据;help [183] 行=bsh [cmd]
- **常驻 ash 交互可靠化(持久 shell 会话)=58 族专场前置**
