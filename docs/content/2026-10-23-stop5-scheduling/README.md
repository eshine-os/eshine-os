<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 第五站内容包:调度站(shell 泵 + 时间片抢占 + RX 仲裁 + WAIT 公平)

`docs/content/2026-10-23-stop5-scheduling/` —— 27.04 第五站(master
d36fb59..e672fd5,2026-10-23 完成)的公开内容底稿,按平台一文件,
发布前人工过一遍。

| 文件 | 平台 | 形态 |
|---|---|---|
| `zhihu-gongzhonghao.md` | 知乎 / 微信公众号 | 深度长文(共用底稿,文末有各平台微调说明) |
| `xiaohongshu.md` | 小红书 | 短内容 + 配图清单 + 标签 |
| `x-thread.md` | X / Twitter | 英文 8 推 thread(附图清单) |
| `youtube.md` | YouTube(B 站可复用中文版) | 分镜脚本 + 录屏清单 + 标题/描述/标签 |

## 统一素材(截图/录屏,一次备齐)

1. 双 VM 真并行录屏(主视觉):`vmrun 0` 秒回 shell(异步)→
   `vmconsole 0/1` 轮转交互 → 双 VM token 交错双回显
2. spin 无损续跑录屏:`spin` → `spinning...` →(约 8 秒)→
   **`spun 3G iters (preempted ok)`** → `vmstat` 前后 preempts 差
3. `make verify`:22 步 `[1/22]..[22/22] RESULT PASS` 连续两次,特写
   `[13/22] spin 3G survived 7 quantum preemptions`——帧转写判据
4. 注释陷阱对比截图:源码段(肉眼看"完全正常")vs objdump diff
   (NULL 路径直接 `ret`,EOI 调用消失)——本站最强素材
5. RX 抢键盘名场面:`vmstat` 命令落进 guest 执行的输出截图(仲裁前
   的原始录像,若未存可补录:双 guest 直读 DR 互抢版本已删,可用
   文中截图替代)

## 统一口径

- 事实:shell=调度泵(命令即原子段,宿主权威);量子=1 tick;双扳机
  点集中切换(guest 态 IRQ 出口帧转写 + HVC 出口查);RX=HVC #3 三态
  代理(-1 前台暂无输入/-2 无输入权→WFI 让位/≥0 字符)+ HVC #4 交还;
  22 步门禁 ×3 绿;spin 3G 实测跨 7 量子;偏差 #11(调度形态)/#12
  (console 形态)入设计记录
- 十坑全实证:帧偏移错位/WAIT 覆盖/WOKE 串台/前台死锁/RX 直读互抢/
  **注释陷阱**(吞 if 体→EOI 空洞→tick 死)/mrs 时基循环不退(QEMU
  helper×TB 疑点,如实记录未追)/工具两枪/解析竞态——弯路如实交代
- 方法论招牌句:**"源码看起来对 ≠ 汇编对,objdump 是终局武器"**;
  "门禁判据不锚时基,锚跨量子次数";"进了等待就必须放下独占资源"
  (让位协议);"事件的受益者 ≠ 当值者,按状态找人"
- AI 署名透明:Claude / GLM-5.3(git log 可查)
- 链接三件套:github.com/eshine-os/eshine-os · www.eshine-os.org · devlog 05
- 系列位:27.04 连载第 5 篇(前三篇暗线=栈与 bank 的"余量与归一",
  第四篇="模拟器源码=地面真值",本篇暗线=**"交互面才是 bug 的主场"**
  ——每个子系统单独都对,拼起来塌)
