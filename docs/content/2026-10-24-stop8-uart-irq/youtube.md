<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# YouTube / B 站分镜:UART IRQ 注入站(回读正确,就是不生效)

## 标题候选

- 中文(B 站):从零写 Type-1 虚拟化 #8:最阴的坑——寄存器回读
  完全正确,硬件就是不干活(UART 中断注入)
- EN:From-scratch Type-1 Hypervisor #8: Readback Correct, Effect
  Absent — UART IRQ injection & the LR bit-name trap

## 描述

第八站:UART RX 改虚拟中断驱动(vSPI 33,LRI 通道),FR 轮询退役。
三大战况:①ICH_LR 位号名实分离——真实 bit60=Group/bit61=HW/bit62=
State,第四站常量名字全错但三位全置碰巧正确;照抄错名=LR 无效,
回读正确≠生效,无效 LR 连 WFI 陷阱语义都骗(QEMU 完成不陷阱→空转
风暴);②GCC post-index 回写型访存 ISV=0,宿主拿不到值→guest
访存器全改 volatile asm;③多字符爆发停半程→ISR 排空式+泵再唤醒。
门禁 28 步双绿。代码:github.com/eshine-os/eshine-os ·
www.eshine-os.org。每晚 1-2h,AI 结对,从零造 Type-1。

## 标签

B 站:虚拟化 hypervisor ARM64 操作系统 内核 嵌入式 QEMU AI编程
调试 从零开始 eshine-os
YouTube:hypervisor arm64 virtualization osdev type-1 qemu GIC
interrupt debugging from scratch

## 分镜脚本(约 12 分钟)

| # | 时段 | 画面 | 口播/字幕要点 |
|---|---|---|---|
| 1 | 0:00-0:50 | 录屏:[vm0] uart irq on | 钩子:串口输入从此走中断;本站标题坑预告——回读正确≠生效 |
| 2 | 0:50-2:20 | 代码:vuart 中断面 + LR1 注入链 | IMSC/RIS 语义(真 PL011 同律:入环置挂起/读 DR 重评定);泵注入纪律 |
| 3 | 2:20-4:30 | 重点:回读探针行 + 位号对照表 | 名实分离解剖:三位全置碰巧正确 vs 状态位空=无效;症状三连(回读原样/线不抬/WFI 完成不陷阱→空转风暴覆盖 vtimer);"位号以手册为准,不以能跑的旧代码为准" |
| 4 | 4:30-6:00 | HW 试探版补刀 | HW LR 挂起跟随物理线,物理无源永不投;纯虚拟正形=Group+State 无 HW 位 |
| 5 | 6:00-7:30 | 反汇编:str w1,[x18],#-32 | post-index 回写型 ISV=0:综合症编码不了回写,宿主拿不到值只能拒;asm 访问器形式保证;编译器的聪明是仿真器的敌人 |
| 6 | 7:30-9:00 | 录屏:六字符只回显五 | 多字符爆发停半程:一发弹一个;ISR 排空式+泵再唤醒双修 |
| 7 | 9:00-10:30 | 录屏:全链(selftest/echo/sleep 经中断) | 弧线收束:HVC 风暴(5)→FR 轮询(6)→vSPI 33(8),每代为上代收尸 |
| 8 | 10:30-12:00 | make verify 28 步 + 尾板 | 门禁判据(uart irq on);帧尾复位防雷;下一站预告;仓库+站点 |

## 录屏清单

1. [vm0] uart irq on + 交互全链(分镜 1/7)
2. 探针/回读对照素材(分镜 3,静态图即可:INJ33 行+手册位号)
3. post-index 反汇编截图(分镜 5,静态图)
4. make verify 28 步 ×2(分镜 8)
