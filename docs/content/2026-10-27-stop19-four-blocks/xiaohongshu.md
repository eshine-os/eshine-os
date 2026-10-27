<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 三核 hypervisor 上 Linux boot 停在 0.2 秒?

自己写 Type-1 hypervisor 的第 19 站:
- 第三个物理核,-smp 3,PSCI/vGIC 双帧/SGI 陷阱全通
- Linux boot 停在 0.2s 不动
- perf 看三个线程全是 100%,热点在页表漫游
- 根因① TLB 风暴:每退出+进场都 tlbi,QEMU 里同步全量冲刷,
  睡眠型 guest 每 park→wake 吃两发,速率塌 ~1000 倍
- 根因② 26 缺位:到期扫描唯一的周期驱动源没了,睡客蒸发后无再发现
- 根因③ 收尸窗误杀:27 三分支第三支一刀杀错在跑客,按 RX 停泊即永眠
- 修完还是冻在 SMP 交接窗——站二十接着打
每一层都是实测,不是猜的。
