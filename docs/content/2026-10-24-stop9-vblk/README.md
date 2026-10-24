<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 第九站内容包:vblk 块设备站(门铃 + 完成中断 + 持久化)

`docs/content/2026-10-24-stop9-vblk/` —— 27.04 第九站(master
站内,2026-10-24 完成)的公开内容底稿,按平台一文件。

| 文件 | 平台 | 形态 |
|---|---|---|
| `zhihu-gongzhonghao.md` | 知乎 / 微信公众号 | 深度长文 |
| `xiaohongshu.md` | 小红书 | 短内容 + 配图 + 标签 |
| `x-thread.md` | X / Twitter | 英文 8 推 |
| `youtube.md` | YouTube(B 站) | 分镜脚本 |

## 统一素材

1. 主视觉:`[vm0] blk irq on` → `dwrite` → `blk wrote` → `dread` →
   `blk read: ESHINE-BLK-persist` → 销毁重建 → `dread` 旧数据仍在
2. "内存即传输面,寄存器即门铃"哲学句(virtqueue 种子)
3. 持久化=结构保证:磁盘静态数组不在 vm_create 复位路径上
4. make verify 31 步 PASS ×2(28→31,块设备三判据入册)
5. "首测一次全绿"战报(与第十站构成"红利期"双连)

## 统一口径

- 事实:guest 首次长出"存储";512B 数据缓冲在 guest 自有 RAM,
  宿主经 mem[] 静态翻译**首次直访 guest 物理内存**(EL2 恒等
  映射);MMIO 只承载门铃/状态五字;同步服务+异步完成中断
  (vSPI 34);跨 VM 生命周期持久化;设备表化(vm_device.dev
  分发,单设备直查欠账到期)
- 主打故事:**设备模型的完整生命周期闭环**——请求→服务→完成
  →ISR,一次门铃陷阱走完全程;而"销毁重建数据仍在"不是代码
  逻辑,是**数据放对了地方**(静态数组不在复位路径上=结构保证)
- 副线:第二个设备落地逼出设备表化;等待原因泛化"设备中断等待";
  泵注入条件 uart||blk 并列,LR0=vtimer(HW)/LR1=vuart/LR2=vblk
  恰好用满 a57 四槽之三
- 方法论招牌句:**内存即传输面,寄存器即门铃**;持久化靠结构,
  不靠代码
- 战况:本站**无战况但有时间线解释**——八站的怀疑论全在这站
  变红利(第一座"首测一次全绿")
- AI 署名:Claude / GLM-5.3 · 系列位:27.04 连载第 9 篇
