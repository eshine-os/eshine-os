<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 第十三站设计:vuart PL011 完整化站(AMBA 身份证 + 控制寄存器族)

日期:2026-10-24 · 版本:27.04-dev · 前站门禁:36 步真绿 ×2

## 0. 决策记录

用户令"开始下一站"——按前报推荐序执行:**PL011 完整化先行**(Linux
战役收官段三选一中最小的一站;完成后 Linux pl011 驱动可探测可绑定,
三门欠账全消,Linux bring-up 就绪)。

## 1. 问题

vuart 至今是与自制 guest 严格对齐的子集(DR/FR/IMSC/RIS/MIS/ICR,
第六站建第八站中断化)。Linux 的 pl011 驱动(AMBA 总线)不吃这套:

- **AMBA 探测**:驱动绑定前,总线先读 **PERIPHID** 四字(0xFE0-
  0xFEC)与 **PCELLID** 四字(0xFF0-0xFFC)组成身份证——PL011 =
  PIDR 0x11/0x10/0x14/0x00(AMBA id 0x00141011),CID 0x0D/0xF0/
  0x05/0xB1(0xB105F00D)。没有身份证,没有绑定。
- **控制寄存器族**:pl011_init 要写 **CR(0x30,关机→配置→UARTEN|
  TXE|RXE)/IBRD(0x24)/FBRD(0x28)/LCR_H(0x2C,8N1+FEN)/IFLS
  (0x34)/DMACR(0x48)**——当前模型全是读 0 写忽略。

## 2. 方案

### 2.1 模型(vuart.c):身份证 + 控制族 + CR 真语义

- **新寄存器**:cr/ibrd/fbrd/lcr_h/ifls/dmacr 存储回读(波特率
  不仿真——无时钟即无时序,YAGNI;复位值按真芯片:CR=0x300
  (TXE|RXE,UARTEN=0)、IFLS=0x12,余 0)。
- **PERIPHID/PCELLID**:只读常量 0x11/0x10/0x14/0x00 与
  0x0D/0xF0/0x05/0xB1(公开 TRM + QEMU 行为基准)。
- **CR 真语义**(本站核心):
  - DR 写(TX)要求 **UARTEN(bit0)|TXE(bit8)**,否则丢——真
    PL011 关机态写 DR 无效;
  - RX 中断就绪(vuart_rx_irq_ready)追加 **UARTEN|RXE(bit9)**
    门——与 IMSC/RIS 三重与(真语义:没开接收就不该有 RX 中断)。
- 其余(DR 弹环/RIS 重评定/行缓冲前缀/ICR)不动。

### 2.2 guest(miniuart.h):Linux-pl011 同律 bring-up

    关机(CR=0)→ 配波特(IBRD=13/FBRD=1,115200@24MHz)→
    LCR_H=8N1+FEN → IFLS/DMACR → IMSC=RXIM → CR=0x301 开机

前置 **AMBA 身份证自查**:读八字节 ID,组 AMBA id 验 0x00141011、
CID 验 0xB105F00D,过则自报 `pl011 on: amba 00141011`(门禁判据),
败则 `pl011 FAIL`。boot 顺序:gic → **pl011 probe+init** → virtio →
banner → fdt → uart irq on → virtio blk on。

### 2.3 门禁(36→37,三处同步)

- [14] `[vm0] pl011 on: amba 00141011`(身份证判据);旧 14-36 顺延
  +1 → 37。
- 判据即测试余韵:CR 真语义上线后,**旧 36 步一个不许掉**——
  echo/RX 链/selftest/dwrite/dread 全部要经新门照常(TX/RX 门的
  回归验证)。

## 3. 内容包(随站新规)

5 文件,主打"设备证明自己是谁"(AMBA 身份证)+自制子集长成
Linux 可绑形态(承站 12 guest 真启动式叙事)。

## 4. 偏离登记

#17(承 #13 演进):vuart PL011 子集 → Linux 驱动可用形态——控制
族存储回读+CR 真语义(TX/RX 门)+AMBA ID 只读;波特率/错误状态
(RSR/ECR)/硬件流控不仿真(无消费者,YAGNI)。

## 5. 不做(YAGNI)

- 波特率时序(无时钟源);RSR/ECR 错误位;MS 出错注入;DMA
  (DMACR 只存储回读);16750 扩展
