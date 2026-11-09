<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 第三站内容包:vCPU 完整上下文切换(陷阱帧 + yield 断点 + fault 隔离)

`docs/content/2026-10-22-stop3-vcpu-context/` —— 27.04 第三站(master
0e56988..916f079)的公开内容底稿,按平台一文件,发布前人工过一遍。

| 文件 | 平台 | 彺态 |
|---|---|---|
| `zhihu-gongzhonghao.md` | 知乎 / 微信公众号 | 深度长文(共用底稿,文末有各平台微调说明) |
| `xiaohongshu.md` | 小红书 | 短内容 + 配图清单 + 标签 |
| `x-thread.md` | X / Twitter | 英文 8 推 thread(附图清单) |
| `youtube.md` | YouTube(B 站可复用中文版) | 分镜脚本 + 录屏清单 + 标题/描述/标签 |

## 统一素材(截图/录屏,一次备齐)

1. 全流程录屏(主视觉):vmrun 0 → `selftest` 输出 100 个点 + OK →
   `yield` → 回宿主 → vmls 见 PAUSED → `vmrun 0` → **resumed,vm0> 原地复活**
2. fault 隔离录屏:`vmrun 1` → `fault` → 诊断行(注意 x0=0x30000000 正是
   野指针原值!)→ VM_DEAD → 宿主 echo 继续活
3. `make verify`:17 步 `[1/17]..[17/17] RESULT PASS`(连续两次)
4. 反汇编特写(grep 出来的 `add x30, x30, x19` + `hvc #0` 相邻两行)
5. gdb 泄漏现场(可选进阶):踩 cur_vcpu 的 watchpoint 输出

## 统一口径

- 事实:陷阱帧(gprs[31]+sp+pc+pstate 内嵌 vcpu,偏移=asm 契约)、
  HVC 三调用号(#2 yield)、PAUSED 首次启用(状态机零新转换)、
  17 步门禁连续 2 过、与参考实现形态对齐(差异入设计记录 #5/6/7)
- 三个 debug war story 全实证:noreturn 反杀 / SP_EL2 79 发陷阱碾穿栈底 /
  GCC 常量折叠杀判据
- AI 署名透明:Claude / GLM-5.3(git log 可查)
- 链接三件套:github.com/eshine-os/eshine-os · www.eshine-os.org · devlog 03
- 系列位:27.04 连载第 3 篇
