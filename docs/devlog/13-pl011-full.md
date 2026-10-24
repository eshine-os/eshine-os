<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# Devlog 13 — PL011 完整化站（AMBA 身份证 + 控制寄存器族）

日期:2026-10-24 · 版本:27.04-dev · 门禁:37 步真绿 ×2 连过（全清重建零警告）

## 里程碑

vuart 从"与自制 guest 严格对齐的子集"长成 **Linux pl011 驱动可探测
可绑定**的形态:

    [vm0] pl011 on: amba 00141011   ← 身份证八字节自查通过后自报

Linux 战役收官段三选一中最小的一站;至此三门欠账全消(DTB/GICD/PL011),
Linux bring-up 就绪。

## 形态

- **AMBA 身份证**:PERIPHID 0xFE0-0xFEC(0x11/0x10/0x14/0x00,组
  AMBA id **0x00141011**)+ PCELLID 0xFF0-0xFFC(0xB105F00D)只读
  常量——AMBA 总线靠它绑驱动,**设备证明自己是谁**。
- **控制族**:CR/IBRD/FBRD/LCR_H/IFLS/DMACR 存储回读;复位值按
  真芯片(CR=0x300 即 TXE|RXE 而 UARTEN=0、IFLS=0x12);波特率
  不仿真(无时钟即无时序,YAGNI)。
- **CR 真语义**(本站核心):DR 写要 **UARTEN|TXE**,RX 中断就绪
  要 **UARTEN|RXE**——关机态写 DR 无效,真 PL011 同律。
- **guest 引导按 Linux pl011 同律**:身份证自查(静默)→关机
  (CR=0)→配波特(IBRD=13/FBRD=1)→LCR_H=8N1+FEN→IFLS/DMACR→
  IMSC=RXIM→开机(CR=0x301)→自报。

## 战况:开机前的输出被门吞了

首版把 pl011 自报放在开机**前**、gic 初始化放在 pl011 **前**——
CR 真语义上线后,复位 UARTEN=0,DR 写全被门丢弃:pl011 自查行、
连站十二的 gicd/gic 自报行全部无声蒸发,boot 只剩 banner 以后
的输出。门禁判据靠"掉 needle"当场示警。

这不是 bug,是**真语义第一次咬到自家的引导顺序**:真 PL011 关机态
就不该有输出,是我此前的模型"读 0 写忽略"惯出来的路径依赖。
修法不删门,而是**引导序回到真 OS 形态**:pl011 先行(探针静默,
开机后自报),gic 随后——Linux 同律:驱动起一个设备,起完才有
console。自报挪到 CR 使能之后,FAIL 诊断也走开机后通道。

## 回归即验收

旧 36 步一条没掉:echo/RX 链/selftest/dwrite/dread/sleep 全部
经 CR 门照常——TX/RX 门的回归验证本身就是本站的验收判据(判据
即测试的又一回)。终测一次全绿,全清重建零警告。

## 数字

- 门禁 36 → **37 步双绿**(+AMBA 身份证判据)
- 模型增 ~60 行(控制族+身份证+CR 门);guest bring-up 重排
- 已知限制(登记 #17):波特率时序/RSR-ECR 错误位/流控/DMA
  语义不仿真(无消费者,YAGNI)
