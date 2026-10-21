<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
<!-- YouTube devlog 00:脚本 + 录屏清单 + 标题/描述/标签。B 站可直接复用本稿(中文配音版) -->

# Devlog 00 视频方案（YouTube / B 站）

## 标题选项

1. **Building an OS Hypervisor From Scratch with AI — Day 1: First Light at EL2**（YouTube 主推）
2. 从零用 AI 写 hypervisor｜第 1 天：EL2 点亮（B 站）
3. Day 1: from zero lines to a tagged release in one evening (eshine-os 26.10)

## 时长与形态

- 目标 3-5 分钟（Day-1 devlog 短平快；真正的长视频留给 27.04 有 guest 可跑时）
- 形态：录屏 + 旁白；无出镜要求

## 分镜脚本

| # | 时长 | 画面 | 旁白（要点） |
|---|---|---|---|
| 1 | 0:00-0:20 | 官网 www.eshine-os.org 滚动 | 开场钩子："这是一个开源 hypervisor 的第一天。我是从零开始写的，搭档是 AI。目标：高可靠高安全场景的 OS 底座。今晚，第一版发布。" |
| 2 | 0:20-0:50 | `make clean && make` 录屏 | "整个系统是一个 Makefile 加 528 行 C/汇编。零警告构建。"（让零警告的绿字停留 2 秒） |
| 3 | 0:50-1:30 | `make run`：banner → shell → `help` → `echo hello` | "QEMU 直接进 EL2 —— virtualization=on，没有 ATF。banner、一个带行编辑的 shell。" |
| 4 | 1:30-2:10 | `state` 命令全屏输出 | 重点段："VM 状态机先于一切功能。表驱动，任何表外转换直接 panic。生命周期先行——这是很多 hypervisor 项目后期补不上的一课，我把它放在了第一天。" |
| 5 | 2:10-3:00 | `make verify` 三步 PASS | "真绿门禁：脚本自动开机、发带进程号的暗号、必须回显两次。今晚它抓了三个 bug，包括一个它自己的。"（屏幕上指出 [3/3] token echoed back） |
| 6 | 3:00-3:30 | GitHub 仓库/commit 历史（AI co-author trailer 特写） | "AI 结对是透明的——Claude 和 GLM-5.3 就签在每个 commit 里。Apache-2.0，每行代码从公开手册直接写。" |
| 7 | 3:30-4:00 | 官网发布表 + tag v26.10 | "26.10，Ubuntu 式的版本节奏，每年四月和十月。下一版 27.04：页表和第一个 guest 地址空间。仓库链接在下面——watch 一下，看它能长成什么样。" |

## 录屏清单（一次录完）

1. 官网首页滚动（慢）
2. `make clean && make`（干净终端，字体调大）
3. `make run`：banner → `help` → `echo eshine-os` → `state`
4. `make verify`（连续跑两次更可信）
5. GitHub：repo 首页 → README 状态区 → commits（滚到 Co-authored-by）→ tag v26.10
6. 官网发布表特写（Latest Release: eshine-os 26.10）

## 描述（Description）

```
eshine-os 26.10 — the first release, built from scratch in one evening
with AI pair programming (Claude + GLM-5.3).

eshine-os is an open-source foundational OS for high-reliability,
high-safety scenarios (autonomous driving, robotics, medical),
built on ARM64 virtualization. This release is the EL2 skeleton:
direct-EL2 boot on QEMU virt, a 16-slot default-deny exception
vector table, PL011 console, an interactive mini-shell, a
table-driven VM state machine, and a true-green verify gate.

Repo:    https://github.com/eshine-os/eshine-os
Website: https://www.eshine-os.org
Release: docs/releases/v26.10 (tag v26.10)
License: Apache-2.0

Timeline:
00:00 What is eshine-os
00:20 Zero-warning build
00:50 Boot to EL2 + shell
01:30 VM state machine, lifecycle first
02:10 The true-green gate
03:00 Transparent AI co-authorship
03:30 26.10 release + what's next (27.04: stage-2 page tables)

#osdev #hypervisor #arm64 #opensource #ai
```

## 标签

YouTube tags: osdev, hypervisor, arm64, aarch64, qemu, operating system, open source, AI pair programming, virtualization
B 站标签: 操作系统、底层开发、hypervisor、虚拟化、开源、AI编程、教程
