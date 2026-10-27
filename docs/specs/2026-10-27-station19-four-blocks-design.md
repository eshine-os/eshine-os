<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 第十九站设计:四块合成站([41] 追查 / 事件组+消息队列 / lazy FP / guest SMP)

日期:2026-10-27 · 版本:27.04-dev · 前置:第十八站 c1ceb95(门禁 62 双绿)

状态:设计完成,待实施。

## 0. 决策记录(2026-10-27,用户令"下一站全部需要开发")

| # | 块 | 内容 | 判据 |
|---|---|---|---|
| A | [41] 闪断追查 | 复现循环优先(区域驱动 ×N + 全门禁),定谳即修;不现则诚实结案(观察哨带数据续哨) | 0 或复现修案 |
| B | 事件组+消息队列 | 32 位事件组(AND/OR 双模等待)+ u64 环形消息队列(收发阻塞) | +[32][33] |
| C | per-task FP lazy 化 | RTOS 自建 EL1 sync 向量+EC 0x07 FPEN 陷阱;fp_owner 单一持有+脏位轮转,非 FP 任务零帧税 | +[34] fpstat |
| D | guest SMP(vm2 第二 vCPU) | **-smp 3 三物理核真并行**:vcpu1 独占 CPU2(专泵跑到出),非分时;DTB cpu@1+psci(-smp 3 自动树重裁剪);EL2 截 PSCI SMC(EC 0x17)启 vcpu1;vgic per-vCPU 帧+SGI/IPI;VMPIDR per-vcpu | +[53] +[65] |

实施序 A→B→C→D(稳定性先落地);设计按站规自决。

## 1. A 块:[41] 闪断追查

签名(站18 e1):vm0 前台心跳流 3s+ 无 shell 回声,DUAL token 单见。
疑点:RX 注入跳拍类罕见窗(泵入口注入条件竞态)。**方法**:区域复现
驱动([29]-[41] 段循环,~30s/轮,20 轮)+ 全门禁 ×2;复现即 -d int
尸检+修;不现则结案为"20 轮区域+2 轮全门禁不复现",观察哨带数据
移交站 20。**预算封顶:不复现不无限烧轮次。**

## 2. B 块:事件组+消息队列(rt_ipc.c,站 17 等待队列机器延伸)

```c
typedef struct { u32 flags; rt_task_t *wq_head, *wq_tail; } rt_event_t;
typedef struct { u64 buf[N]; u32 head, tail;     /* 空=head==tail?用计数 */
                 u32 count, cap; rt_task_t *rx_h, *rx_t, *tx_h, *tx_t; } rt_queue_t;
void rt_event_init(rt_event_t *e);
int  rt_event_wait(rt_event_t *e, u32 want, int and_mode, u32 *got);
      /* 重检环:AND 模 (flags&want)==want 才过,OR 模 (flags&want)!=0;
       * 挂等无超时;醒来由 setter 摇醒后自复核 */
void rt_event_set(rt_event_t *e, u32 bits);  /* |=,扫 wq 摇醒满足者 */
void rt_queue_init(rt_queue_t *q, u32 cap);  /* cap<=RT_QCAP(8) */
int  rt_queue_send(rt_queue_t *q, u64 msg);  /* 满:挂 tx 队;醒后复核 */
int  rt_queue_recv(rt_queue_t *q, u64 *msg); /* 空:挂 rx 队;醒后复核 */
```

- 摇醒粒度:setter/send/recv 唤醒队头(与站 17 同律:唤醒=允许重试,
  非授予);事件组按条件扫全队醒满足者(AND/OR 判定逐个)。
- **eventdemo**:waiter 等 AND{A|B};setter 先 A 后 B——第一发后
  waiter 仍睡(AND 不满足),第二发才醒;OR 分支:waiter 等 OR{C},
  setter 置 C 即醒。判据:顺序行(A 后无醒、B 后醒、OR 即醒)。
- **queuedemo**:8 槽环,producer 发 16 条(第 9 条起阻塞),consumer
  收 16 条闭环;收发计数行+一条"full, sender blocked"的阻塞证明行。
- 判据 **[32] eventdemo**(顺序:after A no-wake、after B wake)、
  **[33] queuedemo**(16/16 闭环 + blocked 行在场)。

## 3. C 块:per-task FP lazy 化(真省俭,非摆设)

- **fp_owner 单一持有**:硬件 FP 现场永远属于一个任务;switch 出
  不存、switch 入不装——只在持有权轮转时(save 旧+load 新)。
- **FPEN 按任务开关**:非 owner 任务 CPACR_EL1.FPEN=0 → 首条 FP 指令
  陷阱 EC 0x07(EL1 自己的 sync,ELR=该指令,不退休)。RTOS vec.S
  的 current-EL SP_ELx sync 槽从 guest_park 换成 guest_sync:压统一
  帧 → rt_sync_fault():EC 0x07 且 ISS 系 FPEN → 轮转持有权
  (save 旧 owner、load cur、FPEN=1)→ eret 回原指令重执行。
- 切换面:RT_SWITCH_BOOKKEEP 的 FP 存取撤出,改为
  `if (old != fp_owner) FPEN=0; else FPEN=1;` + trap 路径负责轮转
  (计数器 rt_fp_lazy_saves++)。
- idle/首启任务 FPEN=0(bss 零,不碰 FP 即无税)。
- **fpstat 判据 [34]**:跑 semdemo(纯整型,多任务切换)+fp+fpdual
  后打 "lazy: N switches, M fp-saves"——断言 M << N(M 为持有权
  轮转数)。fp/fpdual 存量判据照旧双精确(lazy 协议下依然成立)。
- 风险:与泵 per-VM FP(站 16)分层无冲突(两层帧各管各的边界)。

## 4. D 块:guest SMP(-smp 3,vm2 第二 vCPU 真并行)

### 4.1 拓扑(vm→(vcpu)→pcpu 静态绑定表)

```
pcpu0:宿主 shell + 泵{vm0.vcpu0, vm1.vcpu0}        (1s 量子,现状)
pcpu1:专泵{vm2.vcpu0}                                (跑到出,现状)
pcpu2:专泵{vm2.vcpu1}——guest 的第二核,PSCI CPU_ON 后才Runnable (跑到出)
```

- **选型理由**:比"双 vCPU 分时 pcpu1"更诚实(真并行)也更简
  (免 pcpu1 量子/PPI26 整套);站 18 的"专泵跑到出"模型零改动复用。
- `vm_cpu[vm][vcpu]` 二维表;cur_vcpus/excp_stacks 尺寸 3;
  `cpu_id()` 上限 3;主 bring-up 循环唤 pcpu1+pcpu2(mpidr 1/2),
  pcpu2 起立即泊(等 guest 的 CPU_ON)。

### 4.2 PSCI 转发(EL2 截 SMC,EC 0x17)

el1_sync 新增 EC 0x17 分支:PSCI 分发(guest 视角固件即我们):
- `0x84000000 PSCI_VERSION` → 返回 `0x00010001`(1.1)。
- `0xc4000003 CPU_ON`(x1=mpidr,x2=entry,x3=ctx):mpidr==1 →
  vm2.vcpus[1] 帧置(pc=entry,x0=ctx,sp=0,pstate=EL1h 全掩,
  帧尾复位)→ poke pcpu2;x0 返回 0(SUCCESS)。
- `0x84000010 PSCI_FEATURES` → -1(NOT_SUPPORTED,Linux 按位探测降级)。
- `0xc4000001 CPU_SUSPEND` → 0(standby 空转语义;cpuidle 走得通)。
- 其余(CPU_OFF/SYSTEM_*)→ -1。
SMC 是完成型调用:处理后按帧续跑(PC 已在下条),x0 回写结果。

### 4.3 vgic per-vCPU(帧地址即身份,GICv3 的本义)

- `struct vgic` 的 SGI 面(sgi_*)数组化 [2];RD 面 rd_ctlr/waker[2]。
- GICR 窗口 0x20000→**0x40000**(两帧);`帧号 = off>>17`,帧内偏移取
  低 17 位。TYPER:帧 0 → affinity 0|Last?不 Last(Last=帧 1 置),
  帧 1 → affinity 1|Last=1(Linux 帧走发现按 TYPER 亲和匹配)。
- **GICD_SGIR(0xF00,现 WI)**:解码 TargetListFilter/CPUTargetList/
  INTID → 置目标 vcpu 的 sgi_ipendr0 → poke 目标 pcpu。IPI 通道
  (SMP 引导期 resched/tlb 全靠它)。
- 泵入口注入:sgi pending&&enabled → 注 LR2(纯虚拟,intid 即 SGI 号,
  优先级随 sgi_ipriorityr;虚 EOI 硬件联动撤)。挂起位在注入时清,
  与 uart 同律。
- 门控 vgic_ppi_enabled 等 API 按访问帧号索引([2] 化)。

### 4.4 其余面

- **DTB**:QEMU `-smp 3` 自动 dumpdtb 重裁剪(站 14 同律:留
  cpus{cpu@0,cpu@1}+psci(smc)+memory+gic(reg 双帧宽)+timer+uart+
  virtio+model "eshine-os,vm");busybox 面板树同源重裁。
- **VMPIDR per-vcpu**:泵进场 `0x80000000 | vcpu_idx`(帧里加 idx 字段,
  vm_create 置)。
- **vuart fg 判定改按 VM**:owner 的 VM == cur 的 VM 即透传(vm2 的
  cpu1 输出与 cpu0 同律,不再按 vcpu 指针判)。
- **sweep**:谓词扩为 (vm,vcpu) 对——每 pcpu 只扫自家客自家帧。
- **poke_cpu(n)**:泛化 sched_poke_cpu1;WFE/SEV 同律。

### 4.5 判据

- **[53] SMP bring-up**:`[vm2] smp: Bringing up 1 secondary CPU(s)`
  在场(Linux SMP 引导行)。
- **[65] 双核自证**:busybox `cat /proc/cpuinfo` 含
  `processor\t: 1` 行(guest 内第二处理器可见)。
- 存量 62 步全保同强度(-smp 3 全量重验;[49] 起始行、[50] 真并行、
  [54] 并发窗口等不降级)。

## 5. 门禁 62 → 67(只增不减)

+[32] eventdemo +[33] queuedemo +[34] fpstat 插 vm0 独占段
([31] fpdual 后;旧 [32..49]→[35..52],旧 [50..52]→[55..57]);
+[53] smp bring-up(boot 行)+[65] cpuinfo processor 1。
编号映射:N<32 同;32≤N≤49 +3;50≤N≤60 +5;61,62 +6(为 [53]/[65]
让位)。三处同步单一 commit(verify/help/ARCHITECTURE,
git show --stat 验三件在——站 17 教训)。

## 6. 风险与残余

| 风险/残余 | 处置 |
|---|---|
| A:不复现 | 预算封顶结案,观察哨带数据移交;不无限烧轮次 |
| B:事件组唤醒惊群 | AND/OR 逐个判件,只醒满足者;惊群残余记册 |
| C:EC 0x07 陷阱路径自己用 FP? | handler 纯整数(asm 存取),fp_owner 轮转在开 FPEN 前完成 |
| C:fp_owner 与 idle | idle 恒非 owner;持有权随陷阱轮转,bookkeep 只拨 FPEN |
| D:SGI 时序(IPI 风暴) | 挂起位注入时清+虚 EOI 硬件联动;泵间 poke 兜丢醒 |
| D:Linux SMP 引导期跨核同步竞态 | 站 18 真并行机器上实测定谳;[54] 窗口判据不降级 |
| D:-smp 3 TCG 三线程开销 | 全门禁时长可控(站 18 -smp 2 实测 ~6min/轮) |
| D:DTB 重裁剪遗漏节点 | 树裁剪与 QEMU 自动树逐节点 diff 自检(站 14 同法) |
| D:CPU_SUSPEND 语义放水 | standby 空转返回 0 记残余(power_level>0 未建) |

## 7. 偏离登记(预登记 #23,随实现 commit 落 baseline-deviations.md)

> #23 站十九四块:A=[41] 追查结案口径(复现修/不复现结案);B=事件组
> +u64 消息队列(参考RTOS IPC 公开语义最小化:32 位 AND/OR 等待+环形收发
> 阻塞);C=per-task FP lazy(fp_owner 单一持有+EC 0x07 FPEN 陷阱,
> RTOS 自建 EL1 sync 向量);D=guest SMP(-smp 3 三物理核,vm2 第二
> vCPU 真并行:PSCI-SMC 转发、vgic per-vCPU 帧+SGI、DTB cpu@1+psci
> 重裁剪、VMPIDR per-vcpu)。门禁 62->67。用户 2026-10-27 令
> "下一站全部需要开发"。
