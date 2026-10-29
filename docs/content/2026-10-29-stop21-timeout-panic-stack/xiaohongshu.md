<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 我写的 spec 被我的评审舰队证伪了 16 处

从零写 hypervisor+RTOS 的第 21 站:
- 给 RTOS 补三块:同步超时 / panic 打印 / 栈防护
- 先写 spec,再派 4 镜头 × 复核链共 20 个评审 agent 对抗证伪
- 回来 16 条 CONFIRMED,含 2 条 blocker

最有意思的翻案:
🔍 我的 spec 写"PI boost 由 give 侧零改动回收"
→ 评审证伪:boost 的唯一回收点是 give,而互斥超时要防的病
  恰恰是"锁主永不放锁"——boost 无限滞留,中间优先级全部饿死
→ 修订:超时离队时同步重算 owner,判据升格 14→9→14 全弧

实测还撞上三颗雷:
1. __builtin_frame_address 在 -O2 无帧指针下不可用——递归零下潜,
   "dived"秒回,判据静默失效
2. 递归帧足迹稀疏——深潜穿过哨兵而哨兵恰落缝里没被写
3. 溢出任务在深 SP 停泊——tick 的 272B 异常帧穿透垫区
   修法:解栈回浅再停泊

72→75 门,连续两轮全绿 ✅
Apache-2.0,每天 1-2 小时,从零打造。
#从零打造 #RTOS #hypervisor #对抗评审 #ARM64
