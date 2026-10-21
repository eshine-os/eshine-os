<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# eshine-os Architecture（Week-1 骨架）

## 启动流（当前）

    QEMU virt, virtualization=on, gic-version=3
      -kernel eshine.elf (ELF 入口 0x40200000)
        |
        v  EL2
    _start (hypervisor/arch/arm64/start.S)
      校验 CurrentEL == EL2，否则 WFI 停车
      -> DAIF 关中断（本周轮询模型）
      -> 清 BSS
      -> 装载 VBAR_EL2（.vectors，2KB 对齐）
      -> 设栈（.stack 16KB）
      -> main() -> console_init -> banner -> shell_run

## VM 状态机（生命周期先行，spec §4.1）

    VM_INIT -> VM_CREATED -> VM_RUNNING <-> VM_PAUSED
                  |             |
                  +-> VM_TEARDOWN <-+
                          |
                       VM_DEAD -> VM_INIT（27.04 第二站：槽位复用）

- 表驱动 `allowed[current][next]`，表外转换一律 panic（默认拒绝）
- 第二站新增两条：CREATED→TEARDOWN（vmkill 未跑先销毁）、DEAD→INIT（复用）
- 命令层用户错误礼貌拒绝打消息；`vm_transition` 保持 panic 护栏

## VM 抽象（27.04 第二站：双 VM + 生命周期管理）

    vm_table[2] 静态全量内嵌（无堆）：
      struct eshine_vm = state + vmid + vcpus[1] + mem[4] + devices[4] + vm_space
    预定义配置（vm_configs 思路固化于 vm.c）：
      VM0: RAM IPA 0x40000000/16MB -> PA 0x50000000   entry 0x40000000
      VM1: RAM IPA 0x40000000/16MB -> PA 0x51000000   entry 0x40000000
      共享设备页：UART IPA 0x09000000 -> PA 0x09000000（顺序运行，无争用）
    PA 窗口不重叠 = 隔离是映射层的物理事实（VM0 无法经自身映射摸到 VM1）

命令面：`vmls` / `vmcreate <id>` / `vmrun <id>` / `vmkill <id>`（单数字 id）。

## Stage-2（per-VM，27.04 第二站重构）

    struct vm_space：每表独立 4KB 对齐页（DSC_TABLE 掩码砍低 12 位，
    任何表不落 4KB 边界都会被指错 — gdb 实证后固化）
    stage2_build(sp, vmid, mem[], n, dev[], n)：
      RAM 区 -> l2_ram 2MB block 8 项（16MB）
      设备页 -> l2_dev[0x48] -> l3_dev 4KB page，强制 XN
      vttbr = pgd | vmid<<48（VMID 8-bit）
    VTCR/描述符规则与第一站一致：AF bit10 铁律、RAM=0xF、设备=0x1+XN
    拓扑不变：pgd[0]->设备段 L2、pgd[1]->RAM 段 L2；boot 单套静态表已删

## vCPU 出入口（27.04 第二站补完）

    host_ctx = x19-x28 + sp + lr（整块宿主现场）
    ERET 后 guest 覆盖一切可覆盖寄存器 — ret 回宿主前必须全量还回，
    否则调用方 callee-saved 变成 EL2 异常链残留（实测 vmrun 曾把 id
    打印成 HVC 的 ESR 值）
    guest 侧 ABI 现状：x18-x28 靠 EL2 handler 不触碰而存活 — 编译器
    巧合，完整 vCPU 上下文切换是下一站的债（devlog 02 记录）

## 异常级分工（当前）

| 异常 | 处理 | 状态 |
|---|---|---|
| current-EL sync | 打印 ESR_EL2/FAR_EL2 -> WFI | 已实 |
| current-EL irq  | WFI（无 GIC 配置，不应发生） | 已实 |
| lower-EL sync   | el1_sync：HVC #0/#1 分发，非 HVC 默认拒绝 panic | 已实 |
| fiq / serror / 其余 lower-EL 槽 | 默认拒绝 panic + ESR | 占位 |

## SP bank 归一（27.04，gdb 实证后固化）

    _start: msr spsel,#0        宿主 C 世界 -> SP_EL0
    vcpu_init(): SP_EL2 = excp_stack   异常进入专用栈
    异常入 EL2: 硬件强制 SPSel=1 -> SP_EL2（任意时刻都有真栈）
    guest@EL1h: SPSel=1 -> SP_EL1（与宿主 bank 无交集）

QEMU 直启 EL2 复位 SPSel=1，宿主曾整体跑在 SP_EL2 上：vcpu_run 的
excp-stack 舞步会覆盖活栈、切到从未初始化的 SP_EL0=0，导致 ERET 前
宿主帧全为 flash 垃圾（host_ctx={0x20,0}，ret x17 落在 PA 0）。

## Stage-2 IPA 布局（两 VM 同构，仅 RAM 输出物理不同）

    VTCR: 4KB/T0SZ=32/SL0=L1/PS=40bit；SCTLR_EL1.M=0（纯 stage-2，VA 即 IPA）
    guest ELF 均链接在 IPA 0x40000000，QEMU loader 载入各自 PA 窗口
    描述符铁律：AF（bit10）必置位（实测 ESR 0x8200000A 指令中止）；
    RAM MemAttr=0xF，设备=0x1（S1 关闭时），设备页强制 XN

## 超调用 ABI（HVC，当前两个调用号）

| imm | 语义 | x0 | 其余寄存器 |
|---|---|---|---|
| 0   | putc：guest 控制台 | 入：字符；出：0 | x1-x17 易失（EL2 可自由使用）；x18-x28 现靠 EL2 handler 不触碰存活（编译器巧合，完整上下文切换为后续债） |
| 1   | exit：guest 主动退出 | - | 同上；noreturn，宿主接手打印 VM_DEAD |

guest 内联 asm 必须声明 x1-x17 clobber，否则编译器会把活跃值放进去
被 EL2 handler 覆盖。x18 平台保留位不被 EL2 分配，可跨超调用存活。

## 路线注记（范围外，记录归属）

- 真板阶段：EL3/ATF 启动链（QEMU virt 无 secure world，本周用 virtualization=on 直进 EL2）
- GICv3 虚拟化（ICH_*）、vCPU 调度、完整 vCPU 上下文切换（guest 寄存器态）、
  分配器（vm_space/vcpu 由内嵌换动态）、VM0=RTOS 管理 VM / VM1=Linux guest
- 与参考实现的结构对齐约定见 docs/baseline-deviations.md

## 验证

真绿门禁：`python3 tools/verify/verify_el2.py`（14 步：banner + prompt +
宿主 token 双回显 + 空表 vmls + vmcreate + VM0/VM1 各自完整往返
（HVC banner/直访 UART banner/guest token 双回显/VM_DEAD）+ 全 DEAD 表 +
宿主完整性复验 + vmkill 拒绝路径，静态 marker 不算）。CI 在
.github/workflows/ci.yml，同步 GitHub 后生效。
