<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# Devlog 06 — 设备仿真站（vuart/MMIO 陷阱：TX/RX 全仿真 + HVC IO 退役）

日期:2026-10-23 · 版本:27.04-dev · 门禁:24 步真绿 ×2 连过

## 里程碑

guest 的 UART 撤出 stage-2 映射，DR/FR 访问全部 data abort 陷阱化，
宿主 per-VM vuart 按寄存器语义仿真——**guest 驱动不改语义，宿主把
真硬件从它脚下换成了仿真设备**：

    eshine> vmrun 0         → 立即回 shell（异步）
                             → [vm0] eshine-guest VM0 @EL1 via stage-2 (vuart @trap)
                               （后台输出：陷阱 TX 入行缓冲，换行整行带前缀冲）
    eshine> vmconsole 0 → \n → vm0>   （RX 唤醒链：入环→直通 runnable→FR 命中）
    vm0> selftest             → 100 个 '.'（100 次 MMIO 陷阱，寄存器存活性判据）
    vm0> fault                → fault: ESR=0x93c18006 FAR=0x30000000
                                 HPFAR=0x300000 ...（窗口外默认拒绝，隔离死）

HVC #0（putc）/#3（getc）退役，超调用只剩控制类（#1 exit/#2 yield/
#4 host）；selftest 从"跨 100 HVC"升级为"跨 100 MMIO 陷阱"。

## 战况一：ELR 的两副面孔（本站最狠，无声陷阱环）

trap 分发器写完首测：banner 一字不出，门禁挂死在 prompt。QEMU -d int
揭底：**同一条 FR 读指令（ELR 0x40000070）被陷阱 13,748 次连续重执行**。

根因：HVC/WFI 是"已完成"语义——ELR 指向下一条指令；**同步 data abort
不退休——ELR 指向 faulting 指令本身**。仿真完成、写回寄存器之后若不
`pc += 4` 代其退休，ERET 回去就原地重执行访存——再次 fault——仿真
再跑——无限循环。每一环的仿真都"正确"，环本身就是错的。

> 教训：**同一根 ELR，HVC 给你"下一条"，abort 给你"这一条"**。设备
> 仿真没有第二条路：模拟完访存效果，必须代 retire。

## 战况二：x1 的赎金（第三站埋的暗雷，MMIO 时代引爆）

ELR 修复后 guest 仍卡在 `tbnz w1,#5` 自旋环。逐层取证：

- 帧内 gprs[1] = 0x10（MMIO 回填正确落帧）
- **活体 x1 = 0x400010e0**（gprs[0] 的值——saved x0，字符串指针）
- 帧对、活体错——watchpoint 盯帧内 gprs[1] 现形：保存 0x400010e0 →
  回填 0x10 → 再保存 0x400010e0……eret 尾每一圈把 gprs[0] 灌回 x1。

根因在 `trap_resume` 的 eret 尾舞步：x0 必须最后加载（x0 一直当 vcpu
指针用），参考实现借 x1 中转（`ldr x1,[x0]; mov x0,x1`）——**x0 的赎金
是从 x1 的账户上划走的**。三站无害：HVC 只往 x0 回值，x1 从来不是
回值寄存器。第六站 MMIO 回填任意 SRT，第一个 SRT=1 的访存（FR 读）
立刻引爆。

修法：x1 撤出成对装载，与 x0 同批最后回填；x0 改**自加载**
`ldr x0,[x0]`（地址/目标一体，免中转）。

> 教训：**帧契约是全局不变量，asm 尾舞步属于契约的一部分**；一个
> "聪明"的寄存器腾挪可以在三站之后才收到账单。

## 战况三：HPFAR 的重建公式（探针定案，不抄记忆）

HPFAR_EL2 的位域布局各家说法莫衷，第四站方法论直接上探针：

- guest fault（0x30000000）→ 诊断打点 `HPFAR=0x300000` → **HPFAR =
  IPA>>8**（QEMU 8.2.2 实测；即 (IPA>>12)<<4）
- 故页基 = `HPFAR << 8`，页内偏移 = `FAR[11:0]`（纯 stage-2 下 VA 即
  IPA；**FAR 对 S2 fault 的页地址无效、页内偏移有效——不信的是那
  半边**，第一版全信 FAR 页地址 / 只信 HPFAR 各错一次）

首版只信 HPFAR 的后果顺带实证了"窗口匹配"的必要性：页内偏移丢失使
FR 读（+0x18）全落 DR 偏移（+0）——RX 环被 TXFF 轮询误弹。窗口匹配
对了偏移也要对，二者互不代偿。

## 战况四：判据的物理迁移（门禁随实现换代）

HVC #3 退役后，门禁两处判据的物理基础消失，如实重写而非硬保：

- **[9] 前台风暴判据退役**：getc 陷阱化后前台 = FR 轮询 + WFI 让位，
  再无"HVC 风暴计入量子"的负载——preempts 到 spin 前恒 0。抢占活性
  断言归 [15] spin 前后差（锚行为不锚实现）。
- **[7]/[11] prompt 直出判据换代**：prompt 无换行、后台在 vuart 行
  缓冲里不冲刷——"等 prompt"必须先发空行触发冲刷。新增两判据补
  位：后台 banner `[vmN]` 前缀（陷阱 TX 整行冲）+ RX 唤醒链（入环→
  直通→FR 命中→prompt 重打）。22 → 24 步。

## 战况五：owner 语义精化（第五站"WAIT 即归还"过粗了）

两源等待落地时发现第五站的让位规则一刀切：**RX 等待必须持 owner**——
输入即唤醒源，归还 owner 等于判前台死刑（宿主 shell 会把输入吃掉，
guest 永不醒）；**TIMER 等待才归还**（宿主睡期间要能敲命令）。宿主
的回收出口是 guest 的 host 命令（HVC #4）——前台模式的代价被显式
写进 ABI。而 wait_reason 字段在 sched_waiter 挣到饭吃：双 VM 同等
（一 TIMER 一 RX）时，27 的 WOKE 只归 TIMER 等待者——**按原因找人，
受益者≠当值者**（第五站教训的两源版）。

## 工具学（MMIO 时代的 gdb 四连）

- **帧对 ≠ 活体对**：陷阱帧是保存区不是现场，取证必须帧值/活体寄存
  值对照读，watchpoint 盯帧槽看写入者是谁
- `-S` 无 gdb `continue` = CPU 永冻：一切"静默"先排掉假死（含
  `wait=on/off` 的连接前后输出丢弃——wait=off 下 boot 输出全丢，
  until 匹配的可能是陈旧 prompt，锚定下界纪律对驱动脚本同样生效）
- `pgrep -f` 会匹配自己的命令行（第五站坑的 -d int 版）；杀 QEMU
  按 PID 点名，先 `ps` 看清有没有别的会话遗留实例占着 1234 端口

## 数字

- master 59 → 70 commit；门禁 22 → **24 步 ×2 双绿**（另全清重建
  复验一轮）；[15] spin 实测跨 6-7 量子
- 新增：mmio 分发（ESR/HPFAR/窗口/回填/代退休）、per-VM vuart
  （RX 环 64B + TX 行缓冲 128B）、两源等待、HVC ABI 收缩为控制类
- 偏差：#12 收口（console 形态过渡→vuart 陷阱仿真）、#13 新登
  （vuart 子集/无 IRQ 注入/单设备直查）

## 已知限制（登记）

- UART IRQ 不注入（guest 轮询+WFI；SPI 留中断站）
- 单设备直查（多仿真设备后 mmio_dispatch 查设备表）
- 粘贴长行可溢 PL011 16B FIFO（排空窗=1 量子，人类敲键无碍）
- 单源等待假设（同一 guest 不得同时 armed vtimer 又等 RX）
