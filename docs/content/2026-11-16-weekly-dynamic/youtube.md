<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
<!-- YouTube 周报 01:脚本 + 录屏清单 + 标题/描述/标签。B 站可直接复用本稿(中文配音版) -->

# 周报 01 视频方案（YouTube / B 站）—— 开源第一周

## 标题选项

1. **Open-Sourcing My AI-Pair-Programmed OS: Week 1 Was Zero Lines of Kernel Code**（YouTube 主推）
2. 开源第一周：一行内核代码都没写 | eshine-os 周报 01（B 站）
3. Release engineering week: history scrub, honest AI attribution, containerized builds

## 时长与形态

- 目标 4-6 分钟（周报形态，比 devlog 更轻；重头戏留给站 69 的技术站）
- 形态：录屏 + 旁白；无出镜要求

## 分镜脚本

| # | 时长 | 画面 | 旁白（要点） |
|---|---|---|---|
| 1 | 0:00-0:25 | 官网 www.eshine-os.org 滚动（架构图 → 发布表） | 开场钩子："这个从零写的 hypervisor + RTOS 公开一周了。本周战报有个反直觉的事实：一行内核代码都没写。整周都在做一件事——配得上公开。" |
| 2 | 0:25-1:10 | GitHub 仓库首页 → clone 命令 → Gitee 镜像 | "先给地址：GitHub 主仓，Gitee 镜像，官网三件套。68 个站、183 个门、两万五千行自研代码，Apache-2.0。" |
| 3 | 1:10-1:55 | git log 滚动 + trailer 特写 → COMMIT-ATTRIBUTION.md | "第一件：真实署名。AI 结对写在 trailer 里——Model 是主模型，Assisted-by 是实际参与，validator 和 git hook 会拒绝旧式模板。AI 参与是事实，就按事实记录。" |
| 4 | 1:55-2:40 | 终端：`scripts/container.sh make run` → 容器内 QEMU banner + shell | "第二件：容器化。钉版容器、network none——没网也能从构建开机到交互 shell，这才叫没有隐式依赖。周一还有个现场报告：curl 挂了 wget 活着，回落修复，上午闭环。" |
| 5 | 2:40-3:20 | KNOWN-ISSUES.md 首屏逐条滚动 | "第三件，也是我最在意的：诚实边界。五条开放缺陷挂在门口——双核污染族、shell 空格洪水、两连真绿未达。开源不是庆功，是跟未来贡献者签合同。" |
| 6 | 3:20-4:00 | 手机 375px 前后对比（左：表格出屏；右：收进卡片可滑动） | "发布第一天就有现场 bug：手机上发布表超屏 70px。根因：移植 kernel.org 主题漏了一条媒体查询规则。补回上游规则，DOM 探针加逐字节截图验证。教训：移植主题，断点内每个区块都要逐条对上游。" |
| 7 | 4:00-4:40 | PROJECT-STATE.md 的 Next action 清单 | "下周计划，缺陷预算优先：先收双核污染族，再冲两连真绿，然后 shell 空格洪水自首——都收敛了才开站 69。有 P0 不开新站，这是这个项目的铁律。" |
| 8 | 4:40-5:00 | 官网发布表 + repo 链接定格 | "每周一：周标签 + 周报。一个人加 AI，从零长一个 OS 底座——地址在下面，watch 一下看它长成什么样。" |

## 录屏清单（一次录完）

1. 官网首页滚动（桌面，慢速，停在架构图和发布表）
2. GitHub 仓库首页 → README 状态区 → Run in Docker 段
3. git log（滚到新式 trailer 的提交，特写 Model/Assisted-by/Signed-off-by 三行）
4. 容器内 `make run`：banner → shell 提示符 → 一条命令
5. KNOWN-ISSUES.md 五条逐条（浏览器渲染页）
6. 手机模拟器 375px：修复前后对比（可先录线上现状的存档视频，再录修复后）
7. PROJECT-STATE.md 的 Next action 清单
8. 官网发布表定格（mainline: 27.04-dev / stable: 26.10）

## 描述（Description）

```
eshine-os went public one week ago — and this week wrote ZERO lines of
kernel code. All release engineering: full-history compliance scrub,
honest AI attribution (Model/Assisted-by trailers, machine-enforced),
environment isolation, containerized builds that boot QEMU with
--network none, and an honest KNOWN-ISSUES page linked from the
homepage. Plus a day-1 field fix: the mobile overflow on the release
table (one dropped rule from the kernel.org theme port).

eshine-os is an open-source foundational OS for high-reliability,
high-safety scenarios (autonomous driving, robotics, medical) on
ARM64 virtualization: Type-1 hypervisor at EL2, a from-scratch RTOS
management domain, and a real Linux 6.6 guest. 68 dev stations, 183
replayable verification gates.

Repo:    https://github.com/eshine-os/eshine-os
Mirror:  https://gitee.com/eshine-os/eshine-os
Website: https://www.eshine-os.org
License: Apache-2.0

Next week (defect budget first): the [37]/[38] dual-core contamination
family, then two consecutive full 183-gate green runs, then the bsh
space-flood confession — and only then station 69.

Timeline:
00:00 Zero lines of kernel code
00:25 The three public faces
01:10 Honest AI attribution
01:55 Containerized, network none
02:40 KNOWN-ISSUES at the door
03:20 Day-1 mobile overflow fix
04:00 Next week: defect budget first

#osdev #hypervisor #arm64 #opensource #ai #release-engineering
```

## 标签

YouTube tags: osdev, hypervisor, arm64, aarch64, qemu, operating system, open source, AI pair programming, virtualization, release engineering
B 站标签: 操作系统、底层开发、hypervisor、虚拟化、开源、AI编程、周报、发布工程
