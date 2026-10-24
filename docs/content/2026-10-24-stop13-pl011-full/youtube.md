<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# YouTube / B 站分镜:PL011 完整化站(设备证明自己是谁)

## 标题候选

- 中文(B 站):从零写 Type-1 虚拟化 #13:给串口办身份证——AMBA
  总线怎么认设备(附:真语义咬到自家引导顺序)
- EN:From-scratch Type-1 Hypervisor #13: The UART Gets an AMBA
  ID Card — and real CR semantics bite my own boot order

## 描述

第十三站:PL011 完整化,Linux 战役最后一门地基。AMBA 身份证
(PERIPHID/PCELLID 只读常量,id 0x00141011/0xB105F00D——总线先验
身份证再看演技);控制族存储回读(复位值按真芯片);CR 真语义
(DR 写要 UARTEN|TXE,RX 中断要 UARTEN|RXE);guest 引导按 Linux
pl011 同律。战况:真语义上线第一件事咬到自家引导顺序——开机前
输出全被门吞,修法不删门,引导序回真 OS 形态。回归即验收:旧 36
步一条不掉,门禁 36→37 双绿。三门欠账全消,Linux bring-up 就绪。
代码:github.com/eshine-os/eshine-os · www.eshine-os.org。每晚 1-2h,
AI 结对,从零造 Type-1。

## 标签

B 站:虚拟化 hypervisor ARM64 操作系统 内核 嵌入式 QEMU AI编程
串口 pl011 驱动开发 从零开始 eshine-os
YouTube:hypervisor arm64 virtualization osdev type-1 qemu pl011
amba driver from scratch

## 分镜脚本(约 11 分钟)

| # | 时段 | 画面 | 口播/字幕要点 |
|---|---|---|---|
| 1 | 0:00-0:50 | 录屏:pl011 on: amba 00141011 | 钩子:串口办身份证;Linux 战役最后一门地基 |
| 2 | 0:50-2:50 | 重点:ID 字节表+代码 | AMBA 绑定机制:PERIPHID 四字组 id,PCELLID 校验;总线先验身份证再看演技;没有 ID 就没有绑定 |
| 3 | 2:50-4:30 | 代码:控制族+guest bring-up | Linux pl011 同律:关机→波特(不仿真时序,登记即诚实)→8N1+FEN→开机;复位值按真芯片 |
| 4 | 4:30-6:50 | 战况:首测回放 | CR 真语义:关机态写 DR 无效;开机前输出全被吞(pl011/gicd 自报齐蒸发);"这不是 bug,是模型终于对了" |
| 5 | 6:50-8:30 | 修法 diff:引导序 | 不删门,引导序回真 OS 形态:pl011 先行,开机后自报;Linux 同律(驱动起完设备才有 console);FAIL 也要有通道 |
| 6 | 8:30-9:40 | 录屏:回归全链 | 回归即验收:旧 36 步经 CR 门一条不掉(echo/selftest/dwrite/sleep) |
| 7 | 9:40-10:30 | 对照图:三门清单 | DTB ✓ GICD ✓ PL011 ✓;Linux bring-up 就绪——下一站预告 |
| 8 | 10:30-11:00 | make verify 37 步 + 尾板 | 门禁判据;仓库+站点 |

## 录屏清单

1. pl011 on 自报 + 全链回归(分镜 1/6)
2. 身份证字节表(分镜 2,静态图)
3. 被吞输出的首测现场(分镜 4,可复现录屏)
4. 引导序前后 diff(分镜 5,静态图)
5. make verify 37 步 ×2(分镜 8)
