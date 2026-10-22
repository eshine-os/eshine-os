<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 第二站内容包:VM 抽象落实(双 VM + 生命周期)

`docs/content/2026-10-22-stop2-vm-abstraction/` —— 27.04 第二站(VM 抽象,
master bf81d8b..9bebcda/794bbdd)的公开内容底稿,按平台一文件,发布前人工过一遍。

| 文件 | 平台 | 形态 |
|---|---|---|
| `zhihu-gongzhonghao.md` | 知乎 / 微信公众号 | 深度长文(共用底稿,文末有各平台微调说明) |
| `xiaohongshu.md` | 小红书 | 短内容 + 配图清单 + 标签 |
| `x-thread.md` | X / Twitter | 英文 8 推 thread(附图清单) |
| `youtube.md` | YouTube(B 站可复用中文版) | 分镜脚本 + 录屏清单 + 标题/描述/标签 |

## 统一素材(截图/录屏,一次备齐)

1. `make clean && make` 零警告输出
2. `make run` 全流程录屏:vmls → vmcreate 0 → vmrun 0(vm0 banner+交互)
   → exit → vmcreate 1 → vmrun 1(**vm1 banner,身份不同!**)→ exit → vmls
3. `make verify`:14 步 `[1/14]..[14/14] RESULT PASS`(连续两次)
4. gdb 现场(可选进阶素材):`pgd[0]` 指向 pgd 自己的那张 dump
5. 终端特写:`vm1: CREATED (ram 0000000051000000)` —— 同入口不同物理窗口

## 统一口径

- 事实:一晚人机结对、双 VM 静态全量内嵌(每槽自带 ~20KB stage-2 表)、
  同源双镜像(-DGUEST_ID)、14 步真绿门禁连续 4 过
- 三个 debug war story 全部实证级(gdb/反汇编可复现)
- AI 署名透明:Claude / GLM-5.3(git log 可查)
- 链接三件套:github.com/eshine-os/eshine-os · www.eshine-os.org · devlog 02
- 系列位:27.04 连载第 2 篇(前篇:26.10 EL2 骨架发布)
