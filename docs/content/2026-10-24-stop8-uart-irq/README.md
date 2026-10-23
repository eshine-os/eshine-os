<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 第八站内容包:UART IRQ 注入站(vSPI 33 + 中断驱动 getc)

`docs/content/2026-10-24-stop8-uart-irq/` —— 27.04 第八站(master
e7085bf,2026-10-23/24 完成)的公开内容底稿,按平台一文件。

| 文件 | 平台 | 形态 |
|---|---|---|
| `zhihu-gongzhonghao.md` | 知乎 / 微信公众号 | 深度长文 |
| `xiaohongshu.md` | 小红书 | 短内容 + 配图 + 标签 |
| `x-thread.md` | X / Twitter | 英文 8 推 |
| `youtube.md` | YouTube(B 站) | 分镜脚本 |

## 统一素材

1. 主视觉:`[vm0] uart irq on`(guest 使能 RXIM 自报,判据 needle)
2. **回读正确≠生效**实据:探针行 `INJ33 lr=... rb=...`(LR 回读原样
   但 vIRQ 线不放行,guest 到达 wfi 无向量进入)
3. -d int 空转风暴(guest 量子烧尽仍无输出)
4. post-index 反汇编:`str w1, [x18], #-32`(ISV=0 现场)
5. make verify 28 步 PASS ×2

## 统一口径

- 事实:vuart 增中断面(IMSC 仅 RXIM/RIS 读后重评定/MIS/ICR);
  纯虚拟 vSPI 33 走 LR1(vtimer LR0 分道);guest handler 排空 DR
  入软环;ISR 排空式 + 泵内再唤醒(多字符爆发双修)
- 主打故事:**ICH_LR 位号名实分离**——真实 bit60=Group/bit61=HW/
  bit62=State(pending);第四站 vtimer 三位全置"恰好正确但名字全错";
  照抄错名=置 HW 位缺状态位=LR 无效,QEMU 静默弃投;**回读正确≠生效**
- 副线:post-index 回写型访存(ISV=0,宿主拿不到值只能拒)→
  guest 访存器全改 volatile asm 访问器;等待机制更替弧线收束
  (HVC 风暴→FR 轮询→vSPI 33,每代为上代收尸)
- 方法论招牌句:**回读正确 ≠ 生效;位号以架构手册为准,不以"能跑
  的旧代码"为准**;帧值 ≠ 活体值(承第六站)
- AI 署名:Claude / GLM-5.3 · 系列位:27.04 连载第 8 篇
