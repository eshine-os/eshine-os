<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 三核一钟下,Linux boot 停在 0.2 秒:一次 Type-1 hypervisor 的 tick 坍缩解剖

> 本文记录 eshine-os 第十九站的真实开发过程。第十八站我们让 vm2 的
> 第二个 vCPU 真正跑了起来;这一站加第三个物理核(-smp 3),把
> PSCI、vGIC 双帧、SGI 陷阱全部打通。然后 Linux boot 卡在内核
> 0.2 秒不动了。

## 现象

vm2 的第二 vCPU 起来后,boot 打到 "smp: Brought up 1 node, 2
CPUs" 就停。同一份二进制、同一组 QEMU 参数,到这一行有时 0.4 秒,
有时 180 秒——**同构建双速**,这是竞态的第一信号。

## 第一层:TLB 风暴

perf 三个 vCPU 线程全是 100%,热点弥散在 helper_ldul_mmu(stage-2
软件页表漫游)、translator_ldq(重翻译)、TB lookup 的长尾——
典型的 TLB 全量冲刷画像。我们的 vcpu_run/vcpu_exit 每次退出+进场
都无条件 `tlbi vmalls12e1is`;在真机上这属于浪费,在 QEMU TCG 里
这个 IS 变种被实现成**三线程同步全量 flush**。Linux 是睡眠型
guest,每个 park→wake 周期吃两发,100Hz tick 之下模拟速率直接
塌到约 1/1000。修复:per-pCPU 记上次 VTTBR 表值,表值变化才
flush,轮转/出生/槽复用仍熄旧表。

## 第二层:26 缺位

到期扫描(sched_vtimer_sweep)唯一的周期驱动源,是宿主相的
PPI26。专泵核上没配 26,睡客的 tick 在途拍蒸发后,就再没有
再发现的途径——单次蒸发即永眠。修复:次级配 26|27 双 PPI,
本地物理定时器补齐(计数权威留给 CPU0)。

## 第三层:收尸窗误杀

27 三分支的第三支("无主收尸")就地写 `CNTV_CTL=0`。这一刀是
per-pCPU 的,而此窗的扫描只认停泊者:WFI 已入、簿记未竟的在跑客
被误杀,出栈存得 ctl=0,按 RX 归类停泊——扫描(判据要 TIMER)
永不匹配,一个 tick 即永眠。修复:只降线保挂,停泊落定后由 26
每拍扫描接管再发现,有界(≤1s)自愈。

## 诚实记录

三修之后,vm2 靴仍冻死在 SMP 交接窗(~0.2s 内核时),两种拓扑
(vcpu0@cpu0 / vcpu0@cpu1)全部复现。62 门实测 [1]-[30] 过,
[31] 区 vm2-Linux 控制台判据超时,双绿未达;62→67 重编与
[53]/[65] SMP 判据暂缓。判据行实况纠偏:设计所引
"Bringing up 1 secondary CPU(s)" 在实测中不存在,真实行为是
"smp: Brought up 1 node, 2 CPUs"。

站十九收口 commit 6e5f1ef:devlog 19、spec §8 实况纠偏、plan
检查点、偏离册 #23。镜像机 已同步。

## 结语

三核一钟的拓扑打通了,SMP 交接窗的竞态还开着——下一站
(二十)首项就是把它闭死。
