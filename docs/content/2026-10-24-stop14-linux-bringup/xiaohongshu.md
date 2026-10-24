<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# Linux 终于上机了!死在 VFS panic——但这是我们最想要的一个 panic 😭

从零写 Type-1 hypervisor 第 14 站:三门地基(设备树/GIC/串口)会师,
把真 Linux 6.6 内核放上 VM2 槽。

**结果**:内核认树、认 GIC、把我们的 virtio 块设备注册成 vda,
1.06 秒走完全程,在"没有 rootfs"处干净 panic ✅

**战报(五重根因)**:
1️⃣ 600 万次/秒无声异常风暴 → 竟是 SP_EL0:arm64 内核拿这个
全 EL 共享寄存器存 `current`,我们拿它当栈,每次进 guest 都把
宿主栈指针灌给内核——第一次碰 current 就自陷成环,连 Oops 都
打不出来(printk 也要 current)
2️⃣ jiffies 冻死:一拍 tick 都没消费过。Linux 的 timer handler
是轮询式——我们注入前"好心"关了虚拟定时器线,handler 读
ISTATUS=0 → "没来过人" → IRQ_NONE,时序世界停摆
3️⃣ 差 4 字节:GICD word1(0x104)才是 SPI 使能,我们自造约定
用 word0,Linux 按规范写,全被静默吞掉
4️⃣ panic 死循环关尽中断,单核 ARM 没有任何硬件抢占手段——
vmkill 扩权处决僵尸,宿主的绝对权威
5️⃣ 三门地基全兑现:.dumpdtb 诚实树被 Machine model 认领 ✅

**门禁 37→45 步,连续两轮真绿** 🟢🟢

下一站:配一个 rootfs,让 /sbin/init 接力 🏃

#从零打造操作系统 #Hypervisor #ARM64 #Linux内核 #开源
