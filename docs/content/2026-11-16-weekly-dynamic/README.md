<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 开源第一周动态（26.11-2w 收口 + 26.11-3w 开周）内容包

`docs/content/2026-11-16-weekly-dynamic/` —— 开源公开发布后的第一份周动态底稿，
按平台一文件，发布前人工过一遍再发。

| 文件 | 平台 | 形态 |
|---|---|---|
| `zhihu-gongzhonghao.md` | 知乎 / 微信公众号 | 深度长文（共用底稿，文末有各平台微调说明） |
| `xiaohongshu.md` | 小红书 | 短内容 + 配图清单 + 标签 |
| `x-thread.md` | X / Twitter | 英文 8 推 thread（附图清单） |
| `youtube.md` | YouTube（B 站可复用中文版） | 分镜脚本 + 录屏清单 + 标题/描述/标签 |

## 本周主线（一句话）

仓库公开了：GitHub 主仓 + Gitee 镜像 + 官网三件套上线，整周用来把
"能跑的私仓"变成"敢让人看的公仓"——合规清洗、真实署名、环境隔离、
容器化构建、诚实边界，五件事全是发布工程，一行内核代码没写。

## 统一口径（事实）

- 仓库：https://github.com/eshine-os/eshine-os（主）· https://gitee.com/eshine-os/eshine-os（镜像）
- 官网：https://www.eshine-os.org（kernel.org 风格，架构图带 ASIL/CAL 双轴分级 + 路线图）
- 状态数字：68 个开发站、183 个可重放验证门、公开史近 240 个提交、
  自研约 2.5 万行 C/汇编（不含 musl/busybox/Linux 上游）、Apache-2.0
- 版本面：mainline 27.04-dev；stable v26.10（2026-10-21）
- 本周工程五件事：
  1. 合规全史清洗：商标/参考仓标识/私有管线引用全部中性化，
     全历史复扫 CLEAN；
  2. 真实署名：`Model:` / `Assisted-by:` / `Reviewed-by:` 真实语义
     trailer，validator + git hook 机器背书，历史模板在
     docs/COMMIT-ATTRIBUTION.md 里坦白说明；
  3. 环境隔离：单工作根 `ESHINE_WORK`，仓库树宿主无关，
     check-host-independence 扫描 tracked+untracked；
  4. 容器化构建：钉版容器里 `--network none` 跑 QEMU 开机到 banner +
     交互 shell，CI 同一容器；
  5. 诚实边界：KNOWN-ISSUES.md 公开开放缺陷（[37]/[38] 双核污染族、
     bsh 空格洪水 ~1/3、全量两连真绿未达、仅 QEMU virt 验证）。
- 周一（11-16）动态：
  - 周标签 `26.11-3w`（scripts/tag-dev-week.py，锚定本周最早提交）；
  - 官网手机溢出修复：`mainline:` 发布表在手机上出屏 70px，
    根因是 kernel.org 主题的 848px 媒体查询移植时漏掉 `#releases`
    滚动容器规则，v7 恢复上游规则（像素差分验证只影响越界条带）；
  - wget 下载回落 + 宿主侧容器 bootstrap（现场报告修复）。
- 下一步（新周任务，缺陷预算优先）：
  1. [37]/[38] 双核污染族 root-b/root-c 攻坚（root-a 已修）；
  2. 之后 183 门两连真绿（安静窗）；
  3. bsh 空格洪水自首；
  4. 站 69 候选：EL2 边界 fuzz / sub-tick hrtimer / SMMU·GIC-ITS 真板预习。

## 统一素材（截图，一次备齐）

1. 官网首页（手机 375px 修复前后对比图，本周独有素材）
2. GitHub 仓库首页（README 状态区 + Run in Docker 段）
3. Gitee 镜像首页（架构图 PNG 渲染正常）
4. KNOWN-ISSUES.md 首屏（诚实边界五条）
5. git log 特写：新式真实署名 trailer（Model/Assisted-by/Signed-off-by）
6. 官网发布表（mainline: 27.04-dev / stable: 26.10）

## 发布节奏

- 每周一：周标签 + 周动态内容包 + PROJECT-STATE 刷新（本包即 26.11-3w 开周件）
- AI 结对署名透明：Claude Code / GLM-5.3（git log 可查）
