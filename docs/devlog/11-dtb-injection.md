<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# Devlog 11 — DTB 注入站（设备树自描述,Linux 前传第一幕）

日期:2026-10-24 · 版本:27.04-dev · 门禁:34 步真绿 ×2 连过（全清重建零警告）

## 里程碑

guest 对世界的认知不再写在常量里——硬件开始自描述:

    [vm0] eshine-guest VM0 @EL1 via stage-2 (vuart @trap)
    [vm0] fdt ok: cpu arm,cortex-a57
    [vm0] fdt ok: uart arm,pl011
    [vm0] fdt ok: blk virtio,mmio
    [vm0] uart irq on
    [vm0] virtio blk on

三行自查来自宿主注入的设备树:vm_create 把 FDT blob 整拷进 guest RAM
固定偏移(bootloader 同构语义),guest 用 120 行的只读走查器把
CPU/串口/块设备的身份从树里查出来。Linux 前传的第一块地基落定。

## 形态(用户四决策全选推荐)

- **dumpdtb 裁剪补丁**:`qemu-system-aarch64 -M virt,dumpdtb=` 出
  QEMU 真描述,裁剪到只含我们提供的设备——**保留节点保持 QEMU
  原形**(pl011 时钟/primecell 串全保真),中断三元组沿用 GIC 语义
  (SPI 1=INTID 33、SPI 3=INTID 35,PPI 11=INTID 27 恰与六七八十站
  的通道编号一一对上)。两个补丁点:memory 尺寸 512MB→16MB、
  virtio 槽 IRQ SPI 16→SPI 3(我们的单槽真号)。
- **诚实原则的推论**:树里**没有 GIC 节点**——GICD/GICR 尚未仿真,
  登记进树=说谎;psci 同理(留站)。等各自的仿真站落地再请回树。
- **固定偏移拷贝**:blob incbin 入宿主 .rodata,vm_create 整拷到
  `vm_ram_pa[id]+0xF00000`(guest 视角 0x40F00000,最后 1MB);
  magic/totalsize 头校验+64KB 预算,坏 blob 默认拒绝 create。
  零新超调用,HVC 退役弧线不回潮。
- **双 VM 共树**:树描述 IPA 空间,两 guest 的 IPA 硬件完全一致
  ——per-VM 补丁(model 串等)等出现真实分歧再上(YAGNI)。

## 战况:一个,我自己的十六进制(0x4F vs 0x40F)

首版 FDT_ADDR 写成 0x4F000000——0x40000000+0xF00000 的正确算术是
**0x40F00000**,我位错写成 0x4F00000(未映射,差 0xF000000)。
guest 读树第一拍即 data abort,故障机器现场完整(FAR/HPFAR/pc/x0 全
打),平滑隔离死。值得记的不是低级,是**隔离面把这个低级错关在了
门内**:宿主与其他 VM 纹丝不动,门禁 32 步(当时的旧步号)前 8 步
照常。修完地址一次全绿。

另:`-M virt,dumpdtb=file` 是 machine 级选项(8.2.2),不是全局
`-dumpdtb`——考古花了三分钟,记档。

## 嵌合体事故的根治落地

本站首次执行**三处同步纪律**(verify+help+ARCHITECTURE 同站同批):

- 门禁脚本:降序重编号 9-31→12-34(防级联)+插入三步+全局 /31→/34,
  步号 1..34 顺序唯一由脚本断言自校;
- help 走查:整块重写(不再用替换打补丁);
- ARCHITECTURE:验证节判据文字+新设"设备树注入"节,Makefile 注释
  同步。

## 数字

- 门禁 31 → **34 步双绿**(+三节点自查:树就位/路径匹配/compatible
  读出,设备身份非硬编码)
- 树 1057 字节(v17),8 节点;mini-FDT 走查器 ~120 行只读
- QEMU dumpdtb 底稿 391 行 1048576 字节 → 裁剪 2376 行 dts(含注释)
- 已知限制(登记 #15):无 GIC/psci 节点(对应仿真站落地再进树)、
  双 VM 共树、无 bootargs/runtime patch
