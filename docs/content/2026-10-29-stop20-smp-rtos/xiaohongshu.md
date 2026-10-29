<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 双核 RTOS 的 IPI 丢包悬案

自己写 hypervisor + RTOS 的第 20 站:
- 我们的 RTOS 上双核了(自己的 hypervisor 给它第二个 vCPU)
- 双核计数交错、两核浮点各自精确——都很美
- 唯独跨核中断(IPI)一个都收不到

剥了三层洋葱:
1️⃣ MMIO 窗口太小,次核的使能写落在窗外
2️⃣ 窗口修好,读回还是 0——原来 vgic 模型里 vm0 的"帧数"
   硬编码成 1,帧 1 的写全部被静默忽略。写进窗 ≠ 写进模型
3️⃣ 帧能写了还要真使能,取包门 pend & isenabler 又滤一道

三层全真、全必要,任何一层单修都白干。
窗口修好而读回仍是 0,恰恰是第二层存在的证据。

顺带收了两枚教科书不写的雷:
- guest 发 PSCI 用 smc 会被 QEMU 内部 PSCI 截胡,必须 hvc
- 次核不自己设 VBAR_EL1,第一个 tick 中断直接跳 0x280 炸机

门禁 67→72,连续两轮全绿 ✅
Apache-2.0,从零打造,每天 1-2 小时。
#hypervisor #RTOS #ARM64 #SMP #从零打造
