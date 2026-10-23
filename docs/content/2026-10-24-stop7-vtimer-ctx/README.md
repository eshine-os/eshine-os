<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 第七站内容包:vtimer ctx 站(per-VM 上下文 + 多等待者)

`docs/content/2026-10-24-stop7-vtimer-ctx/` —— 27.04 第七站(master
e539bd6,2026-10-23/24 完成)的公开内容底稿,按平台一文件。

| 文件 | 平台 | 形态 |
|---|---|---|
| `zhihu-gongzhonghao.md` | 知乎 / 微信公众号 | 深度长文 |
| `xiaohongshu.md` | 小红书 | 短内容 + 配图 + 标签 |
| `x-thread.md` | X / Twitter | 英文 8 推 |
| `youtube.md` | YouTube(B 站) | 分镜脚本 |

## 统一素材

1. 主视觉:`vm0> sleep 4` → `vm1> sleep 2` → **[vm1] 先 slept,vm0 后 slept**
   (双睡醒序,门禁 [19] 位置断言的原始输出)
2. 表序假唤醒现场:首跑输出 `[vm0] slept` 在 `[vm1] slept` 之前(反例实据)
3. 帧尾偏移注释截图(vtimer_ctl@400/vtimer_cval@408,asm 不触及)
4. 每拍扫描代码截图(纯表逻辑,与 27 归属共用谓词)
5. make verify 27 步 PASS ×2

## 统一口径

- 事实:切换对(切入还本人/切出即存,a57 v8.0 无 CNTV 陷阱位,参考实现同构)
  + 每拍到期扫描(物理恒为"当前客"的,别家扫描兜住);CNTVOFF 共享 0
  (双 guest 同墙钟,正确简化)
- 主打故事:**表序找人的最后一次复发**(双等待者下 27 被领给 cval
  未到的先武者=假唤醒)——按 cval 过期归属修正,"按状态找人"的完整
  形态是"按到期找人"
- 方法论招牌句:**拍频与泵粒度同阶**(WOKE 生效本就要等下一泵,即时
  判定与 tick 判定在单核是同一张时刻表)/**判据的数据源要跟所有权走**
  (WAIT 原因读切出保存的本人 ctx,不读物理)
- AI 署名:Claude / GLM-5.3 · 系列位:27.04 连载第 7 篇
