<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 第十一站设计:DTB 注入站(Linux 前传第一幕)

日期:2026-10-24 · 版本:27.04-dev · 前站门禁:31 步真绿 ×2

## 0. 决策记录(AskUserQuestion 四问,用户全选推荐)

1. 站点:**DTB 注入站**——guest 从硬编码改吃设备树,Linux 落地战役入口;
   对齐参考实现 DTS 注入形态(去偏离化)。
2. DTB 来源:**dumpdtb 裁剪补丁**——`qemu-system-aarch64 -M virt,dumpdtb=`
   出真描述,裁剪到只含我们提供的设备。
3. 交付方式:**固定偏移拷贝**——vm_create 把 blob 拷进 guest RAM 保留区,
   bootloader 同构语义,零新超调用(HVC 退役弧线不回潮)。
4. 门禁强度:**三节点自查清单**——guest 自报 "fdt ok: cpu / uart / blk",
   设备身份必须从树查出而非硬编码;31→34 步。

## 1. 问题

guest 至今对世界的认知全部硬编码:UART 在 0x09000000、virtio 在
0x0a000000、中断号 33/35、RAM 16MB@0x40000000——都写在链接脚本和
常量里。真 OS(Linux)不吃硬编码,它吃设备树。这一站让"硬件"开始
自描述,为 Linux 前传打地基。

## 2. 方案

### 2.1 树:诚实裁剪(只描述我们提供的)

底稿:`qemu-system-aarch64 -M virt,dumpdtb=qemu-virt.dtb,
virtualization=on,gic-version=3 -cpu cortex-a57 -m 512`(QEMU 8.2.2,
dtc 1.7.0 反编译)。裁剪原则:**删我们不提供的节点,留的节点保真
QEMU 原形**;中断单元格沿用 GIC 三元组语义(SPI N = INTID 32+N)。

保留:memory@40000000(**补丁点:reg 尺寸 512MB→16MB**)、cpus/cpu@0
(a57,phandle/cpu-map 删——无消费者)、timer(**只留虚拟定时器 PPI 11
= INTID 27**,唯一真注入面)、virtio_mmio@a000000(**补丁点:
interrupts SPI 16→SPI 3(=INTID 35,第十站通道);单槽**)、
pl011@9000000(SPI 1 = INTID 33,第八站通道;apb-pclk 时钟节点保留,
pl011 驱动同形)、chosen(stdout-path)。

删除:psci/method(未实现,留站)、fw-cfg、platform-bus、其余 31 个
virtio 槽、pl061、pcie、pl031、pmu、**intc/its(GICD/GICR 尚未仿真,
登记进树=说谎)**、flash、rng/kaslr-seed。

诚实原则的推论:**树里没有 GIC 节点**——interrupts 属性带着标准
三元组先挂上,等 GICD/GICR 仿真站落地再把 intc 节点请回树。
双 VM 共树:树描述 IPA 空间,两 guest 的 IPA 硬件完全一致;per-VM
补丁(model 串/serial-number)等出现真实分歧再上(YAGNI)。

产物:`tools/dtb/eshine-vm.dts`(源,裁剪规则与来源记录在文件头)+
`tools/dtb/eshine-vm.dtb`(dtc 编译,随仓提交,构建不依赖 dtc)。

### 2.2 注入:vm_create 固定偏移拷贝

- 偏移契约:`VM_DTB_OFF = 0xF00000`(guest RAM 16MB 的最后 1MB),
  guest 视角 IPA 0x40F00000;预算 64KB。
- `hypervisor/kernel/dtb.c`:`.incbin "tools/dtb/eshine-vm.dtb"` 直入
  .rodata;vm_create 在 stage-2 建立后 memcpy 到
  `vm_ram_pa[id] + VM_DTB_OFF`(EL2 恒等映射直写),totalsize 校验
  (blob 头 totalsize == 实际尺寸,超预算拒绝 create)。
- 与磁盘持久化同律:DTB 每次创造重注(新树随宿主重启生效),
  不承诺跨 create 保留。

### 2.3 guest:mini-FDT 走查器(只读,~120 行)

`guest/fdt.h`,与 miniuart/virtio 同风格。能力面严格对齐判据:

- 头校验:magic 0xd00dfeed、version >= 16(结构块自 v16 稳定)。
- 结构块深度优先走:BEGIN/END_NODE 配深计,路径匹配
  `/cpus/cpu@0`、`/pl011@9000000`、`/virtio_mmio@a000000`。
- 属性扫描:命中节点内找 `compatible`,取首串(NUL 截断)。
- 引导期自报(先设备初始化、后 "uart irq on",硬件清单先于驱动就绪):

      fdt ok: cpu arm,cortex-a57
      fdt ok: uart arm,pl011
      fdt ok: blk virtio,mmio

  任一环节失败打 `fdt FAIL: <tag>`(门禁必抓)。

## 3. 门禁(31→34,只增不减)

[8] vmrun 0 → banner 后,依次插入:

- [9] `fdt ok: cpu arm,cortex-a57`(树就位+CPU 查出)
- [10] `fdt ok: uart arm,pl011`(串口查出)
- [11] `fdt ok: blk virtio,mmio`(块设备查出)

旧 [9]-[31] 顺延 +3 → [12]-[34]。vm1 同树同报(转写可见,不单设判据)。

**三处同步纪律(本站首次执行)**:verify_el2.py、shell help 走查、
ARCHITECTURE 验证节文字同 commit 更新;编辑用整块重写或断言保护。

## 4. 偏离登记

#15(新):设备树注入——参考实现有 DTS 注入地址(per-domain
manifest,树完整含 GIC 描述)→ 新仓最小诚实树(仅已提供设备,
无 GIC/PCI/flash 节点),双 VM 共树、固定偏移 0x40F00000、
mini 走查器只读。参考源:FDT spec(公开)+ QEMU dumpdtb 描述形态。

## 5. 不做(YAGNI)

- GIC/PSCI/时钟节点:Landing 各自的仿真站时再进树
- per-VM 树差异化、/chosen/bootargs、Linux 兼容核对:Linux bring-up 站
- 篡改树(运行时 patch)/FDT 重排:无消费者
