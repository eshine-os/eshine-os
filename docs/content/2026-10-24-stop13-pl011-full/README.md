<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 第十三站内容包:PL011 完整化站(AMBA 身份证 + 控制寄存器族)

`docs/content/2026-10-24-stop13-pl011-full/` —— 27.04 第十三站(master
站内,2026-10-24 完成)的公开内容底稿,按平台一文件。

| 文件 | 平台 | 形态 |
|---|---|---|
| `zhihu-gongzhonghao.md` | 知乎 / 微信公众号 | 深度长文 |
| `xiaohongshu.md` | 小红书 | 短内容 + 配图 + 标签 |
| `x-thread.md` | X / Twitter | 英文 8 推 |
| `youtube.md` | YouTube(B 站) | 分镜脚本 |

## 统一素材

1. 主视觉:`[vm0] pl011 on: amba 00141011`(身份证八字节自查通过)
2. "设备证明自己是谁"——AMBA 总线靠 PERIPHID/PCELLID 绑驱动,
   没有身份证就没有绑定(0x00141011 / 0xB105F00D)
3. 战况:开机前的输出被 CR 门吞——真语义第一次咬到自家引导顺序,
   修法是引导序回真 OS 形态(pl011 先行,开机后自报),不删门
4. 36 步旧判据一条不掉 = 回归即验收(TX/RX 门下全链照常)
5. make verify 37 步 PASS ×2;三门欠账全消(DTB/GICD/PL011),
   Linux bring-up 就绪

## 统一口径

- 事实:vuart 增 AMBA 身份证(PERIPHID/PCELLID 只读)+控制族
  (CR/IBRD/FBRD/LCR_H/IFLS/DMACR 存储回读,复位值按真芯片)+CR
  真语义(DR 写要 UARTEN|TXE,RX 中断要 UARTEN|RXE);波特率无
  时序不仿真;guest 引导按 Linux pl011 同律(自查→关机→配置→
  开机→自报)
- 主打故事:**设备证明自己是谁**——串口从"自制 guest 的默契子集"
  长成 Linux 驱动可探测可绑定的行业标准形态
- 副线:**真语义咬到自家引导顺序**——关机态写 DR 被门丢弃,连站
  十二的 gic 自报都被吞了;不删门、不改模型,引导序回真 OS 形态
  (Linux 同律:驱动起完设备才有 console)
- 方法论招牌句:**真语义上线,第一件事就是咬自家引导顺序——这
  不是倒退,是模型终于对了**;回归即验收(旧 36 步一条不掉)
- AI 署名:Claude / GLM-5.3 · 系列位:27.04 连载第 13 篇
