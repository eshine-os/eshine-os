<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 27.04 第一站设计:stage2 页表 + 第一个 guest 地址空间

- **日期**: 2026-10-22
- **状态**: 已评审通过(user 2026-10-22 批准,"按推荐推进,今晚完成")
- **前置**: 26.10(EL2 骨架/状态机/真绿门禁)

## 0. clean-room 与参考源

全部实现以 **ARM ARM(DDI 0487)** 与 **QEMU virt 文档**为源;参考实现任何代码不读
(其 stage2 虽属自有原创,亦不读——公开规范足够)。寄存器编码、描述符格式均为 ARM 公开事实。

## 1. 三相交付

| 相 | 内容 | 判据 |
|---|---|---|
| A | stage2 模块(建表/walker/s2dump) | 构建零警告 + 现有门禁绿 + dump 正确 |
| B | vCPU 进入 + EL1 guest payload + HVC 控制台(putc) | run_guest → guest banner(经 HVC)→ exit → VM_DEAD → 回 shell |
| C | stage2 映射 UART + guest 交互 + 门禁全扩展 | run_guest → guest banner → GOK token 双回显 → exit → VM_DEAD → 回提示符 |

## 2. 地址布局(刻意非恒等,证明翻译在工作)

```
guest IPA                     → host PA
0x40000000 (guest RAM 2MB block) → 0x50000000 (QEMU -device loader 放置)
0x09000000 (guest UART 4KB page) → 0x09000000 (设备恒等, nGnRE)
VTCR_EL2: 4KB 粒度, T0SZ=32, SL0=1(L1 起), PS=40bit, ORGN/IRGN=WB, SH=IS
guest 链接于 IPA 0x40000000;hypervisor 自身 @0x40200000 不与 guest PA(0x50000000)重叠
```

## 3. stage2 模块

- 静态表池(4 张表 × 512 项, BSS 16KB):pgd(L1) + l2_dev + l3_uart + l2_ram
- 描述符:block/page = PA[51:12] | MemAttr[5:2] | S2AP[8:7]=RW | valid
  - RAM: MemAttr=0b1111(ARM: S1 关时按 Normal WB 处理)
  - 设备: MemAttr=0b0001(nGnRE) + XN[54:53](不可执行)
- API: `stage2_init()`(boot 固定建图) / `stage2_walk(ipa,&pa)` / `stage2_dump()`(shell `s2dump`)
- 建表后 `dsb sy`;首次进入 guest 前 `tlbi vmalls12e1is; dsb sy; isb`

## 4. vCPU 与进入(方案 A)

```c
struct vcpu { u64 entry_pc, entry_sp, spsr; struct { u64 sp, lr; } host_ctx; };
```
- `vcpu_run(vm, vcpu)`:VTTBR=pgd|VMID0 → HCR_EL2=RW|VM(0x80000001) →
  SP_EL1=entry_sp → SPSR_EL2=0x3C5(EL1h+DAIF 全掩) → ELR_EL2=entry_pc →
  **ERET**(EL2 SP/LR 存入 host_ctx,交给 asm)
- **退出路径**:guest `HVC #1`(exit)→ EL2 handler 清 HCR.VM/VTTBR + tlbi →
  恢复 host_ctx(sp/lr)→ 从 vcpu_run 正常返回 → 状态机 RUNNING→TEARDOWN→DEAD
- lower-EL AArch64 sync 槽(向量表第 9 槽)从 panic 占位 → 实装 el1_sync:
  EC=0x16(HVC)分发,其余仍 panic;lower-EL irq 保持占位(guest 轮询,不开中断)

## 5. hypercall ABI(第一个 ABI)

```
HVC #0  putc: x0=char    → EL2 console_putc;x0 返回 0
HVC #1  exit: 无参       → VM TEARDOWN→DEAD,vcpu_run 返回
```
(未列入的功能一律 not-supported panic——延续默认拒绝。)

## 6. guest 侧(独立构建)

`guest/`:linker@0x40000000、start.S(设栈,无向量表——一切异常皆陷 EL2)、main.c
- B 相:banner 逐字符 HVC putc → 立即 HVC exit(往返证明)
- C 相:banner("eshine-guest @EL1 via stage2")→ "guest> " → 直读 IPA 0x09000000 UART
  (自带 mini pl011)行缓冲回显/`echo`/`exit`(HVC #1)
- 构建:`guest.elf` → `objcopy -O binary` → `guest.bin`;
  run_qemu.sh 增 `-device loader,file=guest.bin,addr=0x50000000,force-raw=on`

## 7. 门禁扩展(C 相定型)

```
banner → eshine> → run_guest → guest banner + guest> →
echo GOK<pid> 双回显 → exit → VM_DEAD + eshine> → PASS
```
一次跑通:EL2→EL1 进入、非恒等 stage2 翻译、HVC 陷入、MMIO 映射、状态机真实转换。

## 8. 风险与回退

| 风险 | 回退 |
|---|---|
| MemAttr 0b1111 在 S1-off 下的行为差异(理论 Normal WB) | guest 升级 S1 恒等映射(C 相预留);门禁实测把关 |
| HCR/异常路由地雷 | 三相小步,每相门禁;slot 9 之外全部保持 panic 占位 |
| guest 增长越过 2MB block | 本周范围外;超限是下一里程碑(多 block)的自然议题 |

## 9. 目录落点

```
hypervisor/arch/arm64/mm/stage2.c + include(mm.h)
hypervisor/arch/arm64/vcpu_entry.S
hypervisor/kernel/vcpu.c + include(vcpu.h)   # struct vcpu/vcpu_run
hypervisor/kernel/hcall.c + include(hcall.h) # HVC 分发
guest/{start.S,main.c,linker.ld,miniuart.h}
ARCHITECTURE.md(+IPA 图/hcall ABI)  verify_el2.py(扩展)
```
