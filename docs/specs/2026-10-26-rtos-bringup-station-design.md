<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 第十六站设计:RTOS bring-up(VM0 长出真任务内核)+ SIMD per-VM 帧

日期:2026-10-26 · 版本:27.04-dev · 前置:第十五站 199ed5a(门禁 48 步双绿)

状态:设计完成,待实施。

## 0. 决策记录(2026-10-26,用户四案全选推荐线)

| # | 决策 | 选定 |
|---|---|---|
| 1 | RTOS 参考源基准 | **参考RTOS 公开设计为基准**;参考实现 `rtos-kernel/` 树一页不翻(参考源纪律不破),参考实现只取拓扑愿景(VM0=RTOS 槽位) |
| 2 | 最小人格范围 | **调度人格**:优先级位图抢占 + tick + 任务模型(创建/切换/休眠/退出)+ idle + RTOS 壳 + HVC 兼容层 |
| 3 | SIMD per-VM ctx | **本站入帧**(站 14/15 残余清账);RTOS 出 fp 自证命令,与 Linux 双 FP 客并跑判据真成立 |
| 4 | 门禁终点 | **双客并发全证**:48 存量全保,vm1 裸机对照组保留,vm0 旧 needle 全数重锚,RTOS 新判据(壳 token/多任务交错/双客时序/fp 自证/HVC 面存活) |

**参考源纪律重申**(本站红线):`rtos-kernel/` 属参考实现派生区(参考RTOS/Linux 混合,聚合
按 GPL 污染对待),"视同不存在,连行为参考也不进"。本站 RTOS 的人格参照走
**参考RTOS 公开设计**(2017 年开源的 Apache-2.0 RTOS,公开 API/调度形态:
优先级抢占 + tick + 任务模型),对齐先例:ARM ARM(站 4/16)、virtqueue 公开
设计哲学(站 14)、PL011 TRM(站 13)。clean-room 出身不破。

## 1. 拓扑与槽位

```
QEMU loader: rtos.bin   @ 0x50000000  VM0 = eshine-rtos(新)
             guest2.bin @ 0x51000000  VM1 = 裸机对照组(不动,vm1> 壳)
             Image+initramfs          VM2 = Linux 6.6(不动)
```

- **VM0 语义升格**:裸机 demo → 真任务内核,与终态愿景(VM0 RTOS 管理面)对齐。
- **`guest1.bin` 退役**(GUEST_ID=0 裸机镜像下线;同源双身份的另一半 guest2.bin
  留任对照组)。`Makefile` all 目标改为 `rtos.bin guest2.bin`。
- RTOS 链接几何与裸机客相同:IPA 0x40000000(stage-2 换算宿主 PA 0x50000000),
  PHDRS 三段(偏离登记 #4 形态),raw bin。
- **目录形态**:新顶层 `rtos/`(与 `guest/` 平级);**共享客侧 ABI 头**
  `guest/*.h`(miniuart/gic/hvc/timer/virtio/fdt 是陷阱 ABI,两镜像同一套),
  经 `-Iguest` 引用,零复制。

## 2. RTOS 人格(参考RTOS 味最小内核,`rt_` 命名)

### 2.1 任务模型

```c
typedef struct rt_task {
    /* asm 契约(rt_switch):x19-x30 压任务栈,sp 保存在此 */
    u64 sp;                    /* 切出点 SP;0 = 未启动(首切走伪造帧启动) */
    void (*entry)(void *arg);
    void *arg;
    u8 prio;                   /* 0 最高,31 最低 */
    u8 state;                  /* READY/RUNNING/SLEEP/BLOCK/DEAD */
    u16 wait_mask;             /* BLOCK 等待位(本站仅 RX 位;通用原语留十七站) */
    u64 wake_tick;             /* SLEEP 唤醒 tick 数 */
    u64 slice_left;            /* 同级轮转量子(tick 计) */
    struct rt_task *next;      /* 同优先级就绪环(RR 轮转) */
    char name[12];
} rt_task_t;
```

状态机:READY(就绪环上)/ RUNNING / SLEEP(挂 wake 表)/ BLOCK(等 wait_mask
位,本站仅 RX)/ DEAD(退出回收)。`rt_task_create(name, entry, arg, prio,
stack_size)`:静态任务池 + 独立栈,不建运行时分配器(YAGNI,池容量 8)。

### 2.2 调度器(参考RTOS 形态)

- **32 级优先级位图** + 每级就绪环(单向环,next 指下家,入队尾);`__builtin_clz` 找最高
  就绪位(参考RTOS bitmap 形态)。
- **同级 tick 轮转**(防同饿):running 任务 slice_left 耗尽且同级环有他人 →
  环转一格让位。不同级恒抢占。
- **切换点集中两处**:tick ISR 出口(压当前任务帧 → 选最高就绪 → 换)与
  主动点(`rt_yield`/阻塞/休眠进入)。与站五宿主泵"扳机点集中"同哲学。
- **`rt_switch` asm**:callee-saved x19-x30 压栈换 sp;首切伪造返回帧
  (entry 地址进 lr,arg 进 x0)——教科书式任务启动。

### 2.3 tick

vtimer 100Hz 周期(vSPI 27,GUEST_INTID_VTIMER,站七 per-VM ctl/cval 帧现成,
每次 tick ISR 重臂 CVAL+10ms)。tick ISR 出口逻辑:sleepers wake_tick 到期转 READY、
slice 递减/同级轮转、必要时置切标志。空闲时 idle 任务 WFI(陷阱入泵,
vtimer 到点唤醒泵回,站五机制零改动)。

### 2.4 壳任务(IRQ 驱动——人格分水岭)

裸机客是轮询超级循环;RTOS 的壳是**阻塞任务**:RX 无键时 BLOCK(wait_mask
置 RX 位),UART RX 中断(vSPI 33,ISR 收字入 miniuart 环)置壳任务 READY
并触发调度。命令面:

| 命令 | 行为 | 门禁判据(needle 契约) |
|---|---|---|
| `ps` | 任务表 | `TASK ... shell/heartbeat/idle` 三行在场 |
| `spawn <name>` | 动态生任务:打招呼一次即退出 | `<name>: hello from task` |
| `sleep N`(1-9) | **壳任务**睡 N 秒(心跳任务不睡) | `slept (rtos sleep)` + 睡中 `rtos: alive` 行夹在场 |
| `fp` | 生成低优先 FP 自证任务(见 §3) | `fp OK: 500000500000` |
| `yield` | HVC #2 让出-续跑 | `resumed`(HVC 兼容) |
| `host` | HVC #4 交还控制台 | `releasing console...`(HVC 兼容) |
| `exit` | HVC #1 退 VM | 宿主侧 `VM0` DEAD 记录(HVC 兼容) |
| `echo <t>` | 回显 | 双回显存量判据照用 |

心跳任务(常驻,prio 低于壳):每 50 tick 打一行 `rtos: alive tick=<N>`
(N 为全局 tick 计数,0.5s 一行)——**任务级交错的可见判据**。

### 2.5 bring-up 与设备面

与裸机客同一套自证链(诚实:同一套设备面):`pl011 on: amba 00141011`(站
13 身份证)、`gicd v3 ok`/`gic on: 27 33 35`(站 12)、`uart irq on`、
`virtio blk on`(站 10 probe)、`fdt ok:` 三节点(站 11)——needle 文本与
裸机客逐字相同,前缀 `[vm0]`。**FPEN 自启**:RTOS 自己写 CPACR_EL1 `3<<20`
(站十五哲学第三次兑现:CPACR 归 guest,宿主不碰)。

## 3. SIMD per-VM 帧(站 14/15 残余清账)

- **`struct vcpu` 尾扩**(asm 不触及区,vtimer_ctl/sp_el0 同例):
  `u64 q[64]`(Q0-Q31,512B,16 对齐)+ `u64 fpcr; u64 fpsr;`。
- **泵边前装后存**(站七 vtimer 同形,vm.c 泵 `vcpu_run` 调用点两侧):
  切入前 `fpsimd_load(v->q,fpcr,fpsr)`(asm:ldp q 对 + msr),返回后
  `fpsimd_save(v->q,...)`(stp q 对 + mrs)。每回合自洽,无需 prev 追踪。
  宿主零 FP(-mgeneral-regs-only),EL2 的 CPTR_EL2 无 TFP,硬件现场跨宿主
  代码不染。
- **a57 为 v8.0 无 SVE**,Q/FPSR/FPCR 单份无银行,帧即完备。
- **fp 自证设计**:低优先 FP 任务累加 double 1..1000000(Q 寄存器活体,
  汇编级防常量折叠),闭式校验 500000500000.0(double < 2^53 精确表示);
  Linux VM2 同时在跑(宿主泵每量子切客)→ FP 现场每秒跨客往返数十次,
  帧不保则校验必炸。**per-task FP ctx 不做**:仅 fp 任务用 FP,壳/心跳/idle
  FP-free(安全前提记残余,十七站引入通用原语时再议)。

## 4. 门禁 48 → 56(只增不减)

- **存量 48 步全保**;vm0 裸机系 needle(fdt×3/pl011 身份/gicd/gic/uart irq/
  virtio/selftest/echo/双睡公平/spin 等)逐条**重锚 `[vm1]` 同强度**(裸机
  行为一寸不减,只是新宿主是 guest2.bin)。
- **vm0 = RTOS 新判据**(净增 8 项判据):
  1. bring-up 针(banner/设备自证链,needle 文本同裸机);
  2. `ps` 表判据(shell/heartbeat/idle 在场);
  3. `spawn gate16` → `gate16: hello from task`;
  4. 壳 token 双回显(`echo` 系,双 prompt 锚定,站八竞态教训);
  5. `sleep 3` 睡中 `rtos: alive` 夹行(任务级并发硬判据);
  6. `fp` → `fp OK: 500000500000`;
  7. **双客并发时序证明**:Linux boot 行流与 `rtos: alive` 心跳在 log 中
     交错(顺序断言:两 Linux boot 行之间存在 ≥1 行 RTOS 心跳);
  8. HVC 面存活:`yield`(resumed)/`host`(交还)在 RTOS 壳下重验;exit 由
     vmkill 步骤面覆盖。
- 三处同步纪律:verify + 宿主壳 help(版本行) + ARCHITECTURE 验证节同 commit;
  连续 2 轮全绿为准。

## 5. 构建与工件

- `Makefile`:`rtos.bin` 目标(`rtos/*.c rtos/start.S rtos/vec.S`,
  `-Iguest`,`-T rtos/linker.ld`);`all/run/verify` 依赖改
  `$(TARGET) rtos.bin guest2.bin`;guest1.bin 目标退役。
- `scripts/run_qemu.sh`:槽 0 加载 `rtos.bin`(几何不变 0x50000000)。
- 全仓 Apache-2.0,**零外部工件**(RTOS 自研,与 Linux/busybox 外部工件
  模式不同——本站新增件全部自有)。

## 6. 风险与残余

| 风险/残余 | 处置 |
|---|---|
| RX 中断驱动壳:裸机壳是轮询(-2 → WFI),RTOS 壳 BLOCK-唤醒,路径新 | 站八 RX 注入(vSPI 33)现成;ISR 置 READY 位即醒,机制面零新硬件 |
| vtimer 100Hz 周期重臂 | 站七 per-VM ctl/cval 帧现成,tick ISR 重臂 CVAL,泵零改动 |
| 双客同灌 vuart,串行 log 顺序判据 | TCG 下泵量子稳定;FVP 未验(记残余,同站 14 口径) |
| per-task FP ctx 无 | 安全前提:仅 fp 任务用 FP;十七站通用原语时再议 |
| 心跳行进 shell 会话会打乱行缓冲 | 心跳行独立 `\n` 收尾,verify 判据按"行内包含"锚,不与 token 行互斥 |
| 静态任务池 8 | 池满 spawn 报 `pool full`,门禁不触发(3 任务 + 2 动态 < 8) |

## 7. 偏离登记(预登记 #20,随实现 commit 落 baseline-deviations.md)

> #20 RTOS 形态:参考实现 rtos-kernel(参考RTOS/Linux 派生聚合,130K 行,"连行为
> 参考也不进")-> 新仓第十六站 参考RTOS 公开设计为基准从零最小人格(32 级位图
> 抢占+tick+任务模型+IRQ 驱动壳),VM0 槽语义升格为 RTOS(guest1.bin 退役,
> guest2.bin 留对照组);另收 SIMD per-VM 帧(站 14/15 残余,泵边前装后存,
> 站七 vtimer 同形)。参考源:参考RTOS 公开设计(Apache-2.0 上游)+ 站 5/7 泵
> 形态(自有)。用户 2026-10-26 四案全选推荐线。
