<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# YouTube / B 站分镜:DTB 注入站(硬件开始自描述)

## 标题候选

- 中文(B 站):从零写 Type-1 虚拟化 #11:给 guest 一棵诚实的设备树
  ——没有的设备不进树(附:我的十六进制事故被隔离面关门内)
- EN:From-scratch Type-1 Hypervisor #11: An Honest Device Tree —
  the hardware starts to describe itself

## 描述

第十一站:DTB 注入(Linux 前传第一幕)。树源 = QEMU dumpdtb 裁剪
补丁(1MB→1057B,保留节点保真原形,补丁点有据);裁剪第一原则 =
诚实:GICD/GICR 未仿真,intc 节点不进树;psci 同理。注入 =
vm_create 整拷 guest RAM 固定偏移(bootloader 同构,零超调用);
guest ~120 行 mini-FDT 走查器三节点自查。战况:十六进制位错
(0x4F000000 vs 0x40F00000)被隔离面关门内。门禁 31→34 双绿;
嵌合体事故根治,三处同步纪律首次执行。代码:github.com/eshine-os/
eshine-os · www.eshine-os.org。每晚 1-2h,AI 结对,从零造 Type-1。

## 标签

B 站:虚拟化 hypervisor ARM64 操作系统 内核 嵌入式 QEMU AI编程
设备树 Linux 从零开始 eshine-os
YouTube:hypervisor arm64 virtualization osdev type-1 qemu devicetree
fdt from scratch

## 分镜脚本(约 11 分钟)

| # | 时段 | 画面 | 口播/字幕要点 |
|---|---|---|---|
| 1 | 0:00-0:45 | 录屏:三行 fdt ok 自查 | 钩子:guest 的世界观从常量搬进一棵树;预告"裁剪第一原则" |
| 2 | 0:45-3:00 | 录屏:dumpdtb/dtc/dts 全流程 | 树从哪来:QEMU 吐真描述→反编译→裁剪;保留节点一字不改(裁剪非重写);两个补丁点:memory 尺寸、virtio IRQ 真号 |
| 3 | 3:00-4:40 | 重点:dts 源码标注"删除区" | 诚实原则:GIC 节点为何不进树(没仿真=登记即说谎);psci 同理;"没有的设备不进树";惊喜:中断三元组恰与前站通道对上 |
| 4 | 4:40-6:20 | 代码:dtb.c incbin + vm_create 拷贝 | 注入:固定偏移 0x40F00000,bootloader 同构,零超调用;头校验+预算,坏 blob 拒绝 create;双 VM 共树 |
| 5 | 6:20-8:00 | 代码:guest fdt.h 走查器 | 120 行只读解析:字节游标/大端令牌/路径深度匹配;size_dt_struct 兜底——坏 blob 不许拖 guest 烧量子 |
| 6 | 8:00-9:30 | 录屏:fault 现场回放 | 战况:0x4F vs 0x40F 位错;读树即 abort,现场全须全尾;宿主/他 VM 纹丝不动;隔离面关门内——基建的隐性验收 |
| 7 | 9:30-10:20 | 代码:门禁重编号 diff | 嵌合体根治:三处同步纪律(门禁+help+ARCHITECTURE 同 commit);降序重编号防级联+断言自校 |
| 8 | 10:20-11:00 | make verify 34 步 + 尾板 | 门禁判据三节点自查;下一站预告:GICD/GICR 仿真(intc 节点请回树的那天);仓库+站点 |

## 录屏清单

1. 三行 fdt ok + 交互(分镜 1)
2. dumpdtb→dtc→裁剪流程(分镜 2,终端录屏)
3. dts 保留/删除标注图(分镜 3,静态图)
4. fault 现场回放(分镜 6)
5. make verify 34 步 ×2(分镜 8)
