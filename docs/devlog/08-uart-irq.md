<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# Devlog 08 — UART IRQ 注入站（vSPI 33 + 中断驱动 getc）

日期:2026-10-23/24 · 版本:27.04-dev · 门禁:28 步真绿 ×2 连过

## 里程碑

RX 从轮询换成中断驱动——第六站让 FR 轮询+WFI 替换了 HVC 风暴，
本站让 vSPI 33 替换了轮询。**每一代等待机制被下一代替换**，连载
的弧线：

    [vm0] uart irq on            ← guest 使能 RXIM 后自报（门禁判据）
    （此后每字符:shell 入环→置 RIS→泵注 LR1 vSPI 33→
      guest 向量认领→handler 排空 DR 入软环→EOI 直撤→getc 续取）

## 战况一：ICH_LR 位号名实分离（本站最狠）

UART 首版注入照抄第四站的常量名：`PENDING=1<<61`、`HW=1<<62`。
**真实布局是 bit60=Group、bit61=HW、bit62=State(pending)**——
第四站的 vtimer 三位全置，恰好凑成正确形态，但名字全错；UART
照抄错名置了 Group+"PENDING"（实为 HW 位），**状态位空 = LR 无效**。

症状是 GIC 史上最阴的一类静默：**写进去读回来原样**（探针回读
LR/VMCR/HCR 全对），**vIRQ 线就是不抬**；更妙的是 QEMU 的 WFI
语义——"有 pending 中断则完成而不陷阱"，无效 LR 也算——guest 的
getc 循环空转烧量子，泵每轮注入一次成风暴，sleep 的 27 还被
风暴覆盖。活体取证：LR pending + guest 到达 wfi + 无向量进入 =
线没抬，三位逐一对照 ARM ARM 才见名实分离。

试探版又踩一脚：补上"HW"位（1<<62，实为状态位）+pINTID——
**HW LR 的挂起跟随物理线**，物理 SPI 33 无源，永不投递。纯虚拟
SPI 的正形 = **Group + State(pending) 两位置足，HW 位不置**。

> 教训：**位号以架构手册为准，不以"能跑的旧代码"为准**——旧代码
> 可能只是碰巧三位全置。回读正确不等于生效，无效 LR 是静默的。

## 战况二：post-index 回写型存储（ISV=0）

guest 的 IMSC 写首跑即死：ESR 的 ISV=0。反汇编见 GCC 把
`uart_reg(UART_IMSC)=0x10` 合并成 `str w1,[x18],#-32`——**post-index
回写型，综合症无法编码回写，宿主拿不到写值**，只能默认拒绝。
修复：guest 访存器全部改 volatile asm 访问器（`str %w0,[%1]`），
**形式上保证每条访存都是可综合症的 plain ldr/str**。设备仿真的
契约从"驱动不改语义"加深到"访存形态受契约约束"。

## 战况三：多字符爆发停在半程

投递修通后，`sleep\n` 六字符只 echo 五个——每发注入弹一个字符，
弹完即 WAIT，没人再注入（字符还在宿主环里，RIS 还挂着）。
双修：**ISR 排空式**（一次 IRQ 清空整批——真 UART 驱动同律）+
**泵内再唤醒兜底**（RX 等待者仍有 irq_ready 数据即置 runnable，
封死注入与消费之间的时序缝隙）。

## 顺带修掉

- vm_create 帧尾复位：DEAD→INIT 槽位复用不许带出旧 wait_state
  （残 1 则新客永不被泵——潜在雷，实测前修）。
- 门禁 27→28（[9] uart irq on 判据），shell help 走查清单同步。

## 已知限制

- 仅 RX 中断（TXFF 恒 0 无源，TX 中断不仿真）
- 单设备直查（多仿真设备后 mmio_dispatch 查设备表）
