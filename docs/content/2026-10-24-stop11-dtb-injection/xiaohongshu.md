<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 小红书:DTB 注入站——硬件开始自描述

## 正文(≤800 字)

第十一站:guest 的世界观搬家了 🌳
以前:UART 在哪、中断号几、RAM 多大——全钉死在 guest 链接脚本
现在:**宿主注入一棵设备树,guest 自己查**

引导期自查三行:
fdt ok: cpu arm,cortex-a57
fdt ok: uart arm,pl011
fdt ok: blk virtio,mmio
设备身份从树查出,非硬编码 ✅ 门禁 31→34 步

树怎么来?**QEMU dumpdtb 裁剪**:
一百万字节真描述 → 只留我们提供的设备 → 1057 字节
保留的节点一字不改 QEMU 原形(裁剪非重写)

最有含金量的是**删了什么**:
❌ GIC 节点——GICD/GICR 还没仿真,登记即说谎
❌ psci——没实现就不进树
**没有的设备不进树,树不替未来撒谎** 📜
(裁剪的惊喜:树里 SPI 1/3、PPI 11 恰好对应前几站通道
 INTID 33/35/27——通道编号本来就是按真硬件选的)

本站战况,我自己的十六进制:
0x40000000+0xF00000 = 0x4**0F**00000
我写成 0x4**F**000000(位错了)😅
guest 读树第一拍 data abort,故障机器现场全须全尾打出来
宿主和另一个 VM 纹丝不动 → **隔离面把低级错关门内**
修完一次全绿。十站基建的隐性验收 🛡️

顺带根治了上站的嵌合体事故(help 标题31正文25):
立"三处同步纪律"——门禁+help+ARCHITECTURE 每站同 commit 更新,
门禁重编号用降序防级联+断言自校,help 整块重写 🔧

每晚 1-2h 与 AI 从零造 Type-1,devlog 11 全记录。

## 配图清单

1. 封面:三行 fdt ok 自查特写
2. 1MB 底稿 → 1057B 诚实树 裁剪对比图
3. fault 现场(十六进制战况)vs 宿主照常运行对比
4. 中断三元组对齐表(SPI1/3 PPI11 ↔ 33/35/27)
5. make verify 34 步 PASS ×2

## 标签

#虚拟化 #hypervisor #ARM64 #操作系统 #内核开发 #嵌入式 #QEMU
#AI编程 #从零开始 #eshine-os #设备树 #Linux
