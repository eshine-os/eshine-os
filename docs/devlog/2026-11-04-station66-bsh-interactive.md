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

## 2. 判据与交付

- **交付面**:bsh 无参=直进交互 ash(用户可试;exit 回 rtos>);
  **场景/门禁全走确定性 sh -c 面**(station65-bsh.json 11/11 PASS;
  门 [183]=bsh echo+变量展开)——非确定交互不做门禁判据(挂脸族
  污染,判据不可重复)
- **交互可靠化=58 族专场前置**:boot 挂脸族(58 引入)不定谳,一切
  非确定竞态探针都不可重复;建议下一站=58 族专场取证(58 时代二分
  基线已有:57 态 5/5 净 vs 58 态 1/5 挂)
- make 零警告;help [183] 行更新(bsh [cmd]);ARCHITECTURE 站 66 段
