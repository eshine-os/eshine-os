<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 第十四站内容包:Linux bring-up 站(三门会师 → VFS panic)

`docs/content/2026-10-24-stop14-linux-bringup/` —— 27.04 第十四站(master
站内,2026-10-24 完成)的公开内容底稿,按平台一文件。

| 文件 | 平台 | 形态 |
|---|---|---|
| `zhihu-gongzhonghao.md` | 知乎 / 微信公众号 | 深度长文 |
| `xiaohongshu.md` | 小红书 | 短内容 + 配图 + 标签 |
| `x-thread.md` | X / Twitter | 英文 8 推 |
| `youtube.md` | YouTube(B 站) | 分镜脚本 |

## 统一素材

1. 主视觉:`Kernel panic - not syncing: VFS: Unable to mount root fs`——
   这是**预期的 panic**:Linux 6.6 LTS 在我们自研 Type-1 hypervisor 上
   跑完了整个启动流程,只差一个 rootfs
2. 五重根因的侦查故事:600 万次/秒的无声异常风暴 → 停帧现场 →
   SP_EL0 的 current 语义(arm64 内核把这个全 EL 共享寄存器当
   `current` 用,我们曾把它当栈)
3. "轮询式 handler 的哲学":Linux 的 timer handler 读硬件 ISTATUS
   才认账——仿真层替 guest"关好灯",handler 就说"没来过人",
   一拍 tick 静默蒸发,jiffies 冻死
4. 一个偏移量的代价:GICD word0 vs word1——自制约定与 IHI0069
   差 4 字节,SPI 使能全被吞
5. 45 步门禁 ×2 真绿;`virtio_blk [vda] 128 blocks` 参与挂载;
   僵尸处决权(vmkill 的哲学:宿主对不可抢占的关中断死循环客,
   唯一的解是管理面绝对权威)
