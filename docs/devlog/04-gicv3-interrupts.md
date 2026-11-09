<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# Devlog 04 — GICv3 中断站（真中断 + 虚拟注入全链 + WFI 陷阱化）

日期:2026-10-22 · 版本:27.04-dev · 门禁:20 步真绿 ×2 连过

## 里程碑

宿主第一次活在"被中断"里，guest 第一次收到与宿主物理线无关的虚拟中断：

    eshine> tick    → ticks: 0 → 2 → 5（CNTHP 1s 周期真投递）
    vm0> sleep      → sleeping... → slept (vtimer irq)
                      （CNTV 到期 → 宿主注入 LR → ICV 投递 → guest 自己的向量
                        认领 → 虚 EOI 硬件联动撤销物理线 → 醒来）
    eshine> tick    → guest 退场后仍增长（HCR/DAIF 恢复判据）

全链：CNTHP=宿主 tick（重装后 EOIR+DIR）；CNTV=guest vtimer（宿主
只降优先级，撤销由 guest 虚 EOI 经 HW-LR 硬件联动完成）；WFI 陷阱化
（TWI → VCPU_WAIT，宿主陪睡唤醒后代其完成，guest 醒后自查标志）。

## 战况：五层根因的连环破案（本站真正的知识增量）

Task 1 写完即零调试信号：ticks 恒 0。逐层解剖，五因连环，全部与
"抄记忆"有关（第一站 FR 偏移教训的全面重演+升级）：

1. **SGI 帧偏移幻觉**：IHI0069 的 SGI 帧实为 GICD 家族镜像
   （IGROUPR0@0x80、ISENABLER0@0x100、ICENABLER0@0x180）。凭记忆写的
   "紧凑布局"（0x100/0x104）使"组配置"实际命中 ISENABLER0（意外使能），
   "使能"落进未实现空洞——QEMU 保留区 RAZ/WI，写静默丢、读回零，
   即"写丢"表象。修复：QEMU 8.2.2 gicv3_internal.h 逐行对齐。
2. **GICD_CTLR 组位**：写 1 只开 EN_GRP0；G1NS 分发门是 bit1。
   G0 类中断还需 ICC_IGRPEN0（未设）→ cpuif 按
   `icc_igrpen[grp]==0` 干脆不产生任何信号——tick 恒 0 而 ISTATUS=1。
3. **PPI 接线**：dumpdtb 的槽位序不可反推（曾据此"纠正"成错误映射，
   绕了一圈）。活探针定案：挂起位与 CNTHP 到期时刻相关 → CNTHP=26；
   未知 intid 认领风暴（level+DIR 重挂起，顺带证明 vtimer 分支必须
   eoi_drop）→ CNTV=27。**连线以活体行为为准，不信文档槽位序**。
4. **HCR_EL2 IMO 位号**：真值 bit4，非 bit3。QEMU target_el_table
   rw=1 列必须 hcr=1 才把 EL2 物理 IRQ 路由回 EL2（否则判 EL1，
   cur>target 永不取）。QEMU cpu.h 与参考实现 sysregs.h 双源互证；
   `-d int` 零 "Taking exception"、gdb 断点零命中、isr/daif 全绿
   的矛盾组合是定位关键。
5. **vcpu_exit DAIF**：EL2 异常进入自带掩码，guest HVC 退出回宿主
   没人解 I 位 → 退场后 shell 永久失聪。心跳探针（每 5 tick 打点）
   + 20 步门禁新增的 [19] 复验步实锤抓出。修复：vcpu_exit 内
   daifclr。**guest 在场时它自己的 EL1 PSTATE 未掩码，所以 tick 能
   跨 guest 继续跑——这解释了"sleep 流程能活、退出即冻"的全部观测**。

红鲱鱼清单（耗掉半程，登记免再踩）：GICR_CTLR.UWP=QEMU 复位默认
且 RAZ/WI（与配置写无关）；GICR_WAKER 不门控任何 SGI 帧写；
GICD_CTLR 的 RWP 无异步语义；ISENABLER 的 set-bitmap 语义使"写后
读回"不能验证写路径。工具教训：中文 locale 下 make 报"错误"，grep
'error' 恒零——判绿一律用退出码 + LC_ALL=C；curl 拉源码遇 404 先怀疑
文件改名（QEMU 8.x 目录重组过）。

方法论沉淀：**模拟器之上开发，模拟器源码就是最终地面真值**——本机
QEMU 8.2.2 与 qemu v8.2.2 tag 逐文件对齐（redist/dist/cpuif/virt/
cpu.c/helper.c）；每个"记忆位号"先核源码再落键盘，核不到就活探针
（挂起位相关性、认领风暴、心跳打点三招覆盖了本站全部未知）。

## 实现形态（spec e3ed6d8 → 计划 eb561eb → 5 任务）

- **gicv3.c**：直书初始化（探 TYPER → CTLR=0 → WAKER → ppi_configure
  先禁清后配组/优先级再使能 → SRE/PMR/EOImode/IGRPEN1/CNTHCTL →
  CTLR=EN_GRP1NS → vgic_enable）；HCR_EL2_HOST=0x10 常量与 vcpu_exit 共享
- **irq_entry.S**：el2_trap 平衡桩（x0-x30 全量、eret 直用 ELR/SPSR），
  两组当前 EL 槽 + lower-EL 槽三处同接——guest 运行中的物理 tick 与
  宿主态 tick 走同一入口
- **vgic**：VMCR(VPMR+VENG1)/HCR(En)/LR0(v27|p27|G1|pending|HW)；
  EOImode 双面策略：宿主 EOImode=1（tick 用 EOIR+DIR），ICV 用复位
  默认 0（guest 一发 EOIR 即降+撤）
- **guest**：vec.S 十六槽向量表（两组当前 EL IRQ 槽共用平衡桩）+ gic.h
  （ICV 视角，HCR.IMO 重定向下按普通 GIC 语义写）+ timer.h（CNTV 直访）
  + sleep 命令（`while(!wake_count) wfi`，唤醒靠宿主代完成的陷阱回合）
- **vm.c**：WAIT 环（陪睡 + PC+4 代完成）；原因码 VCPU_WAIT=3（调度站
  的第一个种子语义）

## 对齐与登记

偏差 #8（单 LR 无兜底/单版本直书）、#9（TWI+WAIT+代完成）、#10
（vtimer 串行共享）按 spec §6 预登记入册站点设计文档。
门禁 17 → 20 步：[4] tick 递增、[13] sleep→slept、[19] guest 退场后
tick 复验——三步分别钉死物理面、注入面、恢复面。
