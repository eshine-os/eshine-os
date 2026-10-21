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
                       VM_DEAD

- 表驱动 `allowed[current][next]`，表外转换一律 panic（默认拒绝）
- `struct eshine_vm` 属主清晰：vcpus[] / mem[] / devices[]
- teardown 铁律：设备 -> 内存 -> vCPU（创建序逆序；后续阶段实现）

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

## Stage-2 IPA 图（首个 guest 地址空间，27.04 第一站）

    L1 pgd (VTCR: 4KB/T0SZ=32/SL0=L1/PS=40bit)
      pgd[0]  IPA 0x00000000-0x3FFFFFFF -> L2 设备表
        l2_dev[0x48] -> L3 页表
          l3_uart[0]   IPA 0x09000000 -> PA 0x09000000  nGnRE + XN（PL011）
      pgd[1]  IPA 0x40000000-0x7FFFFFFF -> L2 block（非恒等映射）
        l2_ram[0]     IPA 0x40000000   -> PA 0x50000000  Normal WB（guest 镜像/内存）

- guest ELF 链接在 IPA 0x40000000，QEMU loader 实际载入 PA 0x50000000
- 描述符铁律：AF（bit10）必置位，否则首次访问即 Access Flag fault
  （实测 ESR 0x8200000A 指令中止）；RAM MemAttr=0xF，设备=0x1（S1 关闭时）
- SCTLR_EL1.M=0：guest 走纯 stage-2，VA 即 IPA，无 EL1 stage-1

## 超调用 ABI（HVC，当前两个调用号）

| imm | 语义 | x0 | 其余寄存器 |
|---|---|---|---|
| 0   | putc：guest 控制台 | 入：字符；出：0 | x1-x17 易失（EL2 可自由使用），x18-x28 由 EL2 保存恢复 |
| 1   | exit：guest 主动退出 | - | 同上；noreturn，宿主接手打印 VM_DEAD |

guest 内联 asm 必须声明 x1-x17 clobber，否则编译器会把活跃值放进去
被 EL2 handler 覆盖。x18 平台保留位不被 EL2 分配，可跨超调用存活。

## 路线注记（范围外，记录归属）

- 真板阶段：EL3/ATF 启动链（QEMU virt 无 secure world，本周用 virtualization=on 直进 EL2）
- GICv3 虚拟化（ICH_*）、vCPU 调度、VM0=RTOS 管理 VM / VM1=Linux guest
- 与参考实现的结构对齐约定见 docs/baseline-deviations.md

## 验证

真绿门禁：`python3 tools/verify/verify_el2.py`（8 步：banner + prompt +
宿主 token 双回显 + HVC banner + 直访 UART banner + guest token 双回显 +
VM_DEAD + 宿主完整性复验，静态 marker 不算）。CI 在 .github/workflows/ci.yml，
同步 GitHub 后生效。
