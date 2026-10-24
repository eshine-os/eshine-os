<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 第十二站设计:vgic GICD/GICR 仿真站(中断面 OS 化)

日期:2026-10-24 · 版本:27.04-dev · 前站门禁:34 步真绿 ×2

## 0. 决策记录

用户 2026-10-24 令"你继续你的下一站任务"——按前报推荐序执行:
**GICD/GICR 仿真站先行**(DTB 已就位,树里缺席的 intc 节点是 Linux
gic-v3 驱动的硬前提)。设计决策按站规 YAGNI/诚实原则自决,沿用户
历站一贯的全选推荐口径。

## 1. 问题

guest 的中断至今"无 distributor":宿主直接往 LR 注入,guest 侧
只开 ICV 系统寄存器面。真 OS 不是这么活的——Linux 的 gic-v3
驱动要按设备树找到 GICD/GICR,**经 MMIO 探测(PIDR2)、开 CTLR、
清 WAKER 睡眠、经 ISENABLER 逐 INTID 使能**,才认这套中断面。
第十一站的树因"未仿真不登记"而没有 intc 节点——本站把它造真,
然后请回树。

## 2. 方案

### 2.1 窗口与形态(镜像 QEMU virt 真布局)

- **vGICD**:IPA 0x08000000,0x10000。**vGICR**:IPA 0x080A0000,
  0x20000(RD 帧 + SGI 帧,单 vCPU 一帧)。两窗口均 EMULATED
  (不映射,访问即陷阱);devices[2]/[3] 正好用满 ESHINE_MAX_DEV=4。
- 寄存器语义(offset 以 ARM 手册为准,KB 复核):
  - GICD:CTLR 0x000(Enable 位存储+RWP 恒 0,写即完成)/TYPER
    0x004(ITLinesNumber=1,支持 INTID 32-63)/IIDR 0x008/
    IGROUPR 0x080+4n/ISENABLER 0x100+4n(**写置位**语义)/
    ICENABLER 0x180+4n/ISPENDR·ICPENDR·ISACTIVER·ICACTIVER
    0x200-0x3C0/IPRIORITYR 0x400+4n(字节粒度)/ICFGR 0xC00+4n/
    **PIDR2 0xFFE8 = 0x30**(GICv3 判据,(x&0xf0)==0x30)。
  - GICR RD:CTLR 0x000/IIDR 0x004/**TYPER 0x008**(亲和 0,
    ProcessorNumber=0,Last=bit4)/WAKER 0x014(存储回读,
    清睡眠即零,Linux 轮询立即过)。
  - GICR SGI(+0x10000):IGROUPR0 0x080/**ISENABLER0 0x100**
    (INTID 0-31,PPI 27=bit27)/ICENABLER0 0x180/ISPENDR0 0x200/
    ICPENDR0 0x280/ISACTIVER0 0x300/ICACTIVER0 0x380/
    IPRIORITYR0-7 0x400/ICFGR0 0xC00/ICFGR1 0xC04(PPI 27→
    bit[55:54] level=0,与树里 PPI11 声明一致)。
  - **未知寄存器 RAZ/WI**(设备级默认拒绝,承 vblk 越界置 err 先例)。
- 状态 per-VM(struct vgic 内嵌 eshine_vm),vm_create 复位——
  与磁盘持久化互补:distributor 状态随 VM 生命周期,不跨 create。

### 2.2 注入门控(distributor 成为权威)

泵注入三通道全部改为"guest 使能了才注入":

- vuart SPI 33:`GICD_ISENABLER0 bit1 && vuart_rx_irq_ready`
- virtio SPI 35:`GICD_ISENABLER0 bit3 && virtio_irq_ready`
- vtimer PPI 27:`GICR_ISENABLER0 bit27`(WOKE 注 LR0 前)

guest 未使能而事件就绪:不注入、不代完成 WFI——guest 自己的配置
自己负责(隔离面哲学)。再唤醒网同律加门。

### 2.3 guest 真启动式初始化(volatile asm 访存器契约)

guest init 从"只开 ICV"长成真 OS 形态(约 10 次陷阱):

    PIDR2 探测(判 0x30)→ "gicd v3 ok"
    GICD_CTLR 置 1;GICR_WAKER 清零并回读轮询
    GICR_SGI:IGROUPR0 全 1 → ISENABLER0 置 PPI 27
    GICD:IGROUPR0 全 1 → ISENABLER0 置 SPI 33+35
    → "gic on: 27 33 35"
    (后接既有 ICV 使能,原路径不动)

门禁判据两条(34→36):`[vm0] gicd v3 ok` + `[vm0] gic on: 27 33 35`。
既有 34 步全保——全部中断判据改经门控注入后照常绿。

### 2.4 intc 节点请回树

    gic: intc@8000000 {
        compatible = "arm,gic-v3";
        #redistributor-regions = <0x01>;
        #interrupt-cells = <0x03>;
        interrupt-controller;
        reg = <GICD 0x10000, GICR 0x20000>;
    };root: interrupt-parent = <&gic>

**不加**:its 子节点、maintenance interrupts 属性——LPI/ITS 未仿真,
诚实原则不动(偏差 #15 的 GIC 空缺本站收口,psci 仍留)。

## 3. 三处同步 + 内容包(纪律执行)

门禁降序重编号 12-34→14-36+断言自校;help 整块重写(36 步);
ARCHITECTURE 验证节+中断面节;内容包 5 文件随站(新规)。

## 4. 偏离登记

#16:vgic distributor/redistributor 面——参考实现完整 vgic(gic_ops
版本分发表/LR 满兜底/maintenance 链)→ 新仓最小诚实面(启动+使能
路径真语义,其余存储型 RAZ/WI;泵注入按使能位门控;无 LPI/ITS/
1-of-N 路由)。#8 的"仅 vtimer 单 LR"记录随之演进;#15 的
intc-空缺收口。

## 5. 不做(YAGNI)

- LPI/ITS/MSI:真消费者出现再仿真
- 1-of-N 路由/IROUTER:单 vCPU 无路由面
- GICD_TYPER 全字段位考古(LPI/ESPI/安全扩展位):Linux bring-up
  站按需补
- GICR_SYNCR/MPAM 分区/直接注入(v4):无硬件需求
