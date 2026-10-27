<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
1/ 我们从零写的 ARM64 Type-1 hypervisor 加到第三个物理核了(-smp 3),vm2 的第二 vCPU 通过 PSCI 起来,SMP 的 vGIC 双帧与 SGI 陷阱全部打通。然后 Linux boot 停在 0.2 秒不动。

2/ 现象层:同一构建同一参数,有时 0.4 秒到 "Brought up 1 node, 2 CPUs",有时 180 秒。这是竞态的第一信号。

3/ 第一层根因:perf 三个 vCPU 线程全 100%,热点弥散在 stage-2 页表漫游、重翻译、TB lookup——TLB 全量冲刷风暴的画像。

4/ 我们每次退出+进场都无条件 tlbi vmalls12e1is。QEMU TCG 把这个 IS 变种实现成三线程同步全量 flush;睡眠型 guest(Linux)每 park→wake 吃两发,100Hz tick 下模拟速率塌 ~1/1000。

5/ 修复①:per-pCPU 记上次 VTTBR,表值变化才 flush;轮转/出生/槽复用仍熄旧表。

6/ 第二层:到期扫描唯一的周期驱动源是 26(宿主相)。专泵核上没配 26,睡客的 tick 在途拍蒸发后没有再发现途径——补 26|27 双 PPI + 本地物理定时器。

7/ 第三层:27 三分支的第三支("无主收尸")就地写 CNTV_CTL=0。这一刀是 per-pCPU 的,扫描却只认停泊者;WFI 已入、簿记未竟的在跑客被误杀,按 RX 停泊即永眠。改只降线保挂,26 扫描接管再发现(≤1s)。

8/ 诚实记录:修完 vm2 靴仍冻死 SMP 交接窗(~0.2s 内核时);62 门 [1]-[30] 过、[31] 区超时,双绿未达,重编暂缓;判据行实况纠偏("Bringing up 1 secondary CPU(s)" 不存在)。下一站(二十)首项:SMP 交接窗竞态全闭。
