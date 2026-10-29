# 站二十 devlog:SMP RTOS(vm0 双 vCPU)——四核拓扑 / per-CPU 内核 / 三层定谳 / 门禁 67→72

> 三 commit 收口:e1edbe3(心跳前台门控)/ d891e0e([41] 五案闭环) /
> 663ba57(A 块:四核拓扑)+ a8fd03b(B+C 块:RTOS SMP 内核与门禁)。
> 本篇按 spec(docs/specs/2026-10-28-smp-rtos-station-design.md)分块实录。

## A 块:四核拓扑(663ba57)

- **-smp 4** 与 vm_cpu_map `{0,3},{0,0},{1,2}`:vm0.vcpu0 留 CPU0 轮转,
  vm0.vcpu1 上 CPU3 专泵,vm2 双 vCPU 在 CPU1/2(19D 机制不动)。
  verify + run_qemu.sh 同步 -smp 4(make run 同构纪律)。
- **PSCI CPU_ON 拆 vm2 硬绑**:目标帧=调用者 VM 下 x1 的 aff0
  (guest MPIDR 视图=VMPIDR 0x80000000|idx),poke 走 vm_cpu_map。
  SGI 陷阱 poke 按命中帧精确制导,不再广播双发。
- **三个 19D 潜伏雷**(新拓扑点名,当场挖出):
  1. vm0/vm1 次核帧从未初始化(nvc 硬码 id==2)——零帧被泵当
     就绪帧入场,pc=0 取指崩;
  2. 共享 vuart 泊位复查唤醒 UART 属主 VM 的**任意**帧——[19] 的
     RX 字节把 vm0.vcpu1 的泊位零帧拽起;复查按 19D 注入规则
     (vSPI33/35 是 vcpu0 的面)收窄 `vi == 0`,SGI 子句保 per-帧;
  3. vm1 的 map 填充值 {0,0} 被新 vi 循环当真帧读——几何表
     n_vcpu 界定一切 vi 循环,幻帧入场死。
- vm0 DTB 诚实树:cpu@1 + psci 节点(method=hvc,cpu_on 0xc4000003)。

## B 块:RTOS per-CPU 内核(a8fd03b)

- **TPIDR_EL1=核号**:主引导写 0,次核 asm 入口写 1;rt_cur/
  rt_fp_owner/rt_fp_stat 三符号转 per-CPU 数组,vec.S 簿记宏
  x9 全程驻核号(q@96/fpcr@608/fpsr@616 断言零移动)。
- **per-CPU 就绪队列**:RT_NCPU×RT_NPRIO 位图+FIFO,rdy_lk[]
  自旋锁——rt_spin_lock 存 DAIF+ldaxr/WFE 自旋,unlock stlr+SEV
  +DAIF 还原;任务结构尾追加 cpu 亲和槽。
- **次核引导链**:PSCI 走 **hvc**(smc 在 EL1 被 QEMU 机内 PSCI
  截吃返 DENIED,19D method=hvc 同律);rtos_secondary_entry 自配
  VBAR_EL1(不配则首拍中断落基地址 0+槽偏移,pc=0x280 实测定谳)、
  TPIDR、.cpu1stack 引导页;secondary_main 把 idle1 伪任务转正、
  自臂本核 vtimer、初始化自家 GICR 帧,自报后入调度循环。
- **SGI 跨核重调度**:rt_ipi 写 ICC_SGI1R_EL1(GICv3 布局:INTID
  [27:24]/TargetList [15:0])→ EL2 EC 0x18 陷阱 → vgic 帧挂起 →
  poke CPU3 → 泵注入 → 次核 IAR 返回 0(GUEST_INTID_IPI)→
  rt_resched_ipi。全链复用 19D 机制,零新 ABI。
- **tick 分治**:核 0 单写 rt_ticks + 唯一唤醒扫描(单写多读);
  跨核唤醒=锁内入目标队+发 SGI。rt_puts 上控制台自旋锁,
  rt_puts_u64 整行组串后一发(旧三段拼行双核互串
  "cpu1: n=cpu0: n=…" smpdemo 实测定谳)。

## 三层定谳:ipidemo rx1=0 的解剖(本站最深一仗)

次核起来了、smpdemo 交错干净、fpsmp 双精确——唯独 SGI 收包数
恒 0。逐步剥洋葱,每一层都真实、都必要:

1. **MMIO 窗**(devices[3].size):vm0 的 VGICR 窗沿 19D 遗留仍是
   单帧 0x20000——次核对帧 1 的使能写落在窗外。按 n_vcpu 放宽
   到 0x40000。
2. **模型帧数**(vgic_reset):`g->nframes` 硬码 `(id==2)?2:1`
   ——帧 1 写全部撞上 `frame >= g->nframes` 早退静默 WI,
   ISENABLER0 读回恒 0(gicr1_en=0 探针实测定谳)。改随几何表
   `vm_geo[id].n_vcpu`。这一层是本战的胜负手:窗口修好后读回
   仍为 0,才逼出了"写进窗≠写进模型"的洞察——vgic 的帧卫兵
   三处(挂起循环/sgi_take/redist 读写)全部吃 nframes。
3. **使能门**(vgic_sgi_take):`pend & sgi_isenabler0[frame]`——
   次核 gic_secondary_frame_init 开 SGI0+PPI27(主核
   gic_dist_init 只配帧 0,GICR 是 per-CPU 面)。

收口判据:开机自报 `cpu1: online (rtos psci) gicr1_en=134217729`
(0x08000001=帧 1 使能回读),ipidemo `rx1=23 / rx0=22 roundtrips`。
配套的诚实面:gic_dist_init 的 Last 自检改双视图——帧 0 非末帧
(Last=0)不再是故障,转证帧 1 身份(PIDR2=0x30+末帧 Last=1);
vm1 单核对照帧 0 即末帧,旧判据原样过。

## ipidemo:回合拍补课

spec C1 写的是**往返**(核 0 发→核 1 回发→核 0 计),首版只落了
单程流。按 spec 补:rt_ipi_echo 回合窗,核 1 handler 逐拍回发核 0
(核 0 不回,免乒乓);`ipi: rx0=N roundtrips` 为判据行,窗内
跨核唤醒推送各得一回,计入即真,判据线只设下限(≥20)。

## C 块:门禁 67→72(a8fd03b,三处同 commit)

- verify:[3] 扩三辅核;+[35] 次核自报+帧 1 回读 0x08000001、
  +[36] smpdemo 交错(cpu1 行≥2)、+[37] SGI 往返≥20+跨核唤醒 3、
  +[38] fpsmp 双精确、+[39] 次核在线主核不回归(ps+fpstat 两核
  求和严格递增);旧 [35]-[67] 顺移 +5。号段手术:全局 /67]→/72]
  后降序 67..35 各 +5,断言每号恰一次,新门最后插入零碰撞。
- shell help:72 步走查全表整块重写 + 新三 demo 命令 + version
  行改四核真话(Block A 遗留的 "2 pCPUs" 陈行一并清理);真机
  `help` 全文肉眼过(站十九嵌合体教训,无残留)。
- ARCHITECTURE:四核拓扑 bullet、站 19/20 RTOS 能力行、72 步
  门禁描述;Makefile "55 步" 陈注释顺手修正。

## 判据与诚实边界

- 门禁:72/72 连续两轮全绿(R1/R2b/R3 三轮全过;中间一轮被
  grep 吞管道产出不明,按"连续"口径以 R2b+R3 计双绿,未藏)。
- [54] 观察哨:本轮三跑零复发(累计 1/~12)。
- 镜像机:a8fd03b 已落盘(daemon 形态四步管线,代理 已恢复)。
- 残余记档:spawn 名字单缓冲、PI 单级不传链、rt_pend 无超时
  (见调研报告 gaps——下一站候选:同步超时面/panic 打印/栈哨兵)。
