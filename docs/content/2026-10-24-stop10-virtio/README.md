<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 第十站内容包:virtio 真身站(virtio-mmio legacy + split virtqueue)

`docs/content/2026-10-24-stop10-virtio/` —— 27.04 第十站(master
dee6956,2026-10-24 完成)的公开内容底稿,按平台一文件。

| 文件 | 平台 | 形态 |
|---|---|---|
| `zhihu-gongzhonghao.md` | 知乎 / 微信公众号 | 深度长文 |
| `xiaohongshu.md` | 小红书 | 短内容 + 配图 + 标签 |
| `x-thread.md` | X / Twitter | 英文 8 推 |
| `youtube.md` | YouTube(B 站) | 分镜脚本 |

## 统一素材

1. 主视觉:`[vm0] virtio blk on`(probe 三验→Status 步进→队列就位
   →DRIVER_OK)→ `dwrite` → `blk wrote` → `dread` → 同 needle
2. 协议栈更换、数据不动的迁移实据:自制 vblk 退役,virtio-blk
   承接同一静态磁盘,`blk wrote`/`blk read: ESHINE-BLK-persist`
   needle 原封不动
3. 三描述符链图:header(type/sector)→ data(512B)→ status
   (设备回写),avail 入队 → QueueNotify 门铃 → used 环 append
4. 链接器摆放陷阱:三个独立 `aligned(4096)` 静态变量不保证
   相邻落位 → 单块 8KB 区域公式落位
5. make verify 31 步 PASS ×2(判据同强度迁移)+ "第二座首测
   一次全绿"

## 统一口径

- 事实:virtio-mmio legacy 寄存器最小集(magic 0x74726976/
  version 2/DeviceID 2/VendorID 0x4553/QueueNumMax 8/QueuePFN/
  QueueNotify/InterruptStatus-ACK/Status);split virtqueue legacy
  布局公式(desc 16N/avail 紧随/used 4096 对齐);DRIVER_OK 兼任
  中断使能;完成中断 vSPI 35(LR3);宿主与 guest 两侧自洽
- 主打故事:**自制协议退役,行业标准接棒**——"自制 vblk 只是
  给 virtio 打的草稿";迁移即结构连续:同一静态磁盘,数据一个
  字节不动,guest 输出 needle 原封不动;判据同强度迁移(门禁
  只增不减的最优雅姿势)
- 副线一:**链接器摆放陷阱**——结构布局不能交给链接器;`aligned`
  只保对齐不保相邻,三个独立静态变量中间可以塞进任何 .bss 项;
  布局公式必须落在同一块物理连续区上
- 副线二:probe 三验——guest 先验证"你是不是 virtio"(magic/
  version/DeviceID)再协商;Status 步进(ACK→DRIVER→DRIVER_OK)
  是双向握手的最小实现
- 方法论招牌句:**协议可以换,结构不必换;迁移的最高境界是
  判据原封不动**;不把结构布局交给链接器摆放
- 战况:两处全是构建期抓获(链接器摆放+宏展开优先级),零运行时
  debug——第二座"首测一次全绿",设备站进入"按图施工"节奏
- AI 署名:Claude / GLM-5.3 · 系列位:27.04 连载第 10 篇
