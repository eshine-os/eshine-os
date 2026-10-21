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
| fiq / serror / lower-EL 12 槽 | 默认拒绝 panic + ESR | 占位 |

## 路线注记（范围外，记录归属）

- 真板阶段：EL3/ATF 启动链（QEMU virt 无 secure world，本周用 virtualization=on 直进 EL2）
- stage2 页表 + VTCR、GICv3 虚拟化（ICH_*）、vCPU 调度、VM0=RTOS 管理 VM / VM1=Linux guest
- 与参考实现的结构对齐约定见 docs/baseline-deviations.md

## 验证

真绿门禁：`python3 tools/verify/verify_el2.py`（banner + prompt + token 双次回显，
静态 marker 不算）。CI 在 .github/workflows/ci.yml，同步 GitHub 后生效。
