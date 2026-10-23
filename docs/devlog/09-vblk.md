<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# Devlog 09 — vblk 块设备站（门铃 + 完成中断 + 持久化）

日期:2026-10-24 · 版本:27.04-dev · 门禁:31 步真绿 ×2 连过

## 里程碑

第二个仿真设备：RAM 磁盘 vblk。guest 从"只有终端"长出了"存储"：

    [vm0] blk irq on                       ← guest 使能完成中断后自报
    vm0> dwrite                            ← 模式串入缓冲 → 门铃
    blk wrote                              ← 完成中断(vSPI 34)唤醒后回报
    vm0> dread
    blk read: ESHINE-BLK-persist           ← 数据完整往返
    （vm0 exit 销毁 → vmcreate 0 重建）
    vm0> dread
    blk read: ESHINE-BLK-persist           ← 上个生命周期的数据还在

## 形态（用户四决策全选推荐）

- **内存即传输面，寄存器即门铃**——virtqueue 的种子。512B 数据缓冲
  在 guest 自有 RAM（它是唯一写者），宿主经 mem[] 静态映射翻译
  **首次直访 guest 物理内存**（EL2 恒等映射，PA 即宿主可访地址）；
  MMIO 只承载门铃/状态（CMD/ADDR/STATUS/CAP/IMSC 五个字）。
- **同步服务 + 异步完成**：RAM 介质没有真实延迟，数据搬运在门铃
  陷阱内当场完成；异步的只有完成中断——ris+imsc → 泵注 LR2
  vSPI 34 → guest ISR 读 STATUS 清挂起。**请求→服务→完成→ISR**，
  设备模型的完整生命周期闭环，virtio 同构的最小形态。
- **持久化=结构保证**：磁盘是 vblk.c 里的静态数组，根本不在
  vm_create 的复位路径上——"销毁重建数据仍在"不是代码逻辑，
  是数据放错了（放对了）地方。门禁 [25] 走全生命周期验证。

## 设备表化

第二设备落地，单设备直查的欠账自然到期：vm_device 增 dev 字段，
窗口命中后按 VM_DEV_VUART/VM_DEV_VBLK 分发；泵注入条件 uart||blk
并列（LR0=vtimer HW/LR1=vuart/LR2=vblk，a57 四槽刚好）；等待原因
泛化为"设备中断等待"（非 TIMER 源共用再唤醒网）。表内未知设备类型
=不变量破坏，直接 fault——默认拒绝贯穿到设备层。

## 首测一次全绿（本站无战况，但有原因）

八站的积累全兑现在这站：MMIO 分发（六）、LR 正形与泵注入纪律
（八）、asm 访存器契约（八）、设备等待语义（六七八演化）、帧尾
扩展纪律（五）。五链手测（使能/写全链/读完整/销毁/重建持久化）
一次通过——**前一站在怀疑论上花的每一分钟，都在这一站的红利里
回收**。

## 已知限制（登记 #14）

- 单扇区同步服务，无队列深度/协商/多请求（virtio 站补）
- 静态映射直访依赖 EL2 恒等映射（真板 host MMU 站后需 VA 窗口）
- 磁盘容量 64KB/VM（演示粒度）
