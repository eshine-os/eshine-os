<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 第四站内容包:GICv3 中断站(真中断 + 虚拟注入全链 + 五案连环)

`docs/content/2026-10-23-stop4-gicv3-interrupts/` —— 27.04 第四站(master
2d064eb..e1fca4c,2026-10-22 深夜完成)的公开内容底稿,按平台一文件,
发布前人工过一遍。

| 文件 | 平台 | 形态 |
|---|---|---|
| `zhihu-gongzhonghao.md` | 知乎 / 微信公众号 | 深度长文(共用底稿,文末有各平台微调说明) |
| `xiaohongshu.md` | 小红书 | 短内容 + 配图清单 + 标签 |
| `x-thread.md` | X / Twitter | 英文 8 推 thread(附图清单) |
| `youtube.md` | YouTube(B 站可复用中文版) | 分镜脚本 + 录屏清单 + 标题/描述/标签 |

## 统一素材(截图/录屏,一次备齐)

1. 全流程录屏(主视觉):`tick` → `ticks: 0` →(3 秒)→ `ticks: 2` →
   (3 秒)→ `ticks: 5`——宿主第一次活在"被中断"里
2. 虚拟注入全链录屏:`vmrun 0` → `sleep` → `sleeping...` →
   **`slept (vtimer irq)`** → `vm0>` 回来(注意:宿主物理线由 guest 的
   虚 EOI 硬件联动撤销)
3. `make verify`:20 步 `[1/20]..[20/20] RESULT PASS` 连续两次,特写
   `[19/20] post-guest tick grows`——这步当场抓出第五个 bug
4. 探针名场面截图:`gicdbg` 输出(isr=0x80、daif=0x340、iar=0x1a 全绿
   却不投递的"矛盾三连")与认领风暴 `[unk-irq 0x1b]` 刷屏
5. QEMU 源码考古截图:gicv3_internal.h 的 GICD 镜像偏移表 +
   target_el_table 注释(四行红鲱鱼名单也可以入镜)

## 统一口径

- 事实:CNTHP=INTID 26 / CNTV=INTID 27(活探针定案);HCR_EL2 IMO=bit4
  (QEMU cpu.h 与参考实现 sysregs.h 双源互证);20 步门禁连续 2 过;偏差
  #8/#9/#10 入设计记录(单 LR 无兜底/TWI+WAIT+代完成/vtimer 串行共享)
- 五案连环全实证:偏移幻觉/组位/PPI 活探针/IMO 位号/vcpu_exit DAIF;
  红鲱鱼(UWP/WAKER/RWP)也如实交代——侦探故事不隐瞒弯路
- 方法论招牌句:**"模拟器之上开发,模拟器源码就是地面真值"**;
  每个记忆里的位号,先核源码或活探针再落键盘
- AI 署名透明:Claude / GLM-5.3(git log 可查)
- 链接三件套:github.com/eshine-os/eshine-os · www.eshine-os.org · devlog 04
- 系列位:27.04 连载第 4 篇
