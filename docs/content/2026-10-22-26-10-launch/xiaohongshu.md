<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
<!-- 小红书:短内容 + 配图清单,语气口语化 -->

# 小红书稿

## 标题（选一）

1. 每天 1-2 小时，和 AI 一起从零写操作系统 | 第 1 晚就发版了 🎉
2. 一个人 + AI，第一晚写出 hypervisor 骨架并发布 v26.10
3. 我在从零造一个开源操作系统底座（第 1 天）

## 正文

一晚上，从第一行代码到第一个正式版 🚀

eshine-os 26.10 发布了——一个从零开始写的开源 hypervisor（ARM64 虚拟化底座），以后想做到车载自动驾驶/机器人/医疗这种"不能出错"的场景。

今天这版只有 528 行代码，但五脏俱全：

🖥️ QEMU 上直接以 EL2 最高特权级启动
🛡️ 16 槽异常向量表，12 个槽"默认拒绝"——异常直接死给你看，不默默吞
⌨️ 一个能交互的 mini-shell
🧩 VM 状态机先于一切功能（这是很多 hypervisor 的通病：生命周期靠后补）
✅ 最重要的是——一个"真绿"门禁：每次提交自动开机、登录、发暗号，**暗号必须回显两次**才算过。静态字符串不算数，因为静态字符串会骗人。今晚它真抓到了 3 个 bug 😂

版本号学的 Ubuntu：26.10，每年 4 月 / 10 月发一版。
许可证 Apache-2.0，每一行代码都是从公开手册直接写的，干干净净。
AI 结对的署名都老实写在 commit 里（Claude / GLM-5.3，git log 可查）。

仓库在 GitHub 搜 eshine-os，官网 www.eshine-os.org
下一版 27.04：写页表，让第一个 guest 跑起来。

你们觉得一个人用 AI 从零长一个 OS 底座，能长成吗？评论区聊聊 👇

## 配图建议（按顺序）

1. 封面图：终端里 `eshine-os 26.10 (EL2) @ QEMU virt` banner + `eshine> state` 状态机输出（深色背景，最出片）
2. `RESULT PASS` 三步验证输出（[1/3] banner ok … [3/3] token echoed back）
3. GitHub 仓库首页截图（README + 26.10 released 状态）
4. 官网 www.eshine-os.org 首页（Latest Release: eshine-os 26.10）
5.（可选）git log 截图，展示 AI 署名（历史模板，说明见 docs/COMMIT-ATTRIBUTION.md）

## 标签

#操作系统 #hypervisor #开源项目 #AI编程 #程序员日常 #从零开始 #arm #嵌入式
