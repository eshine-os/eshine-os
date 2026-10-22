<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 27.04 第四站设计:GICv3 中断站(宿主自用中断 + 虚拟注入)

- **日期**: 2026-10-22
- **状态**: 已评审通过(user 批准四节设计,"开写 spec")
- **前置**: 第三站(916f079:陷阱帧/yield/fault 隔离,17 步门禁)

## 0. clean-room 与参考源

实现以 **ARM GICv3 规范(IHI0069)**、**ARM ARM(DDI 0487)** 与 **QEMU virt
文档**为源。参考实现的 vgic/timer 形态已由探索代理扫描(本仓不读其代码,
结论如下):参考实现宿主用 CNTHP_EL2 + PPI 26;guest vtimer CNTV 直通 PPI 27,
**以 HW-mapped LR 注入**(ICH_LR 置 HW 位 + 物理 ID[47:32],宿主
ICC_CTLR EOImode=1 且自身只降优先级不撤销,guest EOI 时硬件自动撤销物理
线);WFI 走 HCR.TWI 陷阱 + `vcpu_wfi_emulate`(带超时轮询再线程 sleep)。
本站选型与该形态一致,差异入 §6。

## 1. 目标与非目标

**A 相目标**: 宿主自己会用中断 —— GICv3 EL2 初始化、CNTHP 物理定时器、
真 EL2 IRQ handler(替换 WFI 停车)、`tick` 计数命令。
**B 相目标**: 虚拟中断注入全链 —— vgic 使能 + HW-mapped LR 注入 vtimer
PPI 27、HCR.TWI、第四原因码 VCPU_WAIT、宿主运行循环雏形、guest 首建
VBAR_EL1 向量表、`sleep` 命令由异步中断唤醒。

**非目标(记账)**: LR 满兜底(维护中断 PPI 25 + UIE + pending ring——
本站单 vIRQ 串行注入,LR 构造性不满)、SGI 软件注入、GICD/GICR MMIO
模拟(参考实现 gic_v3_mmio 形态已备案)、guest EL1 物理 timer(PPI 30)模拟、
中断直通模式、per-VM vtimer 上下文(参考实现 vtimer_ctx_save/restore 已备案,
留调度站)、UART RX 中断化(留调度站)、多 vCPU 抢占。

## 2. A 相:宿主自用中断

```
新增 hypervisor/drivers/gicv3.{h,c}:
  常量: GICD 0x08000000 / GICR_RD(CPU0) 0x080A0000 / GICR_SGI 0x080B0000
        (QEMU virt 连线,与参考实现 qemu_virt dtsi 同源;PPI: 宿主 timer=26)
  gicv3_init(): GICD 校验 ARE → GICR_TYPER 探测 → PPI 26 配置
        (edge / Group1-NS / 优先级 / 使能)→ CPU interface(ICC_SRE_EL2
        使能系统寄存器访问、ICC_PMR、ICC_IGRPEN1、ICC_CTLR EOImode=1)
  gicv3_claim(): 读 ICC_IAR1 返回 intid
  gicv3_eoi(intid): 只降优先级(EOImode=1 语义,物理线由 guest EOI 联动撤销)
新增 hypervisor/drivers/timer.{h,c}:
  timer_init(): CNTHP_CVAL/CTL 编程,tick 计数器
  timer_handler(): tick++ + 重装下一拍(1s 周期)
exceptions.c: el2_irq() = claim → (26: timer_handler) → eoi;未知 intid
        打印计数不 panic(中断是数据,同 fault 隔离哲学)
main.c 顺序固化: console_init → gicv3_init → timer_init → vcpu_init
        → stage2…shell;shell_run 入口前 daifclr 解 IRQ(顺序即安全边界)
shell: `tick` 命令(打印计数;两次递增 = 确定性判据)
```

## 3. B 相:虚拟中断注入与调度器种子

```
gicv3.c 增虚拟面(同一 IP 块):
  vgic_enable(): 读 ICH_VTR 得 LR 数;ICH_VMCR(VPMR/VBPR0/VBPR1/组使能)
        → ICH_HCR.En=1
  vgic_inject_vtimer(): ICH_LR0 = vINTID 27 | HW 位 | 物理 ID 27[47:32]
        | pending | 优先级(参考实现同款编码,LR 位段按 IHI0069 实施期钉死)
  gicv3_init 扩展: 预配置 PPI 27(真 redistributor,edge/Group1-NS/
        优先级/使能);el2_irq 分发扩展: 26→timer_handler,
        27→vgic_inject_vtimer(宿主对 27 只降优先级,物理线由 guest
        EOI 硬件联动撤销)
vm.c: vm_create 无新增配置 —— PPI 27 由 boot 期 gicv3_init 统一预配;
        guest 永不触碰
        GIC MMIO(只用 ICC_* 系统寄存器,硬件虚拟化零陷阱)
vcpu.c: HCR_GUEST |= TWI
exceptions.c el1_sync 新分支: EC=0x01(WFX/WFI 陷阱,ISS 区分 WFI/WFE,
        仅 WFI 认账): LR 有 pending → 直接 trap_resume(vIRQ 已到,WFI 即
        nop 语义);无 → exit_reason=VCPU_WAIT,vcpu_exit()
vcpu.h: #define VCPU_WAIT 3;vm_run 内循环:
        while (reason==VCPU_WAIT) { 宿主 WFI;timer IRQ 已注入 → 重进 }
        exit/yield 才返回 shell —— vm_run 仍阻塞,运行循环藏在里面
CNTHCTL: 按需开放 EL1 计数器/频率访问位(参考实现设 EL1PCTEN=1 先例;
        CNTVCT/CNTV_*/CNTFRQ 的具体 gate 位实施期按 ARM 定)
```

**guest 侧(第一次拥有自己的异常世界)**:

```
guest/linker.ld: .vectors 段(2KB 对齐,VBAR_EL1 要求)
guest/start.S: 装 VBAR_EL1 + 解 DAIF(在 shell 循环入口,时机自控)
guest/vec.S: 16 槽 EL1 向量表;IRQ 槽(current-EL SP_ELx 组,偏移 0x280)
        最小帧(x0-x18+lr)→ C isr → 恢复 → eret;其余槽 WFI 死循环
guest/gic.h: ICC_IGRPEN1/ICC_PMR 使能(硬件虚拟化,直通)
guest/timer.h: CNTFRQ/CNTVCT 读 + CNTV_CVAL/CTL 编程
guest/main.c: 新命令 `sleep`:设 CNTV(~1s)→ WFI → 醒后关 timer →
        打 "slept"(全链判据:WFI 陷阱→WAIT→物理 IRQ→LR 注入→vIRQ→
        ISR→回 shell 循环)
```

vtimer 状态边界:本站单 vCPU 串行,guest 每命令全量编程 CNTV
(醒来即 ENABLE=0),CNTVOFF=0;per-VM vtimer ctx 留调度站。

## 4. 门禁(17 → 20 步)

| # | 判据 | 相 |
|---|---|---|
| 1-3 | banner + prompt + 宿主 token 双回显(不变) | — |
| 4 | `tick` → 读数;等待 ≥1s 再 `tick` → 递增 | A |
| 5-9 | vmcreate/vmrun 0 → selftest → token → exit→VM_DEAD(沿用) | — |
| 10 | vmrun 1 → `sleep` → **"slept"**(异步中断唤醒全链) | B |
| 11-13 | fault 隔离链(沿用)+ VM_DEAD | — |
| 14 | 宿主 `tick` 再次递增(guest 往返后宿主中断仍活) | A/B |
| 15-16 | yield→PAUSED→resumed(沿用) | — |
| 17-20 | 全 DEAD 表 + 宿主完整性 token + vmkill 拒绝 + 槽复用 | — |

心跳 `♥` 输出仅目检,不入门禁(异步字符与判据位置有竞争,不重蹈
FR 教训)。连续 ×2 全过。

## 5. 风险与预置

- **QEMU TCG 的 ICH 支持面**(LR/VMCR/HCR 虚拟化)A 相末实测;若注入
  缺件,退级方案 = 纯软件 vIRQ(非 HW 位)+ 宿主轮询 LR 状态收尾 ——
  vgic_inject_vtimer 是唯一注入点,换后端不动调用方
- PPI INTID(26/27)与 GIC 常量已双源核对(公开文档 + 参考实现 dtsi);
  WFX 的 ISS 位段、CNTHCTL gate 位、LR 位段实施期按 IHI0069 钉死
- 门禁对异步输出免疫:tick 用两次读数差,sleep 用确定性等待

## 6. 参考实现基准对齐(差异预登记,Task 5 入册)

| 形态 | 参考实现 | 新站 | 定性 |
|---|---|---|---|
| vtimer 注入 | 全 PPI/SPI 统一 HW-mapped LR + LR 满兜底(maint PPI 25+UIE+ring) | 仅 vtimer 单 LR,无兜底 | 结构一致,范围缩小(登记 #8) |
| WFI 陷阱 | per-domain manifest 开关 + 超时轮询再线程 sleep + wfi bool | 全局 TWI + 结构化 VCPU_WAIT 原因码 | 新仓显式化,改进(登记 #9) |
| vtimer 上下文 | vtimer_ctx_save/restore per-vCPU(CNTVOFF/CTL/CVAL) | 串行共享 + 每命令全量编程 | 范围边界,调度站补(登记 #10) |
| 宿主 timer | CNTHP PPI 26 + 软件 timer 堆框架 | CNTHP PPI 26 + 裸 tick 计数 | 命名一致,框架留后续 |
| gic 初始化 | gic_ops 版本分发表 + DTS 注入地址 | 单版本直书常量 | 简化(登记 #8 并条) |
