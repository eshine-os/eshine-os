<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 第一站内容包:stage-2 + 第一个 guest 地址空间

`docs/content/2026-10-22-stop1-first-guest/` —— 27.04 第一站(master
11a6f9a..648f040,9 commit)的公开内容底稿,按平台一文件,发布前人工过一遍。

| 文件 | 平台 | 形态 |
|---|---|---|
| `zhihu-gongzhonghao.md` | 知乎 / 微信公众号 | 深度长文(共用底稿,文末有各平台微调说明) |
| `xiaohongshu.md` | 小红书 | 短内容 + 配图清单 + 标签 |
| `x-thread.md` | X / Twitter | 英文 8 推 thread(附图清单) |
| `youtube.md` | YouTube(B 站可复用中文版) | 分镜脚本 + 录屏清单 + 标题/描述/标签 |

## 统一素材(截图/录屏,一次备齐)

1. 全流程录屏(主视觉):run_guest → guest banner(HVC 控制台)→
   UART banner(直访)→ guest 交互 → exit → 回宿主 prompt
2. gdb 连环案现场(核心素材):
   `A sp=0x4020ce60 / B after msr spsel,#0: sp=0 / C x16=0 x17=0 /
   H ret x17 后 pc=0` 四连截图
3. ESR 0x02000000 崩溃输出 + ESR 0x8200000A(AF fault)对照
4. `make verify` 8 步 `[1/8]..[8/8] RESULT PASS`(修 FR 后连续 8 次)
5. s2dump 页表输出(l1→l2 block→l3 page 三级,非恒等映射特写)

## 统一口径

- 事实:一晚人机结对、stage-2 模块(VTCR/descriptor/walker)+ vCPU
  进出口 + 首个裸机 guest(IPA 0x40000000→PA 0x50000000 非恒等映射)+
  双 IO 路径(HVC 超调用 + 直访 UART 设备页)+ 8 步真绿门禁连过 8 次
- 三个 debug war story 全部 gdb 实证:AF bit / SP bank 连环案 / FR 偏移热自旋
- AI 署名透明:Claude / GLM-5.3(git log 可查)
- 链接三件套:github.com/eshine-os/eshine-os · www.eshine-os.org · devlog 01
- 系列位:27.04 连载第 1 篇(前篇:26.10 EL2 骨架发布,即 launch 包)
