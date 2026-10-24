<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# Devlog 14 — Linux bring-up 站（三门会师 → VFS panic）

日期:2026-10-24 · 版本:27.04-dev · 门禁:45 步真绿 ×2 连过

## 里程碑

Linux 6.6 LTS 以 **VM2 槽**跑通整机启动,终点为预期的无 rootfs VFS panic:

    [vm2] [    0.540872] virtio_blk virtio0: [vda] 128 512-byte logical blocks
    [vm2] [    1.048053]  driver: virtio_blk
    [vm2] [    1.050670] Kernel panic - not syncing: VFS: Unable to mount root fs
    [vm2] [    1.060746] ---[ end Kernel panic ...]---

三门地基(DTB 注入/vGIC/PL011)会师成功;内核时间 1.06s 走完整个
启动流程,`virtio_blk` 以 root 候选身份参与挂载,jiffies 正常走时,
宿主全程存活。**第十一到第十三站的欠账全部兑现。**

## 形态

- **几何**:槽 2 = 128MB @ PA 0x58000000(guest 视角 IPA 0x40000000,
  stage-2 非恒等映射),入口 IPA 0x40080000(Image+text_offset),
  x0=0x40F00000(DTB 指针,arm64 启动协议)。Image 外置构建
  (scripts/build-linux.sh 配方,GPL 隔离不入仓)。
- **树**:per-VM `eshine-linux.dts`(站 11 欠账兑现):128MB memory、
  cpus、timer(interrupt-names="virt" 稀疏接线)、virtio_mmio、pl011、
  chosen("console=ttyAMA0 earlycon")。诊断期 bootargs
  (initcall_debug/loglevel=8)收口时裁净。
- **门禁 37→45**(纯追加):[38] stage-2 进入 + DTB x0;[39] 诚实树
  "Machine model: eshine-os,vm";[40] arch_timer virt 62.50MHz +
  clocksource(tick 真活);[41] Linux gic-v3 在 vGICD/vGICR 上;
  [42] ttyAMA0 全驱动交接;[43] vda(完成中断链);[44] VFS panic
  端点;[45] 僵尸处决 + 宿主完整。三处同步:verify/help/ARCHITECTURE。

## 战况:五重根因

本站没有一步到位的路——每一层"看起来通了"下面都埋着下一层。
按发现序记录,取证的手段与教训同样重要。

### ① SP_EL0 归还(current 语义,本站最深的一刀)

**症状**:内核推进到 0.57-0.65s 后无声死——无 panic、无 oops、无宿主
崩溃,停靠 PC `0xffff8000810979f4`。连续多次运行死点随机漂移
(zone init/tty init/console handover 都停过)。

**取证**:`-d int` 抓到 **600 万次/90 秒**的 `ESR 0x25/0x96000004`
EL1→EL1 data abort 自陷风暴;停帧 FAR=`0x40255c48+44` 落在**宿主
自身 .stack 区**;把停帧 PC 按链接态解析(内核 slide +0x80000,
"Kernel image misaligned"的补偿,非 KASLR——无 seed 时 KASLR
disabled,slide 只能是 2MB 对齐,0x80080 不合法)=
`enter_from_kernel_mode` 读 `current`。

**根因**:宿主 C 世界跑在 **SP_EL0** 上(启动 `msr spsel,#0` +
`vcpu_return_host` 同律)。SP_EL0 全 EL 共享;arm64 内核在 EL1
以 SP_EL0 存 `current`(init_cpu_task 写入)。**每次 eret 进
guest,SP_EL0 带着宿主 .stack 指针**——guest 第一次碰 current 即
解引用宿主栈地址:level-0 翻译错 → el1_da → enter_from_kernel_mode
又读 current → 再错 → 向量入口自陷风暴。oops 之所以无声:printk
自身也要走 current。

**修复**:宿主世界整体迁 **SP_EL2**(start.S spsel=1;vcpu_return_host
撤切换),SP_EL0 从此归 guest;陷阱帧扩 **sp_el0@416**
(el1_trap/el2_trap 转写存,trap_resume 还,vm_create 清零)——
真 hypervisor 的标准 vCPU 现场分。裸机站十三步无感的原因:自制
guest 从不在 EL1 读 SP_EL0。

### ② vtimer 注入不越权(轮询式 handler 哲学)

**症状**:SP 修复后推进到 0.658s 仍停——console 交接后 initcall
静默,vmstat 显示 preempts=0、jiffies 停在 INITIAL(−300Hz)。
内核靠 sched_clock(CNTVCT 直读)打点,整个启动**一拍 tick 都没
消费过**。

**取证**:`__log_buf` 整环 peek dump(无声 oops 全躺在环里!)+
QEMU `-trace gicv3_*`:全 boot **恰一次** LR0 写入、恰一次 guest
IAR(27)+EOI(27),再无下文——第一发 tick 被消费后不再有第二发。

**根因**:Linux `arch_timer_handler_virt` 是**轮询式**——读 CNTV
ISTATUS 才认账、自 IMASK 落线、自重装下一拍。而我们
el2_irq_guest 的 27 分支沿用了睡客路径的 `vgic_wake_waiter`
(**把 CNTV 关零**防电平重挂风暴)+ ctx 熄火——guest handler
读到 ISTATUS=0 → **IRQ_NONE → tick 静默丢弃**,无重装,
时序世界从此停摆,一切 msleep 永睡。

**修复**:跑动路径禁碰 CNTV(guest 的 handler 自会落线+重装)、
ctx 不熄(泵切出保存的才是真相);保留 EOI-drop(claim 抬起的
物理 RPR 要还);**LR 优先级镜像 guest 自配 IPRIORITYR**
(vgic_ppi_prio)——硬 0x00 会落进 Linux pseudo-NMI 阈值把
tick 走成 NMI 路径(Linux 的 gic-v3 用 VEOIM=1 拆分 EOI/DIR,
撤活走 ICV_DIR/erratum-poke 而非默认联动)。关线+熄火只属于
睡客路径(el2_irq_host:WAIT 中没人替它落线),两路分治。
修复后 jiffies +6393、preempts 30、boot 直达端点。

### ③ GICD 家族译码规范对齐(word1 才是 SPI)

**症状**:virtio 节点回树后,vda 注册成功但**分区扫描永等完成中断**:
0 次 LR3 注入,guest 活在正常 idle 循环(醒-取 tick-睡)。

**取证**:gdb-multiarch 挂活机读宿主 .bss(eshine.elf 带 -g):
`int_pend=1, last_avail=1`(设备侧一切正常)、
**`dist_isenabler[0]=0`**——guest 从未在仿真 distributor 里使能过
任何 SPI(连 UART 33 也没有)。

**根因**:Linux gic-v3 按规范写 **GICD_ISENABLER1(0x104)**(word1
= SPI 32-63);站 12 译码把家族 word0(0x100)自造约定为
"覆盖 SPI 32-63"(裸机 guest 同约定),word1 落
`n >= VGIC_SPI_WORDS` **静默丢弃**。

**修复**:译码规范对齐——word0 = 银行视图(SGI/PPI 属当前 CPU
重分发器面,guest 不应从 GICD 动,RAZ/WI 诚实拒绝),word1 起 =
SPI 面;guest/gic.h 的 SPI 偏移同步 +4(IGROUPR1/ISENABLER1)。
裸机回读语义不变,门禁 [14] 依旧真绿。

### ④ 僵尸处决权(ARM 无 EL2 级不可屏蔽抢占)

**症状**:门禁 [45] 的宿主复验在 Linux panic 后冻死:token 只回显
11 字符,preempts 停涨。

**根因**:内核 panic = `local_irq_disable` + 死循环;我们的 1 秒量子
全靠 PPI26 物理投递,**PSTATE.I 把 EL2 路由的 IRQ 也挡住**
(QEMU 实测同律)——关尽 DAIF 的死循环客无法被 GIC 抢占,宿主
shell 永冻。

**修复**:管理面绝对权威——`vmkill` 从"CREATED/PAUSED only"
扩权 RUNNING(宿主处决僵尸客的唯一解;无异步问题:shell 命令
即原子段,执行时泵必不在跑;console owner 随杀回收)。死客仍拒绝
(无事可做,防误报)。这层限制记入架构事实:单 pCPU ARM 上,
不可屏蔽的 EL2 类抢占需要 Group-0/FIQ tick 一类的真改造,
留后续站。

### ⑤ 会战前修的账(本 session 前段,一并入账)

- **preempt 出口 SP 棘轮**(el2_trap_guest 转写后不还 SP):裸机站
  tick 节奏无感,Linux 全程 RUNNING,16 秒碾穿 4KB excp_stack 踩进
  .data,宿主崩于 EL2——先于本战报修复。
- **HCR.AMO + el1_serror**:物理 SError 路由 EL2 隔离面(调查期
  作为疑似根因布设,事后证明风暴另有真凶——保留为正确性改进,
  诚实记案)。

## 战法记事

- **QEMU -d int**:异常风暴的全程录像(600 万行/90 秒),ESR/FAR/
  向量去向一屏尽收;阶段转折点=风暴第一现场。
- **QEMU -trace gicv3_\\\***:IAR/EOI/DIR/LR-write 生命周期级取证
  ——"恰一次"这类定量结论只有 trace 能给。
- **`__log_buf` peek dump**:无声死的内核,oom 的文本全在环里;
  vmlinux 常量区运行时定位=kimage 偏移直算(slide 0x80000 由内核
  自报"Kernel image misaligned"佐证)。
- **gdb-multiarch 挂活机**:.bss 常量直读,配合 eshine.elf 的 -g
  调试信息一步到位(仿真设备/vGIC 状态一眼定谳)。
- **QEMU pl011 RX FIFO 16 字节**:长命令必须分块发送;guest 在跑时
  宿主排空慢,一次性 sendall 丢尾(token 回显实测)。verify 的
  token_echo 已分块化。
- peek/十六进制参数的坑:自写的 xparse 逢空格即败、十进制"1024"
  被按十六进制解析——**诊断工具也要过自己的门**。

## 残余与后续

- sp_el0 已入帧,SIMD/FP 现场仍未逐 VM 保存(Linux 唯一 FP 用户,
  当前单 Linux 槽下无碍,多 VM 并跑前须补)。
- 关尽 DAIF 的死循环客不可抢占——Group-0/FIQ tick 改造记 backlog。
- ICFGR SPI 面(word1 同律)未仿真:Linux 电平型 SPI 不写它,
  暂无消费者(YAGNI)。
- 拓扑仍单核;SMP/PSCI 在树里诚实缺席(不说谎)。
