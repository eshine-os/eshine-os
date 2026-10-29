# 站二十主体设计:SMP RTOS(vm0 双 vCPU,真物理并行 + per-CPU 就绪队列)

> 承接:站二十首项([41] 五案闭环 d891e0e,67 门双绿)已完成。
> 本 spec = 站二十主体。2026-10-28 用户四决策定形。

## 0. 决策记录(2026-10-28,用户令"第一步:修复现有问题;第二步:继续下一站任务")

| 决策点 | 用户选定 |
|---|---|
| vm0.vcpu1 物理安置 | **-smp 4 新专泵**(真物理并行,弃"cpu0 共享"案) |
| RTOS 调度器 SMP 形态 | **per-CPU 就绪队列**(参考RTOS 真形态,弃大锁 BKL 案) |
| 跨核唤醒(IPI) | **SGI 走 vgic 陷阱**(复用 19D ICC_SGI1R 转发,零新 ABI) |
| 双核范围 | **vm0 only**(vm1 留裸机单核对照组) |

用户明知"工程量大,本站难收口"仍选 per-CPU 队列——按站规自决设计细节。

现状基线:master d891e0e,门禁 67 双绿。待办携带:镜像机 同步
(e1edbe3+d891e0e)因本地网络阻塞(用户侧
待重启);[54] spinning 超时单发 1/9 未定谳——本站门禁多轮跑批顺带
观察,复发即用 vmrx/vmtrail 取证。

## 1. A 块:宿主拓扑面(-smp 4)

### A1 四核上电与 cpu3 专泵

- QEMU 参数 3→**4**(verify + scripts/run_qemu.sh 同步,make run
  同构纪律)。宿主 boot 序新增 `smp_boot_secondary(3, 3)` +
  有界等扩 `sched_cpu_up[3]`;自报行 `cpu3: online (psci cpu_on)`
  (与 cpu1/cpu2 同律,站 20 行原子整串)。
- `vm_cpu_map`:`{0,0},{0,0},{1,2}` → **`{0,3},{0,0},{1,2}`**
  (vm0.vcpu0 留 cpu0 与宿主泵轮转,vm0.vcpu1 驻 cpu3 专泵)。
- `sched_pump` 硬编码 `pump_run_vcpu(id, 0)` → **vi 循环**
  (`vm_cpu_map[id][vi] == cpu_id()` 过滤)——19D 注释里早已点名的
  遗留,本站清偿;现有拓扑下行为不变。
- cpu3 专泵 = 既有 `pump_cpu(3)` 零改动(WFE 睡,SGI/poke 醒)。

### A2 PSCI CPU_ON 泛化(拆 vm2 硬绑)

exceptions.c CPU_ON 分支现硬绑 `vm_vcpu_idx(2, 1)` + x1 低字节
必须为 1。泛化:

- `vm = vm_of_vcpu(v)`(发起客),`vi = x1 & 0xff`(客视角 MPIDR
  = VMPIDR = 0x80000000|idx,aff0 即帧号);
- 校验:`vi != 0`(不许 on-line 自己/核 0)、`vi < ESHINE_MAX_VCPUS`、
  目标帧未活;
- 帧初始化照搬现 vm2 路径(gprs 清零、pc=x2 入口、sp=x3 自配、
  EL1h+全掩、vtimer ctx 清、ws=0 出泊);
- poke 按 `vm_cpu_map[vm][vi]` 定核(新 `sched_poke_cpu3`)。

### A3 SGI poke 泛化

EC 0x18 SGI1R 分支现在 poke cpu1+cpu2 双发。改为**按命中帧逐个
poke 其映射核**(命中 vm0 帧 1 → poke cpu3;vm2 帧 → cpu1/2)。
取走/注入路径零改动(19D 机制已按帧)。

### A4 vm0 DTB 诚实树

tools/dtb/eshine-vm.dts cpus 节点补 `cpu@1`(reg=<1>,cortex-a57)
+ psci 节点(method="hvc")。RTOS 不解析 DTB 引导(PSCI 直呼),
诚实树是站 19 纪律的延续,非功能面。

## 2. B 块:RTOS per-CPU 内核面(深水区)

### B1 per-CPU 数据与访问面

- **TPIDR_EL1 = 本核号**(0/1):主核 boot 初值 0,次核入口自写 1。
  C 侧 `rt_cpu_id()` = `mrs tpidr_el1`(免每次读 MPIDR)。
- `rt_cur` → **`rt_cur_arr[2]`** + `#define rt_cur rt_cur_arr[rt_cpu_id()]`
  ——C 全树读写点零改动;
- `rt_fp_owner` → **`rt_fp_owner_arr[2]`**(每核各持各的 FP 硬件)
  + 同款宏;
- `rt_fp_stat` → **`rt_fp_stat_arr[2]`**(asm RMW 拆 per-CPU,根除
  双核计数互踩;fpstat 显示两核求和);
- **vec.S `RT_SWITCH_BOOKKEEP`** 三符号改经 TPIDR 索引:
  `mrs xN, tpidr_el1` + `ldr =arr` + `[xN, lsl #3]`——契约改动,
  rtos.h `_Static_assert` 家族不动(q@96/fpcr@608/fpsr@616 不变)。

### B2 per-CPU 就绪队列

- `rdy_map/rdy_head[]/rdy_tail[]` → **`[2]` 套**:每核 32 级位图
  + 每 prio FIFO,互不相干;
- 任务结构尾追加 **`u32 cpu` 亲和槽**(置于 fpsr@616 之后,帧契约
  断言零移动;sizeof 变化由池静态数组消化);
- 创建即定亲和:boot 世界(shell/heartbeat/idle)亲和 0;次核自建
  本核 idle 亲和 1;demo 命令按参入列。
- **自旋锁 `rt_spin_lock`**(RTOS 首个自旋锁:ldaxr/stxr + WFE/SEV,
  按核递归免自死):护两条临界面——①目标队列的跨核入列(waker 与
  目标核 pick 并发)②**rt_puts 跨核行原子**(现 DAIF 掩只防本核
  抢占,双核下心跳行会互串——站 20 [53] 的教训反用在 RTOS 侧)。
  本核自队操作照旧免锁(单写者),跨核入列必锁。

### B3 tick 与唤醒分治

- **rt_ticks 由核 0 记账**(宿主三核一钟同律,防双计),核 1 的
  tick 只做本队轮转与唤醒扫描;
- 每核自臂本核 vtimer(100Hz);唤醒扫描(rttask_wake_sleepers/
  wake_all)按任务的 `cpu` 槽**入目标队**:亲和本核直接入列,
  亲和对核**锁内入列 + 发 SGI**;
- 睡账仍全池单表(wake_tick 对全局 rt_ticks,核 0 推进,核 1 只读
  ——单写多读无锁)。

### B4 次核引导链(RTOS 侧)

1. 主核世界就绪后 PSCI CPU_ON(mpidr=0x80000001,
   entry=rtos_secondary_entry,ctx=1)(SMC conduit,同 vm2 判据面);
2. `rtos_secondary_entry`(asm):载本核栈顶(linker 新增 cpu1 栈段)
   → TPIDR=1 → 转 C;
3. `rtos_secondary_main(1)`:GIC 本核 ICV 面 enable + vtimer 自臂
   + 本核 idle 帧伪造 + DAIF 解掩 → 自报 `cpu1: online (rtos)`
   → 本核调度循环(与 rt_idle_loop 同构,noreturn);
4. 次核 idle = 本核队列空时 WFI(泵判 WAIT),醒源 = 本核 tick 27
   或 SGI——泵停泊复查已含 `vgic_sgi_pending`(站 19 修,TIMER 停泊
   也醒,零新增)。

### B5 跨核唤醒链(端到端)

核 0 唤醒亲和 1 的任务 → 锁内入核 1 队 → 写 ICC_SGI1R_EL1(靶 1)
→ EL2 陷阱(EC 0x18)→ vgic 帧挂起 → poke cpu3 → cpu3 泵注入 →
次核 vIRQ → RTOS SGI handler(新,rt_irq_dispatch 增 SGINTID 分支:
重扫描本队即切)→ 目标任务上核。**全链复用 19D 机制,零新 ABI。**

## 3. C 块:demo 与门禁

### C1 demo 命令(展示即判据)

- `smpdemo`:起两个亲和 0/1 的计数任务,各跑 3s 自旋计数并周期
  打行(`cpu0: n=…` / `cpu1: n=…`)——双核行交错即证;
- `ipidemo`:核 0 任务发 SGI→核 1,核 1 handler 回 SGI→核 0,数
  往返拍:窗口内 `ipi: N roundtrips in T ticks`(N 下限入判据);
- `fpsmp`:两个 FP 任务分驻两核(亲和 0/1),Σ1e6 双精确
  (500000500000 ×2)——per-CPU FP 帧无损,rt_fp_owner_arr 双持。

### C2 门禁 67 → 72(只增不减)

| 新步 | 判据 |
|---|---|
| [3] 扩 | cpu1/cpu2/**cpu3** 三行 online(增强存量,不挪号) |
| +[35] | RTOS 次核自报 `cpu1: online (rtos)`(PSCI 泛化判据) |
| +[36] | smpdemo 双核计数行都在且交错(cpu1 行数 ≥2) |
| +[37] | ipidemo 往返数 ≥ 判据线(SGI+跨核唤醒全链) |
| +[38] | fpsmp 两核双精确(per-CPU FP 帧) |
| +[39] | 次核在线时主核判据不回归(ps 行/fpstat 求和如常) |

号段:插在 [34] fpstat 之后,[35]-[67] 顺移 +5 → **72 门**。
三处同步(verify 72 表/shell help 72 行/ARCHITECTURE 含 -smp 4
拓扑)同 commit,git show --stat 验三件在。

## 4. 风险与诚实边界

- **-smp 4 时序面**:vm2 靴预算([52] 90s/[53] 120s)按 4 线程 TCG
  重验;宿主 QEMU 4 线程 + 门禁 python 同机,资源面本机可容;
- **[41] 家族新面**:cpu3 专泵 + vm0 双帧并发——vuart/console 锁
  已按核递归跨核互斥(站 18D+20),新面主要是 vgic 帧并发(19D
  机制按帧,vm2 已实证);
- **帧契约红线**:vec.S 改动只动簿记三符号,帧偏移断言零移动;
  改动编译期炸 = 契约机器盯(站 17 教训);
- **[54] 观察哨**:站内每轮门禁即采样,复发即取证(工具在树);
- **镜像机**:代理 恢复后一次快进 e1edbe3+d891e0e+本站收口 commit;
- 预算:A 块半天、B 块一至两个工作单元(深水)、C 块半天——分
  段可验,每段独立 commit+门禁,不追求一日全站。

## 5. 偏差登记预告

per-CPU 就绪队列+TPIDR 索引偏离"参考RTOS 公开设计基准"的最小形态
(参考RTOS 真身即 per-CPU 调度,实为**对齐**而非偏离);-smp 4 为
新增偏差条目(基线 3 核);DTB cpu@1 同诚实树纪律。
