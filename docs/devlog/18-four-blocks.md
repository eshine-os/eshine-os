<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# Devlog 18 — 四块合成站(在途拍守门 / 优先级继承 / per-task FP / vCPU>1)

日期:2026-10-27 · 版本:27.04-dev · 门禁:62 步(e2/e3/e4 连续全绿;e1 一记 [41] 闪断如实记档)

用户令:"下一站4项工作全部需要完成"——四块合成站,实施序 A→B→C→D
(稳定性先落地,最大风险殿后)。

## A 块:在途拍守门([53] 闪断根治)

**真相考古**比修本身更有意思:站16 e87a3bb 的 el2_irq_host 27 分支注释
已写对方案("关线只属睡客路径…RUNNING 轮转客 ctl 不许动:重装帧
ctl=1+过期 cval 即自证挂起,进场即注入"),**但代码仍无条件
vgic_wake_waiter()**;注释里"见下"的封窗机器(陷阱量子配额重填)随站16
整组退役,窗重新敞开=站17 [53] 四轮一挂。本站把注释的方案真正落进
代码:

| 27 到点(宿主相) | 动作 |
|---|---|
| sweep 真唤醒睡客 | 关线(老行为:睡客没人替它落线) |
| **在途(vtimer_inflight)** | **不碰物理线**(HW LR pending 跟线,关了即蒸发);EOI 只降优先级,置回业主标志,泵以最后进场者为旋转起点进场消费 |
| 无睡客无在途 | 关线收尸(无主陈旧挂起) |

风暴疑虑(re-assert)由泵的必然回环消化:shell 每圈必调 sched_pump,
线挂期间重复 27 由三分支廉价吸收,收敛到一圈泵间隙;业主已死/停由泵
顺手熄线收尸。58 存量 ×3 连绿([53] 解包窗三过)。

## B 块:互斥优先级继承

单级 PI(参考RTOS 传递 PI 裁剪,记残余):take 挂等前 boost owner 至挂
等者优先级(取最强,READY 者摘出重挂);give 还 base_prio 再按剩余
挂等者重升。base_prio 置结构尾——**前部偏移是 asm 契约不许动**(站17
名字长度栈下标的血缘教训:C 块的 q@96 断言依赖布局稳定)。

pidemo 饥饿反转判据的初版是掷硬币:mid 首条 spinning 落 t=150 与 low
完工同拍,顺序判据同拍竞速。修为进旋即打(t=50),变成 100 tick 的
确定性间隔。终版弧:low 9 进临界区 → mid(7) t=50 醒后饿死 low →
high(6) t=100 挂等 → **boost low 9->6** → low 反超 mid 完工 →
high 得锁 → 还继承回 9——**9→6→9 全弧 + 反超顺序**双证;无 PI 则
low 永饿、判据必红,不可假阳。

## C 块:per-task FP 帧

存取点=RT_SWITCH_BOOKKEEP 宏——**rt_switch_to 与 guest_irq 两个
切换点共用一宏,一处扩展双点生效**;旧任务(x2)存 16 对 stp+
fpcr/fpsr,新任务(x0)装;整数帧已压栈,x9/x10 作临时自由。偏移契约
(q@96/fpcr@608)以 `_Static_assert(__builtin_offsetof)` 钉死——改
结构必编译期炸。帧即完备第三兑(泵 per-VM FP 同哲学)。

汇编小雷:64 位 stp 量程 ±504 不够 608,fpcr/fpsr 两条 str/ldr;128 位
q 对量程到 1008 无碍。fpdual:A=Σi→500000500000(复用站16 闭式),
B=Σ(i×0.5)→250000250000(半值闭式,每项与部分和皆 <2^53 精确),
跨 tick 抢占交错双精确=per-task 帧无损自证。

## D 块:vCPU>1(双核真并发)

**探针先行**(站4 方法论二选一必落地):raw 源码镜像被企业网墙,改走
活探针——`-smp 2` 下 CPU1 零异常记录=上电即 off;QEMU 自动 DTB 的
psci 节点 `method="smc"`、`cpu_on=0xc4000003`。**PSCI 探针一发入魂**:
SMC 由 QEMU 内部截获(无需 EL3),`cpu_on -> 0`,CPU1 在 EL2 起立。

拓扑:CPU0=宿主 shell+泵{vm0,vm1}(1s 量子,前台过滤);CPU1 专泵跑
vm2(无量子跑到出,PPI26 不配,PPI27=vm2 vtimer 生命线)。泵拆分=
pump_run_vm 共享体;WFE/SEV 邮箱跨泵握手(vmrun/RX 推送/vmkill 即
poke),**WFE 事件寄存器语义天然防丢醒**。console 自旋锁(按 owner
可递归,行级);VM 状态锁(kill 与在途退场的查后写跨核互踩即表外
panic,锁内重检 RUNNING 者独占入殓)。

### 战况四记(全实测,一门四雷)

1. **el1_trap 桩 x1 自灭**:在 guest x1 入帧前 `mrs x1, mpidr` 作
   scratch——x1 被砸成 cpu 号再进帧(CPU0→0,CPU1→1)。MMIO 的
   SRT=1 读 gprs[1] 恒得 1:vm2 全线 `\x01`(每字节写都送 1),vm0 写
   NUL 无换行永不冲刷全哑。修:x1 押栈绕一圈原值回填。**陷阱桩里
   所有 x0-x30 在入帧前都是 guest 血肉,唯 x0(先押)可自由。**
2. **VMPIDR 虚拟化**:vm2 在 CPU1 真 mpidr=1,Linux 重分发帧发现按
   TYPER 亲和匹配 → "CPU0: mpidr 1 has no re-distributor!" → NULL
   rdist → Oops 炸靴。EL1 的 MPIDR 视角即 VMPIDR_EL2——进场装
   0x80000000,客恒见 cpu0,树/仿真面全一致。宿主读 MPIDR_EL2
   不受影响。
3. **上线打印劈 token**:CPU1 的 online 自报(异步起立)劈进判据
   token 的逐字符回显("E…SHINEOK")。修:握手标志先行,main 出
   提示符前有界等(~500ms;-smp 1 下 PSCI 被拒从容放弃)。
4. **重复迁移对 + 竞态双层案**:vmkill 2 在 [58] 处 panic——表象
   是"跨核迁移竞态",尸检先揪出**自己埋的雷**:poke 插入的 python
   replace 把 kill 的 TEARDOWN/DEAD 迁移对写了两遍(DEAD→TEARDOWN
   二连非法)。修雷之后,真正的跨核竞态(kill 的"查后写"与 CPU1
   在途 vm_finish 互踩)由 VM 状态锁一并收敛。**python 手术每刀
   必 grep 的纪律再立功;竞态归因前先排自伤。**

### 门禁 58→62 与真并行硬证

- [3] cpu1 online / [30] pidemo / [31] fpdual / **[50] 真并行硬证**:
  vm1 复活 spin 3G 独占 CPU0 ~8s 的窗口内,vm2 的内核 boot 行
  **持续行进(实测 210-212 行,时间戳单调)**——单物理 CPU 下 spin
  会饿死一切他客,行进即证第二物理核。判据后交还宿主(后续并发
  窗口要有 vm0 心跳)。
- 存量 58 全数保留同强度,-smp 2 下全量重验(e4 一次通过)。
- [41] 一记闪断(DUAL token 单见,1/3):vm0 前台心跳流 3s+ 无
  shell 回声;e2/e3/e4 三连全绿后不再现。机制未定谳(疑 RX 注入
  跳拍类罕见窗),与 [53] 结构性类不同(该窗已闭合);立观察哨,
  后续站若复现即重启追查,不刷绿。

## 残余与后续

- [41] 闪断观察哨(见上);e1 样本留存 /tmp/gate18-e1.log。
- PI 传递链不做(锁链场景留后续);RTOS 客仍是单核视角(guest SMP
  红线未破:三客 DTB 仍单 cpu,vm2 用 VMPIDR 对齐)。
- vCPU 迁移/负载均衡不做(静态绑定);vmcreate 2 于僵尸窗重造的
  理论竞态记残余(人节奏不触发)。
- console 逐字符回显仍非行级(shell echo;判据 token 期无并发打印,
  握手先行后 [3] 已封)。
- 下一步候选:vgic 事件组/消息队列、per-task FP lazy 化(性能)、
  guest SMP(vm2 第二 vCPU,PSCI 转发设计)。
