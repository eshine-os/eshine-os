<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 27.04 第十站:virtio 真身站(virtio-mmio legacy + split virtqueue)设计 spec

- 日期:2026-10-24 · 版本:27.04-dev · 前置:0beaae4(第九站 vblk,31 步门禁双绿)
- 用户批准:四决策全选推荐(站点=virtio 真身/vblk 退役+数据迁移判据/
  legacy+split 最小/完成中断 vSPI 35·LR3)

## 0. 目标与真绿定义

第九站的"virtqueue 种子"长成行业标准设备:guest 从此说 virtio——
特性协商、描述符链、avail/used 双环、通知门铃、完成中断全链。自制
vblk 协议退役,**virtio-blk 接管同一静态磁盘**——门禁判据同强度
迁移(dwrite/dread 行为与 needle 不变),持久化判据(销毁重建数据
仍在)照旧。真绿=门禁 31 步(判据迁移非削减)×2。

## 1. 传输层:virtio-mmio legacy(窗口沿用 0x0A000000/4KB)

寄存器面(legacy 最小集):MagicValue(0x74726976)/Version=2/
DeviceID=2(blk)/VendorID=0x4553;DeviceFeatures=0(无特性位,
DeviceFeaturesSel=0)/DriverFeatures+Sel;QueueSel/QueueNumMax=8/
QueueNum/QueuePFN(legacy 队列基址页号);QueueNotify(门铃);
InterruptStatus(bit0=used 环更新)/InterruptACK;Status
(ACKNOWLEDGE/DRIVER/DRIVER_OK/FAILED)。**Status.DRIVER_OK 兼任
本设备的中断使能位**(virtio-mmio 无 IMSC,泵注入条件 =
DRIVER_OK 且有挂起)。

## 2. 队列:split virtqueue(legacy 布局公式)

QueueNum=8(2 的幂,深度即门禁节奏)。legacy 布局(guest 单块
连续区,QueuePFN 为基址页):
- desc 表:基址起 16N 字节(64 位 addr/32 位 len/16 位 flags/16 位 next)
- avail 环:紧随 desc(2 对齐):u16 flags/idx + N u16 ring + u16 used_event
- used 环:avail 后 4096 对齐:u16 flags/idx + N {u32 id,u32 len} + u16 avail_event
宿主与 guest 驱动同用此公式(自制两侧自洽;Linux 对齐留站后)。

## 3. 请求:virtio-blk 三描述符链

每请求 = desc 链三节:①header 16B(u32 type:0=读 1=写;u32 rsvd;
u64 sector)②data 512B ③status 1B(设备写 0=OK/1=ERR)。门铃
(QueueNotify)路径:宿主从 last_avail 扫到 avail->idx,逐链解析
(静态翻译 addr),对**同一静态磁盘**(承接 vblk 的数组,迁移即
结构连续)执行扇区读写,写 status、append used 环、used->idx++、
置挂起。参数越界:status=ERR 照常完成(设备级默认拒绝)。

## 4. 完成中断:vSPI 35(LR3)

用户批准新号:与 vblk 时代 34 区分,设备代际在中断号上可见。通道
编号化收束:LR0=vtimer(HW)/LR1=vuart/LR3=virtio(LR2 随 vblk 退役
空置)。泵注入纪律不变;再唤醒网泛化到 virtio_irq_ready。
guest ISR:读 InterruptStatus → InterruptACK(清)→ 查 used 环 →
置完成标志。

## 5. guest 驱动(guest/virtio.h)

probe(magic/version/devid 三验)→ Status|=ACKNOWLEDGE|DRIVER →
QueueNum=8 → 静态区(2 页:desc+avail|used)→ QueuePFN →
Status|=DRIVER_OK → 自报 `virtio blk on`。请求助手 submit(type,
sector):三 desc 填链、avail 入队、门铃、WFI 等完成中断。
dwrite/dread 命令名与输出 needle 不变(`blk wrote`/
`blk read: ESHINE-BLK-persist`)——**判据同强度迁移**。

## 6. 门禁 31 步(迁移非削减)

[10] `blk irq on` → `virtio blk on`;[17] dwrite/dread 完成链
(vSPI 35);[25] 销毁重建持久化(dread 旧数据+再 exit)。三判据
同强度换协议栈,其余 28 步原样。

## 7. 基准对齐(#14 收口)

参考实现 virtio(队列协商/异步 DMA/多队列)-> 新仓最小真身落地:
legacy+split+单队列深度 8+零特性位。#14 从"预登记"改"收口",
残余差(协商深度/MSI/packed)记录在案。
