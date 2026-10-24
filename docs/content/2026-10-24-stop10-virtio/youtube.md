<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# YouTube / B 站分镜:virtio 真身站(自制协议退役,数据一个字节不动)

## 标题候选

- 中文(B 站):从零写 Type-1 虚拟化 #10:退役自制协议换 virtio——
  数据一个字节不动,门禁判据原封迁移(附:链接器摆放陷阱)
- EN:From-scratch Type-1 Hypervisor #10: Retiring My Own Protocol
  for Real virtio — migration with zero data movement

## 描述

第十站:virtio 真身(virtio-mmio legacy + split virtqueue +
virtio-blk)。自制 vblk 退役,同一静态磁盘原封承接;门禁判据
同强度迁移(blk wrote/blk read needle 原封不动);probe 三验/
Status 步进/QueuePFN 交付/DRIVER_OK 兼任中断使能;三描述符链
(header→data→status)+ avail/used 环。两个战况全在构建期:
链接器摆放陷阱(aligned 只保对齐不保相邻→单块 8KB 公式落位)、
宏展开优先级(指针宏配箭头)。第二座首测一次全绿。门禁 31 步
判据同强度迁移双绿。代码:github.com/eshine-os/eshine-os ·
www.eshine-os.org。每晚 1-2h,AI 结对,从零造 Type-1。

## 标签

B 站:虚拟化 hypervisor ARM64 操作系统 内核 嵌入式 QEMU AI编程
virtio 驱动开发 从零开始 eshine-os
YouTube:hypervisor arm64 virtualization osdev type-1 qemu virtio
virtqueue driver from scratch

## 分镜脚本(约 12 分钟)

| # | 时段 | 画面 | 口播/字幕要点 |
|---|---|---|---|
| 1 | 0:00-0:55 | 录屏:virtio blk on → dwrite/dread → 与第九站输出对比 | 钩子:协议栈换了,输出一字不差;迁移哲学——数据一个字节不动 |
| 2 | 0:55-2:40 | 对照表+代码:vblk 映射 virtio | 为什么要退役自制协议(唯一用户);"内存即传输面,寄存器即门铃"本就是 virtqueue 哲学;磁盘数组原封承接 |
| 3 | 2:40-4:40 | 握手时序图:probe 三验/Status/QueuePFN | 设备身份握手出来非假设出来;Status 步进双向可见;QueuePFN=物理页帧号交付;DRIVER_OK 兼使能(比 PL011 干净) |
| 4 | 4:40-6:40 | 布局公式三段图+代码 | split virtqueue:desc 16N/avail 紧随/used 4096 对齐;三描述符链角色分工;宿主 guest 两侧同一公式表,一侧造链一侧拆链 |
| 5 | 6:40-8:40 | 重点:.bss 布局图(三个静态变量中间塞 .bss 项) | 链接器摆放陷阱:aligned 只保对齐不保相邻;公式作废是静默的;修法=单块 8KB 公式落位;普适教训"不把结构布局交给链接器" |
| 6 | 8:40-9:30 | 编译错误截图 | 宏展开优先级:指针宏配点号=编译错;零运行时成本的构建期抓获 |
| 7 | 9:30-10:50 | 录屏:五链全过+门禁 diff | 第二座首测一次全绿;判据同强度迁移(needle 原封);门禁只增不减的最优雅姿势;九/十连绿=按图施工节奏 |
| 8 | 10:50-12:00 | make verify 31 步 + 尾板 | vSPI 34→35 代际考古层;已知限制(#14:零协商/深度 8/静态直访);下一站预告;仓库+站点 |

## 录屏清单

1. virtio blk on + dwrite/dread 全链(分镜 1/7)
2. vblk vs virtio 对照表(分镜 2,静态图)
3. probe 握手时序图+布局公式三段图(分镜 3/4,静态图)
4. .bss 布局示意图(分镜 5,静态图)
5. make verify 31 步 ×2 + 判据迁移 diff(分镜 8)
