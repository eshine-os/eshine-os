<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 我们从零写的 RTOS 上双核了——但 IPI 一个都收不到

(eshine-os 从零打造记 · 第二十站)

前情:我们用一个从零写的 ARM64 Type-1 hypervisor(eshine-os)
在同一台 QEMU virt 上同时跑三个客——VM0 是我们自己从零写的
RTOS,VM1 是裸机对照组,VM2 是真 Linux。十九站把宿主铺到了
三个物理核,Linux 在里面跑出了双核。二十站的主体问题很自然:
**宿主自己会多核了,我们的 RTOS 呢?**

这一站的答案是:vm0 升双 vCPU——RTOS 的调度器从单核形态长成
per-CPU 就绪队列的真 SMP 形态,跨核唤醒走 SGI 中断。设计四问
(专泵拓扑/队列形态/IPI 机制/范围判据)都按推荐线定了:CPU3 做
vm0.vcpu1 的专泵,per-CPU 就绪队列(参考RTOS 公开设计的真形态),
SGI 走 vgic 陷阱,范围 vm0 only。

## 先落内核,再收三枚雷

per-CPU 化的机械部分是一次干净的大改:TPIDR_EL1 存核号(ARM64
的 per-CPU 数据标准位置),rt_cur/fp_owner/切换计数三符号转数组,
vec.S 的切换簿记宏让核号全程驻 x9;每核一套 32 级位图+FIFO,
队列锁是本工程第一个自旋锁——ldaxr/stxr 排 + WFE 睡 + SEV 唤,
进锁存 DAIF 出锁还原(同核递归=死锁,注释明写);tick 分治:
核 0 单写 rt_ticks 并独占唤醒扫描,跨核唤醒=锁内入对方队+发
SGI 让对方重新调度。

然后次核引导连收三枚雷,枚枚都是"不炸不知道":

1. **PSCI conduit**:EL1 发 `smc #0` 会被 QEMU 的机器级内部 PSCI
   截胡直接返回 DENIED——guest 的 PSCI 必须走 `hvc #0` 进我们
   EL2 的陷阱。十九站 Linux bring-up 踩过同款雷(method=hvc),
   这回轮到我们自己的 RTOS 踩。
2. **次核 VBAR**:EL1 向量表是 per-CPU 面,主核 start.S 设的那
   份不经次核——次核不自己设 VBAR_EL1,第一个 tick 中断取指落
   基地址 0+槽偏移(pc=0x280),指令 abort 炸回宿主。Linux 次核
   自配向量是同律,教科书不写,调试器教你。
3. **双核打印互串**:"cpu1: n=cpu0: n=…"——三段拼行各自加锁,
   段间被对方核插队。行原子必须整行组串后一次下发。

## 最深一仗:rx=0 的三层洋葱

内核活了,smpdemo 双核计数交错干净,fpsmp 两核浮点各算各的
闭式和都精确——唯独 ipidemo 的收包数恒 0。SGI 链每一环都
"应该没问题":rt_ipi 写的是 ICC_SGI1R_EL1 的 GICv3 规范布局,
EL2 陷阱翻译也对,泵注入也在跑。逐层剥:

**第一层:MMIO 窗。** 次核要初始化"自己那个"重分发器帧
(GICR 是 per-CPU 面,主核只配过帧 0)。帧 1 在 IPA+0x20000
——查 vm0 的设备表,VGICR 窗口还是 19D 时代的单帧 0x20000,
次核的使能写全落在窗外。放宽到 0x40000。

**第二层:模型帧数。** 窗口放宽后,`gicr1_en=0` 读回**仍是 0**。
这是本战的胜负手:写进窗≠写进模型。读 vgic 的重分发写处理,
第一行就是 `if (frame >= g->nframes) return;`——而 `g->nframes`
在 vmcreate 里硬码 `(id == 2) ? 2 : 1`:vm2 有两帧,vm0 只有一帧,
帧 1 的每一次写都被这个卫兵静默 WI(写忽略),读回自然全零。
把 nframes 改随几何表(n_vcpu),探针立刻读回 0x08000001。

**第三层:使能门。** 帧能写了,还要真使能——vgic_sgi_take 的
取包门是 `pend & isenabler`,次核的 SGI0 位不开,照样一包不进。
gic_secondary_frame_init 三寄存器(WAKER/IGROUPR0/ISENABLER0=
SGI0|PPI27)补上,rx1 从 0 跳到 23。

三层都真实、都必要——任何一层单修都白干。这种"每一层都
对、合起来全错"的洋葱,比单点 bug 有意思得多:窗口修好而
读回仍是 0,恰恰是它存在的证据。

## 按 spec 补课:回合拍

判据面还有个诚实账:spec 写的 ipidemo 是**往返**——核 0 发、
核 1 回、核 0 计数。首版只落了单程流(rx1 计到 20 但没有回程)。
按 spec 补:echo 回合窗内核 1 的 SGI handler 逐拍回发核 0
(核 0 不回,免乒乓),判据行 `ipi: rx0=22 roundtrips`。窗内
跨核唤醒推送各得一回,计入即真,判据线只设下限。

## 收口

门禁 67→72(只增不减):次核自报+帧 1 回读、smpdemo 交错、
SGI 往返≥20+跨核唤醒 3、fpsmp 双精确、次核在线主核不回归
(fpstat 两核求和严格递增)。三处同步纪律(verify/shell help/
ARCHITECTURE)同 commit,72 步走查表整块重写真机肉眼过。
连续两轮 72/72 全绿,镜像机 落盘 a8fd03b。

下一站候选(调研报告口径):同步原语超时面(rt_pend 加
wake_tick 截止,字段机器现成)、panic/未知异常打印 ESR/ELR/
任务名(现在静默 WFI 停车,排障全靠猜)、栈底 magic 哨兵。
License Apache-2.0,持续从零,欢迎围观。
