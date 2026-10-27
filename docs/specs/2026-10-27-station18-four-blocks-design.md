<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 第十八站设计:四块合成站(在途拍守门 / 优先级继承 / per-task FP / vCPU>1)

日期:2026-10-27 · 版本:27.04-dev · 前置:第十七站 6f041b4(门禁 58 双绿)

状态:设计完成,待实施。

## 0. 决策记录(2026-10-27,用户令"下一站4项工作全部需要完成")

| # | 块 | 内容 | 判据 |
|---|---|---|---|
| A | vgic 在途拍守门扩展 | 修 [53] 闪断:宿主相 27 三分支(真唤醒→关线 / 在途→回业主 / 无主→关线) | 门禁 58 存量不动,×3 绿(闪断窗结构性闭合) |
| B | 互斥优先级继承 | 单级 PI(boost/restore+rdy 重挂),pidemo 饥饿反转证明 | +[59] |
| C | per-task FP ctx | vec.S 统一簿记宏内嵌 FP 存取(两切换点一处改),fpdual 双 FP 任务并发自证 | +[60] |
| D | vCPU>1 首步 | QEMU -smp 2 双核真并发:PSCI-CPU_ON 唤起 CPU1(探针实证:CPU1 上电即 off,conduit=SMC),CPU1 专泵跑 VM2 | +[61][62](上线针+自旋窗内 Linux 持续行进=真并行硬证) |

实施序 A→B→C→D(稳定性先落地,最大风险殿后);设计按站规自决。

## 1. A 块:在途拍守门扩展(闪断根治)

**现状真相**(站 17 归因+本次复读):exceptions.c el2_irq_host 27 分支的
注释已写对方案("关线只属睡客路径…RUNNING 轮转客 ctl 不许动"),但代码
仍无条件 `vgic_wake_waiter()`;注释里"见下"的封窗机器(陷阱量子配额
重填)随站 16 整组退役——窗重新敞开,即 [53] 闪断(4 轮 1 挂)。

**修法三分支**(27 到点,宿主相):

```c
u32 woke = sched_vtimer_sweep();
if (woke)
    vgic_wake_waiter();        /* 真唤醒了睡客:没人替它落线,熄线+EOI */
else if (vtimer_inflight())
    gicv3_eoi_drop(GIC_PPI_VTIMER),
    sched_vtimer_recall = 1;   /* 在途拍保活:不碰物理线(HW LR pending
                                * 跟线,关了即蒸发);EOI 只降优先级;
                                * 泵置回业主标志,下一圈优先进场消费 */
else
    vgic_wake_waiter();        /* 无睡客无在途:无主陈旧挂起,熄线(老行为) */
```

**回业主**:泵记录 `sched_vtimer_last = id`(进场时);置 recall 后
`sched_pump` 以 last 为旋转起点(首圈即回业主)——泵由 shell 每圈必调
(排空轮询循环),风暴窗收敛到一圈泵间隙;若业主已非 RUNNING(死/停),
泵顺手 `vgic_wake_waiter()` 熄线收尸。前台模式兼容性:物理 CNTV 恒为
"最后进场者",而前台模式最后进场者=owner,recall 目标恒可跑。

**为何不沿用"gating"**:不关线导致物理线恒挂→EL2 27 风暴(站 16 实验);
本设计里线只挂到下一次泵调用(微秒-毫秒级),每发被三分支廉价吸收,
泵必然回来消费——无死循环路径。

## 2. B 块:互斥优先级继承(单级,参考RTOS 公开语义的裁剪)

- `rt_task` 增 `base_prio`(create 时 = prio);boost 即写 `t->prio`
  (调度器全程读 t->prio,一处改动全局生效)。
- **take 挂等前 boost**:若 owner 当前 prio > 挂等者(数字大=优先低),
  owner->prio = waiter->prio;owner 若 READY 需 `rdy_remove` + 重挂
  (新优先级位图位)。rdy_remove:单级链表扫描(池 ≤8,非热点)。
- **give 还原**:nest 归零后 owner->prio = base_prio,再扫 wq 剩余
  挂等者取最强重 boost(多等待者取 min),然后照旧 handoff。
- **单级不传递**(锁链不追):参考RTOS 有传递 PI,本站裁剪记残余。
- **pidemo**(饥饿反转,经典三任务):
  - high(6):睡 100 tick → take(阻塞)→ "pi: high got lock (after boost)"
    → give → 退场;
  - mid(7):睡 50 tick → 忙旋 400 tick,期间三行 "pi: mid spinning" →
    "pi: mid done";
  - low(9):take 即进临界区,忙旋 ~150 tick,中途打印
    "pi: low running at prio 6 (boosted)"(读 rt_cur->prio 自证),
    完工 "pi: low done (boosted)" → give → "pi: low restored prio 9"。
  - 时序:t=50 mid 醒后永久抢占 low(7 压 9);t=100 high 醒→挂等→
    **boost low 9→6** → low 反超 mid 完工 → high 得锁 → mid 才继续。
    无 PI 则 low 永饿、high 永挂——判据必红,不可假阳。
- **判据 [59]**:"mid spinning" < "low done (boosted)"(顺序=反超证明)
  + "running at prio 6" + "restored prio 9" + "high got lock" 四针在场。

## 3. C 块:per-task FP ctx(帧即完备,站七/站十六同哲学)

- `rt_task` 尾扩 `u64 q[64]`(16 对齐,**偏移 96 静态断言钉死 asm 契约**)
  + fpcr@608/fpsr@616;idle_task 同结构同享。
- **存取点=RT_SWITCH_BOOKKEEP 宏**——rt_switch_to 与 guest_irq 两个
  切换点共用一宏,一处扩展双点生效:旧任务(宏内 x2)存 16 对 stp +
  mrs fpcr/fpsr;新任务(x0)装 16 对 ldp + msr。寄存器预算:帧已压栈,
  x9/x10 作临时自由可用;q 寄存器操作不碰整数现场。
- 每切换全量存取(不做 lazy):100Hz×每秒数次的 4KB 搬运,QEMU 量级无感;
  "帧即完备"哲学第三次兑现(泵 per-VM FP 同款)。
- **fpdual**:双 FP 任务并发(prio 11/12),A 累加 Σi(1e6)→
  500000500000;B 累加 Σ(i×0.5)→ 250000250000(半值闭式,双精确
  表示 <2^53)。跨 tick 抢占交错,两结果皆精确=per-task 帧无损自证。
- **判据 [60]**:"fpdual: A OK: 500000500000" 与
  "fpdual: B OK: 250000250000" 双针在场。

## 4. D 块:vCPU>1 首步(双核真并发)

**探针实证**(2026-10-27,本站设计期落地):`-smp 2` 下 CPU1 零异常
记录(上电即 off);QEMU 自动 DTB psci 节点 `method = "smc"`、
`cpu_on = 0xc4000003`——bring-up 走 **PSCI CPU_ON(via SMC,QEMU
内部截获,无需 EL3)**;SMC 不截获的兜底=改试 HVC(探针先行,实现期
一步定谳)。

### 4.1 拓扑与分工

```
CPU0:宿主 EL2 + shell + sched_pump{vm0, vm1}(1s tick 量子,前台模式)
CPU1:secondary_entry 起,专泵 pump_secondary{vm2}(无 tick,跑到出)
      ——vm2(Linux)与 vm0/vm1 真并行,不再分时
```

### 4.2 per-CPU 面(盘点即工单)

| 全局面 | per-CPU 化 |
|---|---|
| `excp_stack[512]` | `excp_stacks[2][512]`;asm 桩读 MPIDR_EL2.aff0 索引 |
| `cur_vcpu` | `cur_vcpus[2]`;asm 桩 MPIDR 索引;C 侧 `cur_vcpu_get()` 访问器全量改写 |
| 泵 | sched_pump 静态集 {0,1};新 pump_secondary 单 VM 循环;vm2 的 vcpu_run 块提取共享 |
| GICR | CPU1 自配其重分发帧(0x080A0000+0x20000=0x080C0000,TYPER 探针定谳);PPI26 不配(无量子);27 配置(vm2 vtimer);GICD 共享不动 |
| console | console.c 加自旋锁(ldaxr/stxr+WFE):console_puts 与 vuart_tx_flush 行级原子——双 CPU 并发打印,判据行不许串字 |
| CPU 间握手 | WFE/SEV 邮箱:`pump_poke_cpu1()`(置标志+sev);CPU0 在 vm2 状态变更(vmrun/RX 推送/交还)时 poke;CPU1 泵空转 WFE |

### 4.3 起立与判据

- start.S:MPIDR 门(aff0≠0 → secondary_park,不碰 CPU0 的 bss/栈);
  CPU0 main() 末 PSCI CPU_ON(entry=secondary_entry)→ CPU1 配栈/向量/
  GICR → "cpu1: online (psci cpu_on)" 自报 → WFE 泵待命。
- **判据 [61]**:"cpu1: online (psci cpu_on)" 在场(banner 区)。
- **判据 [62] 真并行硬证**:vm1 spin 3G(独占 CPU0 ~8s)窗口内,
  vm2 的 Linux boot 行持续行进(≥3 行,内核时间戳单调)——单物理 CPU
  下 spin 期间 Linux 必然饿死,行进即证第二核真在跑。
- 存量 58 判据全部保留同强度:[47]/[50] 并发判据语义升格为真并行
  (日志交错依旧成立);[53] 解包窗口在专核上更快;vmconsole 2 交互
  走 RX 邮箱;vmkill 2 由 CPU1 再扫状态自然停车。

### 4.4 范围红线(YAGNI)

- guest 不变:三客仍单核视角(树单 cpu 无 psci,诚实),不引入 guest SMP;
- 不做 vCPU 迁移/CPU 间负载均衡(静态绑定);
- 不做 per-CPU pump 抢占(CPU1 无量子,vm2 跑到出);
- A 块守门逻辑 per-CPU 硬件天然生效(CPU1 的 27 分支同源)。

## 5. 门禁 58 → 62(只增不减)

A 块不动步数(存量 58 全保,×3 绿证);+[59] pidemo、+[60] fpdual
插 vm0 独占段([29] 前,旧 29-58 顺移);+[61][62] 插 Linux 段。
三处同步(verify+shell help+ARCHITECTURE)随 D 块收尾单一 commit
(纪律:commit 后 git show --stat 验三件在,站 17 教训)。

## 5.5 实现期纠偏(2026-10-27,门禁五轮实证)

- **A 块真相考古**:el2_irq_host 27 分支注释(站16 e87a3bb)已写对
  方案但代码未落,"见下"的封窗机器(陷阱量子配额重填)又随站16 整组
  退役——窗重开即站17 [53] 闪断。本站把注释方案落进代码(三分支+
  回业主);58 存量 ×3 连绿。
- **D 块战况四记**(全实测):①el1_trap 桩在 guest x1 入帧前 mrs x1
  作 scratch→MMIO 的 SRT=1 恒读 1(vm2 全线 \x01、vm0 全哑)——
  x1 押栈绕一圈修;**陷阱桩里 x0-x30 在入帧前都是 guest 血肉**。
  ②vm2 在 CPU1 真 mpidr=1,Linux 重分发帧亲和匹配炸靴——
  VMPIDR_EL2 进场虚拟化恒见 cpu0。③上线打印劈判据 token 回显——
  握手先行+main 有界等。④poke 插入 python replace 自埋重复迁移对
  (DEAD→TEARDOWN 非法 panic);真竞态(kill vs CPU1 在途退场的查后
  写互踩)由 VM 状态锁一并收敛。**python 手术每刀必 grep;竞态
  归因前先排自伤。**
- **B 块判据时序初版掷硬币**:mid 首条 spinning 落 t=150 与 low 完工
  同拍;修为进旋即打(t=50),顺序判据变 100 tick 确定性间隔。
- **C 块汇编小雷**:64 位 stp 量程 ±504 不够 608,两条 str/ldr
  (128 位 q 对到 1008 无碍)。
- **[41] 闪断观察哨**:e1 双醒双活 DUAL token 单见一记(1/3,e2/e3/e4
  三连全绿不再现)——vm0 前台心跳流 3s+ 无 shell 回声;机制未定谳
  (疑 RX 注入跳拍类罕见窗),非 [53] 结构性类;后续站复现即重启
  根因追查,不刷绿。

## 6. 风险与残余

| 风险/残余 | 处置 |
|---|---|
| A:回业主与泵间隙的重复 27 | 每发三分支廉价吸收;泵每圈必回;无死循环路径 |
| A:前台模式下 recall 目标不可跑 | 物理线恒属"最后进场者"=前台模式下的 owner,恒可跑;死/停由泵熄线收尸 |
| B:PI 传递链不做 | 单级裁剪记残余(锁链场景留后续) |
| C:q 偏移 96 的 asm 契约 | 静态断言钉死(编辑结构必编译期炸) |
| D:PSCI SMC 不被截获 | 探针先行:SMC 试→el2_sync 现形即改 HVC(一步定谳) |
| D:CPU1 GICR 帧地址 | TYPER 探针定谳(0x080C0000 假设,错了不猜第二把) |
| D:双 CPU 打印串字 | console 自旋锁行级原子;判据行完整性为门禁底线 |
| D:TCG 多线程下自旋锁护持者被宿主调度饿死 | 临界区有界(行级),WFE 让出;实测定谳 |
| D:-smp 2 下 58 存量全量重验 | D 块收尾门禁以 62 步全量跑,旧判据一寸不减 |
| D:vm2 双核下 [53] 窗口语义变化 | 更快即更稳;A 块先行已把丢拍路径结构性闭合 |

## 7. 偏离登记(预登记 #22,随实现 commit 落 baseline-deviations.md)

> #22 四块合成站:A 块 vgic 在途拍守门三分支(宿主相 27 真唤醒才关线,
> 在途回业主消费,无主收尸——站 16 注释宣称/代码未落+量子退役后的窗
> 重开,[53] 闪断根治);B 块互斥单级优先级继承(参考RTOS 传递 PI 裁剪,
> base_prio+rdy 重挂+pidemo 饥饿反转判据);C 块 per-task FP ctx
> (RT_SWITCH_BOOKKEEP 一处扩展双切换点生效,q 偏移 96 静态断言,
> fpdual 双精确闭式自证);D 块 vCPU>1 首步(QEMU -smp 2 PSCI-SMC
> bring-up 探针实证,CPU1 专泵跑 VM2 双核真并行,per-CPU excp_stack/
> cur_vcpu/GICR/WFE 邮箱,console 自旋锁行原子;guest SMP/迁移/均衡
> 红线不做)。门禁 58->62。用户 2026-10-27 令"下一站4项工作全部需要
> 完成"。参考源:参考RTOS 公开设计(B)+ 站 7/16 帧哲学(C)+ QEMU 8.2.2
> 探针实证(D)+ 站 16 中断物理分析(A)。
