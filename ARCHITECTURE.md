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

## vCPU 出入口（27.04 第二站补完，第三站并入陷阱帧节）

    host_ctx = x19-x28 + sp + lr（整块宿主现场）
    ERET 后 guest 覆盖一切可覆盖寄存器 — ret 回宿主前必须全量还回，
    否则调用方 callee-saved 变成 EL2 异常链残留（实测 vmrun 曾把 id
    打印成 HVC 的 ESR 值）
    第三站起 guest 侧现场由陷阱帧全量保存恢复 — 第二站记账的
    "编译器巧合"债务已清偿（见下节与 devlog 03）

## vCPU 陷阱帧（27.04 第三站：帧即真相）

    struct vcpu = gprs[31](x0-x30) + sp + pc + pstate   ← 陷阱帧（asm 契约偏移）
                 + host_ctx(x19-x28 + sp + lr)          ← 宿主现场
                 + exit_reason(EXIT/YIELD/FAULT)
    el1_trap:    向量槽入口,停 x0 于 excp_stack → 全 GPR+SP_EL1/ELR/SPSR 入帧
                 → SP_EL2 归位栈顶 → el1_sync(x0=vcpu)
    trap_resume: 帧加载尾部(x0 最后回填)→ eret
    vcpu_enter_guest: 存 host_ctx → 复用帧加载 → eret
    vcpu_return_host: 还 host_ctx → ret 回 vcpu_run 调用点

- 新跑 = vm_create 初始化帧；yield 后帧 = 断点；续跑同一条进入路径
- **EL2 handler 从此可自由用任何寄存器** — 第二站"编译器巧合"债清零
- SP_EL2 每次陷阱入口归位栈顶：handler 链不退栈、eret 不还栈，
  不归位则每次净泄一截、累计碾穿栈底（实测第 79 发陷阱踩 cur_vcpu）
- guest 侧 selftest：sum 跨 100 次 HVC 存活（x19/x30 实证落点），
  帧破则 4950 校验红

## 异常级分工（当前）

| 异常 | 处理 | 状态 |
|---|---|---|
| current-EL sync | 打印 ESR_EL2/FAR_EL2 -> WFI（宿主自身异常仍 panic，默认拒绝） | 已实 |
| current-EL irq  | WFI（无 GIC 配置，不应发生） | 已实 |
| lower-EL sync   | el1_trap 保存帧 → el1_sync：HVC #0/#1/#2 分发；非 HVC = guest fault → 帧诊断 + VM 隔离死 | 已实 |
| fiq / serror / 其余 lower-EL 槽 | 默认拒绝 panic + ESR | 占位 |

## Guest fault 隔离（27.04 第三站）

非 HVC 异常 → `fault: ESR/FAR/pc/sp/x0-x3`（读自陷阱帧，展示野访问
现场）→ vcpu_exit retire → vm_run `faulted -> VM_DEAD`。**坏 guest 杀不死
hypervisor**；宿主自身异常仍是 panic+WFI（边界反转之处）。

## SP bank 归一（27.04，gdb 实证后固化）

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

## 超调用 ABI（HVC，三个调用号）

| imm | 语义 | x0 | 其余寄存器 |
|---|---|---|---|
| 0   | putc：guest 控制台 | 入：字符；出：0 | x1-x17 易失（EL2 可自由使用）；x18-x30 由陷阱帧全量保存恢复（第三站兑现） |
| 1   | exit：guest 主动退出 → VM 层 TEARDOWN→DEAD | - | 同上 |
| 2   | yield：主动让出 → VM 层 RUNNING→PAUSED，返回点即续跑断点 | 入 0/出 0 | 同上；返回 = 从断点继续 |

guest 内联 asm 必须声明 x1-x17 clobber；跨超调用的活跃值落 x18-x30
（selftest 实证 x19/x30），由帧保存恢复。

## 路线注记（范围外，记录归属）

- 真板阶段：EL3/ATF 启动链（QEMU virt 无 secure world，本周用 virtualization=on 直进 EL2）
- GICv3 虚拟化（ICH_*）、vCPU 调度（多 vCPU/抢占，yield 语义升级为
  调度点）、分配器（vm_space/vcpu 由内嵌换动态）、VM0=RTOS 管理 VM /
  VM1=Linux guest
- 与参考实现的结构对齐约定见 docs/baseline-deviations.md

## 验证

真绿门禁：`python3 tools/verify/verify_el2.py`（17 步：banner + prompt +
宿主 token 双回显 + 空表 + vmcreate + VM0 selftest（寄存器跨 100 HVC）+
token + yield→PAUSED + vmls + 断点续跑 + exit/VM_DEAD + VM1 banner +
fault 隔离（诊断+VM_DEAD+宿主活）+ 全 DEAD + 宿主完整性复验 +
vmkill 拒绝路径，静态 marker 不算）。CI 在 .github/workflows/ci.yml，
同步 GitHub 后生效。
