<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# Devlog 12 — vgic GICD/GICR 仿真站（distributor 成为权威）

日期:2026-10-24 · 版本:27.04-dev · 门禁:36 步真绿 ×2 连过（全清重建零警告）

## 里程碑

guest 的中断从此有 distributor——真 OS 形态的 bring-up:

    [vm0] gicd v3 ok            ← PIDR2 探测:(x&0xf0)==0x30,经全链陷阱
    [vm0] gic on: 27 33 35      ← GICR PPI 27 + GICD SPI 33/35 使能,
                                   回读验证通过后自报

第十一站树里缺席的 intc 节点请回树（its/maintenance 仍未仿真不登记，
诚实原则不动）；泵注入三通道全部改按 guest 使能位门控——**distributor
成为注入权威**：guest 没使能就不注入、不代完成 WFI，它自己的配置
自己负责。

## 形态

- **窗口镜像 QEMU virt 真布局**：vGICD@0x08000000（0x10000）、
  vGICR@0x080A0000（0x20000，RD+SGI 双帧）；devices[2]/[3] 正好
  用满 ESHINE_MAX_DEV=4。offset 全部经手册复核（GICD_ISENABLER
  0x100+4n 从 SPI 32 起算、GICR_TYPER 的 Last 位是 bit4——站八
  纪律"位号以手册为准"的又一次正面执行）。
- **寄存器分层**：探测/使能/唤醒路径真语义（PIDR2/CTLR(RWP 恒 0)/
  WAKER 存储回读/ISENABLER 写置位），纯状态寄存器存储回读，未知
  RAZ/WI（设备级默认拒绝，承 vblk 先例）。写即完成——轮询型 guest
  （Linux 的 WAKER/RWP 循环）零等待通过。
- **状态随 VM 生命周期**：vm_create 复位 vgic——与磁盘持久化互补：
  distributor 是每 VM 新硬件，磁盘是每 VM 旧资产。

## 战况（三处，两处被 guest 回读验证当场抓获）

1. **stage-2 单页铁律拦下真布局**：vGICD 64KB/vGICR 128KB 跨页，
   stage2_build 的 `size != PG_4K` 拒绝——对 EMULATED 窗口这是过严
   约束（反正不建映射，窗口大小只是陷阱区）。放行 EMULATED 多页
   （对齐+4K 整数倍），直映射仍单页。
2. **GICD SPI 寄存器族解码 bug**：首版把 0x080-0x3FF 当一族共用
   `(off-0x080)>>2`，ISENABLER0(0x100) 被算成 n=32 越界——写静默
   WI、读 RAZ，**guest 使能回读验证当场抓获**（这正是判据里放回读
   的意义：判据即测试）。
3. **SAS 编码值 vs 字节数**：mmio 分发把 ISS.SAS 的编码值（2=4 字节）
   当字节数传进模型——4 字节读被当半字，掩码 0xFFFF 把 0x08000000
   吃成 0。自证三遍全对的代码，探针打点两行实锤。修法：`size = 1 << sas`
   标准换算。**教训：自证无误≠实测无误，回读与探针是两个不同层。**

小战况：vgic.c 的 SPDX 行被我收成 `-->`（markdown 肌肉记忆串台），
`/*` 不闭合吞掉版权行——-Wcomment 一发命中，秒修。

## 数字

- 门禁 34 → **36 步双绿**（+gicd 探测/gicd+gicr 使能回读两判据）
- intc 节点回树（blob 1057→1303 字节）；its/psci/maintenance 仍缺席
- vgic 模型 ~250 行；guest bring-up ~10 次陷阱走全链
- 已知限制（登记 #16）：无 LPI/ITS/1-of-N 路由/IROUTER，TYPER 全
  字段位考古（LPI/ESPI/安全扩展位）留 Linux bring-up 站按需补
