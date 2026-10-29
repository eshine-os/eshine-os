<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 第二十站内容包:SMP RTOS(vm0 双 vCPU,门禁 67→72)

口径(README 索引):
- **主体(已批 spec docs/specs/2026-10-28)**:宿主 -smp 4
  (vm0.vcpu0 留 CPU0 轮转、vm0.vcpu1 上 CPU3 专泵、vm2 双 vCPU
  在 CPU1/2)+ RTOS per-CPU 内核 + 三 demo + 门禁 67→72。
- **per-CPU 内核**:TPIDR_EL1=核号、per-CPU 就绪队列+自旋锁
  (WFE/SEV+DAIF 还原)、tick 单写多读分治(核 0 记账+唯一唤醒
  扫描)、rt_puts 控制台自旋锁(双核心跳行互串定谳)。
- **SGI 跨核重调度全链**:rt_ipi 写 ICC_SGI1R_EL1 → EL2 陷阱 →
  vgic 帧挂起 → poke 专泵 → 注入 → 次核 IAR=0 → 重调度。PSCI
  走 hvc(smc 被 QEMU 机内 PSCI 截吃,返 DENIED 实测定谳)。
- **最深一仗:ipidemo rx=0 三层定谳**:①MMIO 窗单帧(0x20000→
  0x40000)②模型帧数硬码 id==2(vm0 nframes=1,帧 1 写全部静默
  WI,gicr1_en=0 探针定谳)③isenabler 门(次核自配 GICR 帧)。
  三层全真、全必要——窗口修好读回仍是 0,才逼出"写进窗≠
  写进模型"。
- **回合拍补课**:spec 明写往返(核 0 发→核 1 回发→核 0 计),
  首版只有单程;按 spec 补 echo 回合窗,rx0=22 roundtrips 判据。
- **判据**:72/72 连续两轮全绿(a8fd03b);镜像机 已同步;GitHub
  push 归用户(不碰红线)。

文件:
- zhihu-gongzhonghao.md:知乎/公众号长文(主线:RTOS 上双核了,
  IPI 一发不收——三层洋葱;含 PSCI hvc/smc 与次核 VBAR 两枚
  侧雷)
- xiaohongshu.md:小红书短文(双核 RTOS 的 IPI 丢包悬案)
- x-thread.md:X 8 推(串位:拓扑→per-CPU 内核→三层定谳→回合)
- youtube.md:视频脚本(逐层剥洋葱实录,含探针输出对照)
- 同批:docs/devlog/20-smp-rtos.md(devlog 收口)
