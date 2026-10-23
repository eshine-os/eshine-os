<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 27.04 第八站:UART IRQ 注入设计 spec

- 日期:2026-10-23/24 · 版本:27.04-dev · 前置:第七站 vtimer_ctx
- 用户批准:IRQ 驱动接管 getc(推荐);轮询路径退役

## 0. 目标与真绿定义

vuart 增中断面(RIS/MIS/IMSC/ICR 仿真),RX push 且 guest 使能 RX 中断
→ vgic 注入虚拟 SPI vINTID 33(QEMU virt PL011 IRQ 线);guest 向量加
UART handler,getc 改中断驱动。FR 轮询+WFI 让位的历史使命完成(第六站
是它把 HVC 风暴换掉的;现在它自己被 IRQ 换掉——每一代等待机制被
下一代替换,正是连载的好弧线)。真绿=门禁 27 → 28 步连续 2 过。

## 1. 形态

- **vuart 中断面**:`imsc`(0x38 读)/`ris`(0x3c:RX=bit4,RX 入环置位,
  读 DR 清)/`mis`(0x40 = ris & imsc)/`icr`(0x44 写清)。TX 中断
  不做(TXFF 恒 0,宿主即时排空,无源)。
- **注入通道编号化**:LR0 = vtimer(HW 映射,第四站)/ LR1 = vuart
  (纯虚拟 vINTID 33,G1 pending,无 HW bit,guest 虚 EOI 即撤)。
  "per-pCPU 单 LR"纪律升级为**编号通道 + 泵注入**:shell push 时置
  `uart_pending` 标志,泵轮到该 VM 时挂 LR1 再进 guest(防在跑别家
  串台消费——第五站纪律沿用)。
- **guest**:init 写 IMSC.RXIM 使能;向量表加 UART(SPI 33)handler:
  IAR 认领(33)→ 读 DR 入 guest 软环形缓冲 → EOI;getc 从缓冲取,
  空则 WFI(IRQ 到 → handler 填缓冲 → 同一 WFI 醒来续取)。TX 不变。
- **宿主 push 语义**:shell 入环后若 imsc.RXIM → 置 uart_pending;
  RX 等待者不再由直通唤醒点醒(IRQ 注入经泵到它时 LR 挂上,注入即醒)
  ——直通唤醒路径保留为"未使能中断时的回退"?不——**全切**:guest
  一定使能,不回退,双轨即复杂度(用户已批全切)。
  wait_reason 的 RX 唤醒语义变为"pending 注入后自然醒"。

## 2. 门禁 27 → 28

- 新增:guest 启动后打 "uart irq on"(IMSC 使能即打)——中断驱动
  模式的显式判据;原有 RX/echo 判据全部保留(机制换代判据不变)。
