<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 26.10 发布内容包（多平台底稿）

`docs/content/2026-10-22-26-10-launch/` —— eshine-os 26.10 发布当天的公开内容底稿，
按平台一文件，发布前人工过一遍再发。

| 文件 | 平台 | 形态 |
|---|---|---|
| `zhihu-gongzhonghao.md` | 知乎 / 微信公众号 | 深度长文（共用底稿，文末有各平台微调说明） |
| `xiaohongshu.md` | 小红书 | 短内容 + 配图清单 + 标签 |
| `x-thread.md` | X / Twitter | 英文 8 推 thread（附图清单） |
| `youtube.md` | YouTube（B 站可复用中文版） | 分镜脚本 + 录屏清单 + 标题/描述/标签 |

## 统一素材（截图/录屏，一次备齐）

1. 终端：`make clean && make` 零警告输出
2. `make run`：banner + `eshine> state` 状态机输出（主视觉图，深色背景）
3. `make verify`：`[1/3]…[3/3]…RESULT PASS`
4. GitHub 仓库首页（README 26.10 状态）
5. GitHub commit 历史（历史 AI 署名模板示例；模板≠逐条参与，见 docs/COMMIT-ATTRIBUTION.md）
6. 官网 www.eshine-os.org（Latest Release: eshine-os 26.10）

## 统一口径

- 事实：一晚（约 4-5 小时）人机结对、528 行 C/汇编、18 源文件、Apache-2.0、Ubuntu 式 YY.MM 版本管理
- AI 署名透明：Claude / GLM-5.3（git log 可查）
- 链接三件套：github.com/eshine-os/eshine-os · www.eshine-os.org · tag v26.10
- 下一版：27.04 —— stage-2 页表 + 第一个 guest 地址空间
