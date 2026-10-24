<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 小红书:virtio 真身站——自制协议退役,数据一个字节不动

## 正文(≤800 字)

第十站:把自己五分钟设计的协议退役,换成 virtio 🔄

先看结局有多舒服:
dwrite → blk wrote
dread → blk read: ESHINE-BLK-persist
**和第九站一字不差**
协议栈整个换了,判据 needle 原封不动——
这叫"判据同强度迁移":行为不变,实现换了 💯

为什么要退役自己的协议?
自制协议五分钟能跑,但**全世界只有你一个用户** 😅
自制 vblk 是给 virtio 打的草稿:
内存命令块 → 三描述符链(header/data/status)
CMD 写一下 → avail 入队 + QueueNotify 门铃
磁盘静态数组 → **磁盘静态数组(原封承接)**
第九站的数据,跨过协议栈更换活到了 virtio 时代

virtio-mmio legacy 寄存器最小集:
✅ probe 三验:magic/version/DeviceID——设备身份是**握手
   出来的,不是假设出来的**
✅ Status 步进:ACK→DRIVER→DRIVER_OK
✅ QueuePFN 交付:整块队列区的物理页帧号
✅ DRIVER_OK 兼任中断使能(没有 IMSC,语义更干净)

本站两个坑,**全在构建期抓获**(零运行时 debug):
①链接器摆放陷阱:`aligned(4096)` 只保对齐不保相邻!三个独立
  静态变量中间可以塞任何 .bss 项,布局公式当场作废
  → 单块 8KB 按公式落位
  **不把结构布局交给链接器摆放** 📐
②宏生成指针配了点号 `.idx`——编译错,指针配箭头 `->`,
  编译期抓获零成本

两站连绿,"首测一次全绿"从偶发变成节奏:
设备站的难点早在六、八站付清,现在按图施工 ✨

门禁 31 步判据同强度迁移双绿 ✅
vSPI 34→35(新号与旧代际区分,LR 通道留考古层)
每晚 1-2h 与 AI 从零造 Type-1,devlog 10 全记录。

## 配图清单

1. 封面:virtio blk on → dwrite/dread → 与 vblk 同 needle 对比
2. 自制 vblk vs virtio 对照表(四行)
3. 三描述符链+avail/used 环布局公式图(三段式)
4. aligned 不保相邻:三个静态变量中间塞 .bss 的示意图
5. make verify 31 步 PASS ×2(判据同强度迁移)

## 标签

#虚拟化 #hypervisor #ARM64 #操作系统 #内核开发 #嵌入式 #QEMU
#AI编程 #从零开始 #eshine-os #virtio #驱动开发
