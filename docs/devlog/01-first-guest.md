# Devlog 01 — 第一个 guest 地址空间（27.04 第一站）

日期：2026-10-21/22 · 版本：27.04-dev · 门禁：8 步真绿 ×8 连过

## 里程碑

QEMU virt 直启 EL2 的 hypervisor 里，第一次跑通完整虚拟化往返：

    eshine> run_guest
    run_guest: entering EL1 via stage-2 (non-identity map)
    eshine-guest @EL1 via stage-2 (HVC console)
    eshine-guest UART @IPA 0x09000000 (stage-2 device page)
    guest> echo GOK<pid>
    GOK<pid>
    guest> exit
    run_guest: VM_DEAD, back at EL2
    eshine>                            <- 宿主 shell 无损存活

guest 的每一条路径都被真硬件级验证过：非恒等 stage-2 block
（IPA 0x40000000 -> PA 0x50000000）、HVC 超调用控制台、stage-2 设备
页直访 PL011（nGnRE + XN，不经超调用）、HVC #1 主动退出后宿主
C 世界完好。两条 IO 路径都在 guest 自己的 shell 里被交互验证。

## 根因一：SP bank（gdb 实证，非推理）

症状：banner 全打印、exit HVC 到达，但 vcpu_return_host 崩在
`ret x17`（ESR 0x02000000）。静态链路每一环都验证正确，运行时
却矛盾——不再脑内模拟，gdb 单步拿到铁证：

    A vcpu_run 入口:  sp=0x4020ce60（宿主活在 SP_EL2！）
    B msr spsel,#0 后: sp=0          （SP_EL0 从未初始化）
    C eret_enter:     x16=0x20 x17=0（ldp 从 PA 0 读 flash 零页）
    H ret x17 之后:   pc=0           （执行 QEMU bootloader）

QEMU 直启 EL2 复位 SPSel=1，`_start` 的 `mov sp` 一直写的是
SP_EL2，SP_EL0 从未初始化。vcpu_run 里的 excp-stack 舞步把活栈
覆盖后切到空 bank，ERET 前宿主帧全为 flash 垃圾。guest 却"正常"，
因为 ELR/SPSR 都在舞步之前设好——这正是它藏了三天的原因。

修复即归一 bank 世界：`_start` 置 SPSel=0（宿主 C -> SP_EL0），
`vcpu_init()` boot 一次装 SP_EL2=excp_stack（异常进入硬件强制
SPSel=1，任意时刻都有真栈），vcpu_run 删舞步。

教训：**寄存器 bank 的"默认值"在固件/仿真器里没有标准答案，
boot 代码必须显式归一。**

## 根因二：门禁 flaky 追出来的 FR 偏移错误

B 相门禁稳定，C 相加入 guest 交互后 [6/8] 约 25% 概率失败：
`guest token seen 0`，dump 显示 `GOK1882187` 被回显成
`GOOK18822187`——个别字节被投递了两次。trace 一开就躲
（Heisenbug），最后从 dump 本身破案：

guest miniuart.h 把 FR 写成了 0x14——PL011 的 FR 在 0x18，0x14
未实现、QEMU 读恒 0，于是 `while (FR & RXFE)` 永不等待，
uart_getc 变成对 DR 的百万次/秒热自旋，与 QEMU 主线程的
chardev FIFO push 在 read_pos/write_pos 上赛跑。空读返 0 被
`c >= 0x20` 过滤掉，所以"碰巧能用"——直到竞态偶发重复/丢字节。

修复一行（0x14 -> 0x18）后 8 连跑全过。

教训：**"没在等"的轮询不是慢，是竞态制造机；寄存器偏移抄手册
不抄记忆。**

## 下一步

- 多 vCPU / VM 抽象（struct eshine_vm 的 vcpus[]/mem[]/devices[] 落实）
- GICv3 虚拟化（ICH_*），中断不再 WFI
- EL1 stage-1 打开后的双阶段翻译
- 27.04 路线见 docs/specs/ 的后续站点设计。
