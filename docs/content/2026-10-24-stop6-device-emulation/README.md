<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 第六站内容包:设备仿真站(vuart/MMIO 陷阱 + HVC IO 退役)

`docs/content/2026-10-24-stop6-device-emulation/` —— 27.04 第六站(master
892c763..5d5a832,2026-10-23 完成)的公开内容底稿,按平台一文件。

| 文件 | 平台 | 形态 |
|---|---|---|
| `zhihu-gongzhonghao.md` | 知乎 / 微信公众号 | 深度长文(共用底稿,文末平台微调) |
| `xiaohongshu.md` | 小红书 | 短内容 + 配图清单 + 标签 |
| `x-thread.md` | X / Twitter | 英文 8 推 thread |
| `youtube.md` | YouTube(B 站复用中文版) | 分镜脚本 |

## 统一素材

1. 主视觉录屏:`vmrun 0` → `[vm0] eshine-guest VM0 @EL1 via stage-2 (vuart
   @trap)` —— banner 逐字从陷阱里走出来(后台整行 [vm0] 前缀)
2. `-d int` 现形截图:同一条 ldr 陷阱 13,748 次连环(ELR 两副面孔的实据)
3. gdb 截图:帧内 x1=0x10 vs 活体 x1=0x400010e0 + watchpoint 盯帧槽的
   交替写入(帧对活体错,本站最强素材)
4. HPFAR 探针截图:`fault: ... HPFAR=0x300000`(0x30000000>>8 定案)
5. fault 诊断全行(窗口外默认拒绝):`ESR=0x93c18006 FAR=0x30000000
   HPFAR=0x300000 pc=... -> vm0: faulted -> VM_DEAD`

## 统一口径

- 事实:UART 撤出 stage-2 映射 → DR/FR 全 data abort 陷阱化 → HPFAR/IPA
  → 窗口匹配(默认拒绝)→ per-VM vuart 仿真 → SRT 回填 → pc+=4 代退休;
  HVC #0/#3 退役(超调用只剩控制类);selftest 升级 100 MMIO 陷阱;门禁
  24 步双绿(第六站时点;后续站已到 28)
- 三大战果如实交代:ELR 两副面孔(HVC/WFI 指下条 vs abort 指 faulting
  指令)/x1 的赎金(第三站暗雷:eret 尾 x0 中转费从 x1 账户划走)/
  HPFAR=IPA>>8(探针定案,不抄记忆)
- 方法论招牌句:**"源码看起来对 ≠ 汇编对"**(承第五站)+ **"回读正确
  ≠ 生效"**(为第七/八站埋线)+ **"帧值 ≠ 活体值"**(MMIO 时代 gdb
  取证两层对照)+ **"同一根 ELR,两副面孔"**
- AI 署名透明:Claude / GLM-5.3 · 链接三件套 · 系列位:27.04 连载第 6 篇
