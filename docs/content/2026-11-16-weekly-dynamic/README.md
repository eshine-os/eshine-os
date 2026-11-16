<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 第二轮消息公布：开源第 1 份周报（26.11-2w → 26.11-3w）内容包

`docs/content/2026-11-16-weekly-dynamic/` —— 11/14 仓库正式开源之后的**首份周报**，
也是"每周一发"固定节奏的第一期。按平台一文件，发布前人工过一遍再发。

| 文件 | 平台 | 形态 |
|---|---|---|
| `zhihu-gongzhonghao.md` | 知乎 / 微信公众号 | 深度长文（共用底稿，文末有各平台微调 + 话题投递说明） |
| `xiaohongshu.md` | 小红书 | 短内容 + 配图清单 + 圈层标签 |
| `x-thread.md` | X / Twitter | 英文 8 推 thread（每推带圈层 hashtag） |
| `youtube.md` | YouTube（B 站可复用中文版） | 分镜脚本 + 录屏清单 + 标题/描述/标签 |

## 本轮三条主线

1. **功能点回顾（26.11-2w → 26.11-3w，讲给不懂代码的人）**：
   shell 体验三连修（Ctrl-D 结束输入 / 相对路径 / 偶发开机卡死根因）、
   缺内核友好报错、一条命令跑起来（make run 自动补齐 + Docker 交互容器 +
   克隆即构建）、发布工程（历史清洗 / AI 真实署名 / 诚实边界）、
   官网双轴架构图与手机端修复。
2. **本周（26.11-3w）预告**：双核污染族 root-b/root-c 攻坚 → 183 门两连真绿 →
   bsh 空格洪水自首 → 站 69 候选。
3. **开源宣告 + 每周节奏 + 招募**：11/14 正式开源；每周一发周报 + 周标签；
   明确欢迎内核/虚拟化/C 汇编/嵌入式/测试/文档/前端工程师，
   入口=跑起来 → 认领 KNOWN-ISSUES → 提 Issue。

## 统一口径（事实，截至 2026-11-16）

- 开源日：**2026-11-14**
- 仓库：https://github.com/eshine-os/eshine-os（主）· https://gitee.com/eshine-os/eshine-os（镜像）
- 官网：https://www.eshine-os.org（kernel.org 风格，ASIL/CAL 双轴架构图 + 路线图）
- 状态数字：**68 个开发站、183 项自动检查、240+ 个公开提交、
  自研约 2.4 万行 C/汇编（不含 musl/busybox/Linux 上游）、Apache-2.0**
- 版本面：mainline 27.04-dev；stable v26.10（2026-10-21）
- 周标签：26.11-2w（锚 2026-11-09，收口件 68G Ctrl-D→EOF）、
  26.11-3w（锚 2026-11-16）

## 本轮功能点（写作底账）

- 68G（2w 收口）：Ctrl-D → EOF，cat/wc 等读输入命令不再永挂
- 68H：fd 相对路径正常化（`ls`、`cat ./etc/motd` 可用）
- 68I：开机偶发卡死根因=迹环递归锁竞态（藏两周，修后静置 6/6 绿）
- vm2 守卫：缺 Linux Image 时明确拒绝启动，不再跳空内存报神秘地址
- make run 自动 bootstrap；cjson/zlib 配方可执行；Docker 段 + 容器交互 QEMU
- 环境隔离：单工作根 + 宿主无关扫描（克隆即构建）
- 发布工程：全历史合规清洗 CLEAN；真实署名 trailer + validator/hook；
  KNOWN-ISSUES 诚实边界五条
- 发布面：双轴 ASIL/CAL 架构图；手机端发布表溢出修复（移植 kernel.org
  主题漏媒体查询规则，DOM 探针 + 像素差分验证）；Gitee 架构图 PNG 渲染

## 圈层投递矩阵（#主题，本轮硬要求）

| 平台 | 圈层 | 标签 / 话题 | 落点 |
|---|---|---|---|
| **X** | 开源 / OSDev | `#OpenSource #OSDev #Hypervisor #ARM64` | 1/8 开场 |
| **X** | Build in Public | `#BuildInPublic` | 4/8 |
| **X** | 嵌入式 / 内核 | `#RTOS #Embedded #LinuxKernel #Virtualization` | 8/8 收尾 |
| **小红书** | 宽圈 | `#操作系统 #开源项目 #程序员` | 正文末 |
| **小红书** | 垂圈 | `#嵌入式开发 #Linux #ARM #虚拟化` | 正文末 |
| **小红书** | 人设圈 | `#AI编程 #独立开发 #技术分享` | 正文末（10 标签以内） |
| **知乎** | 技术话题 | 操作系统 / 开源 / Linux / 虚拟化技术 / ARM / 嵌入式系统 / AI 编程 | 文末话题（5-6 个） |
| **公众号** | 合集 + 话题 | 合集「eshine-os 开源周报」；话题 `#操作系统 #开源 #AI编程` | 合集归档 + 文末 |

## 统一素材（截图，一次备齐）

1. 官网首页（双轴 ASIL/CAL 架构图首屏）
2. 手机端修复前后对比图（本周独有素材）
3. GitHub 仓库首页（README 状态区 + Run in Docker 段）
4. KNOWN-ISSUES.md 首屏（诚实边界五条）
5. git log 特写：真实署名 trailer（Model / Assisted-by / Signed-off-by）
6. 容器终端里 QEMU 开机 banner

## 发布节奏

- 每周一：周标签 + 周动态内容包 + PROJECT-STATE 刷新（本包即 26.11-3w 开周件，
  开源后的第 1 期周报）
- AI 结对署名透明：Claude Code / GLM-5.3 / DeepSeek 等（git log 可查）