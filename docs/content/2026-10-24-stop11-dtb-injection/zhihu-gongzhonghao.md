<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 硬件开始自描述:给 guest 一棵诚实的设备树

> 27.04 连载第 11 篇。前十一站的 guest 对世界的认知全写在常量里:
> UART 在 0x09000000、virtio 在 0x0a000000、中断号 33/35、RAM 16MB
> ——都钉死在链接脚本。这一站把这些搬进一棵设备树,由宿主注入。
> Linux 前传的第一块地基,也是这趟"从零造 Type-1"第一次开始
> 长得像一台真机器。

## 先说结局

    [vm0] fdt ok: cpu arm,cortex-a57
    [vm0] fdt ok: uart arm,pl011
    [vm0] fdt ok: blk virtio,mmio

guest 引导期自查三行——CPU、串口、块设备的身份,全部从宿主注入
的设备树里**查出来**的,不是编译期常量。门禁 31→34 步双绿。

## 树从哪来:dumpdtb 裁剪补丁

不是手写。QEMU 的 virt 机型能直接吐出它自己的设备树:

    qemu-system-aarch64 -M virt,dumpdtb=qemu-virt.dtb ...
    dtc -I dtb -O dts qemu-virt.dtb

一百万字节的真描述在手,裁剪原则只有一条:**只描述我们提供的
设备**。保留 memory/cpus/timer/virtio_mmio/pl011/chosen,删 psci/
fw-cfg/pcie/flash/其余 31 个 virtio 槽……保留的节点一字不改 QEMU
原形(pl011 的时钟 phandle、primecell 串全保真),两个补丁点都有据:
memory 尺寸 512MB→16MB(我们真的只给 16MB)、virtio 槽 IRQ
SPI 16→SPI 3(我们的设备真号,INTID 35,第十站通道)。

裁完后有个惊喜:树里的中断三元组 SPI 1、SPI 3、PPI 11,恰好与
前几站设备通道 INTID 33、35、27 一一对上——六七八十站的通道
编号本来就是按 QEMU 真硬件对齐选的,现在树把这些约定显式化了。

## 裁剪的第一原则是诚实

最有含金量的决定是**删掉了什么**:GIC 节点没进树。

树是给未来 Linux 看的硬件说明。Linux 的 gic-v3 驱动按树找到 GICD
初始化中断——但我们**尚未仿真 GICD/GICR**(当前 guest 的中断经
GIC 列表寄存器直接注入,不走过 distributor)。把 intc 节点写进树,
等于替一个不存在的服务打广告。psci 同理:没实现的功能,树里
就不许有。

**没有的设备不进树;树不替未来撒谎。** GICD/GICR 仿真站落地那天,
intc 节点再请回树——那时它是真的。

## 注入与走查

交付方式:vm_create 时把 blob 整拷进 guest RAM 固定偏移(0x40F00000,
16MB 内存的最后 1MB)——bootloader 同构语义,零新超调用,HVC
退役的弧线不回潮。头校验(magic/totalsize)+64KB 预算,坏 blob
拒绝创建。

guest 侧一个 ~120 行的只读走查器:字节游标走结构块(大端 u32
令牌流),路径深度匹配找节点,读 compatible 首串自报。游标以
size_dt_struct 兜底——坏 blob 不许拖 guest 空转烧量子。

## 战况:一个,我自己的十六进制

首版 FDT_ADDR 我写成 0x4F000000。0x40000000+0xF00000 的正确算术
是 **0x40F00000**——我位错了。guest 读树第一拍:data abort,
故障机器把现场打了个全须全尾:

    fault: ESR=... FAR=0x4f000000 HPFAR=... pc=... x3=0x4f000000
    vm0: faulted -> VM_DEAD

宿主与另一个 VM 纹丝不动,门禁前 8 步照常。**隔离面把我的低级错
关在了门内**——修完地址一次全绿。十站基建的隐性验收:一个
十六进制手误,烧掉的只是一个租户。

值得反思的是:如果这个手误落在宿主侧(比如拷贝目标算错),就
没有第二道门了。权限纪律的价值不在你犯错时,在你确信自己
不会犯错时。

## 顺带:嵌合体事故根治

上一站用户抓出 help 走查"标题 31、正文停在 25 步时代"(replace
无断言、基底漂移静默失败)。本站立"三处同步纪律"并首次执行:
门禁脚本降序重编号防级联+步号断言自校、help 整块重写、
ARCHITECTURE 验证节文字同批。34 步三处全对齐,肉眼复验。

## 数字

- 门禁 31 → **34 步双绿**(+树就位/串口/块设备三节点自查)
- 树 1057 字节(v17)8 节点;底稿 1048576 字节 391 行裁剪而来
- 已知限制(登记 #15):无 GIC/psci 节点(对应站落地再进树)、
  双 VM 共树、无 bootargs/runtime patch

代码、devlog 11:github.com/eshine-os/eshine-os · www.eshine-os.org。
每晚 1-2 小时,与 AI 结对,从零造 Type-1。

### 平台微调说明

- **知乎**:全文可发;"树不替未来撒谎"+"隔离面关门内"双传播点
- **公众号**:dumpdtb 裁剪对比图(1MB→1KB);十六进制战况配 fault 现场
- **B 站**:见 youtube.md,诚实裁剪 + 十六进制事故做双分镜
