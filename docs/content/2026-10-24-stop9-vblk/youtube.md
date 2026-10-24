<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# YouTube / B 站分镜:vblk 块设备站(内存即传输面,寄存器即门铃)

## 标题候选

- 中文(B 站):从零写 Type-1 虚拟化 #9:guest 长出存储——门铃+完成
  中断+跨生命周期持久化(本站零 debug,原因值得听)
- EN:From-scratch Type-1 Hypervisor #9: The Guest Grows Storage —
  doorbell, async completion, and persistence as a structural guarantee

## 描述

第九站:RAM 磁盘 vblk。接口哲学"内存即传输面,寄存器即门铃"
(512B 缓冲在 guest 自有 RAM,宿主首次直访 guest 物理内存,MMIO
只承载门铃/状态五字);同步服务+异步完成中断(vSPI 34),请求→
服务→完成→ISR 完整生命周期闭环;持久化=结构保证(磁盘静态数组
不在 vm_create 复位路径上,零保持代码);第二设备落地逼出设备
表化。本站首测一次全绿——八站的怀疑论在这站兑付红利。门禁
28→31 步双绿。代码:github.com/eshine-os/eshine-os ·
www.eshine-os.org。每晚 1-2h,AI 结对,从零造 Type-1。

## 标签

B 站:虚拟化 hypervisor ARM64 操作系统 内核 嵌入式 QEMU AI编程
virtio 存储 从零开始 eshine-os
YouTube:hypervisor arm64 virtualization osdev type-1 qemu virtio
block device from scratch

## 分镜脚本(约 11 分钟)

| # | 时段 | 画面 | 口播/字幕要点 |
|---|---|---|---|
| 1 | 0:00-0:45 | 录屏:dwrite/dread → 销毁重建 → 旧数据仍在 | 钩子:guest 第一次有"记性";预告本站零 debug 的原因 |
| 2 | 0:45-3:00 | 切面图+代码:guest RAM 数据面 vs MMIO 五字 | "内存即传输面,寄存器即门铃":唯一写者=无信任问题;宿主 mem[] 静态翻译首次直访 guest 物理内存;不逐字陷阱搬运 |
| 3 | 3:00-5:00 | 代码:门铃陷阱→搬运→置挂起→泵注→ISR | 完整生命周期闭环:同步服务(RAM 无延迟)+异步完成;请求→服务→完成→ISR 与 virtio 同构 |
| 4 | 5:00-6:40 | 代码:vm_create 复位路径 vs vblk.c 静态数组 | 持久化=结构保证:磁盘不在复位路径上;零保持代码;"放对地方的东西不需要维护" |
| 5 | 6:40-8:00 | 代码:vm_device.dev 分发 | 第二设备逼出表化:单设备直查欠账到期;未知类型=fault,默认拒绝到设备层;LR 三槽编号化 |
| 6 | 8:00-9:40 | 时间线动画:六/八站坑 → 本站红利 | "首测一次全绿"归因:MMIO 分发/ELR 两面孔/SRT 回填/LR 正形/asm 访存器——怀疑论的复利在设备站兑付 |
| 7 | 9:40-10:30 | 录屏:五链全过 | 使能/写全链/读完整/销毁/重建持久化一次通过;门禁 28→31 三判据入册 |
| 8 | 10:30-11:00 | 尾板 | 下一站预告:种子长成 virtio 真身;仓库+站点 |

## 录屏清单

1. dwrite/dread/销毁重建全链(分镜 1/7)
2. guest RAM vs MMIO 切面图(分镜 2,静态图)
3. vm_create 复位路径 vs 静态数组标注(分镜 4,静态图)
4. make verify 31 步 ×2(分镜 8)
