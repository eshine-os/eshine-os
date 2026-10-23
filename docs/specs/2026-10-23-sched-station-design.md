<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 27.04 第五站:调度站(时间片抢占轮转)设计 spec

- 日期:2026-10-23 · 版本:27.04-dev · 前置:e1fca4c(第四站中断站,20 步门禁双绿)
- 用户批准:四决策全选推荐(tick 抢占轮转 / shell 泵+vmconsole / 1s 量子 / vtimer_ctx 留下一站单等待者),并全权授权推进至完成

## 0. 目标与真绿定义

单物理 CPU 上多 VM 真并行:busy-loop 的 guest 能被 tick 抢占切走,
另一 VM 不被饿死;shell 与调度器共存(宿主权威);WAIT 语义从"宿主陪睡"
升级为"泵中让位"。真绿=门禁 20 → 24 步连续 2 过(异步时序用
read_until+位置下界消化)。

## 1. 调度模型:shell 泵 + 出口抢占(参考实现同构)

- CPU 权威:shell 是最高优先级任务。等输入处泵调度:每轮给所有
  `VM_RUNNING` 且非 waiting 的 VM 各跑一个量子(=1 tick),跑完回来看
  UART;命令执行期间不泵(宿主原子段)。
- `vmrun <id>` 异步化:CREATED/PAUSED→RUNNING 后立即回 shell;
  死亡/暂停的善后(TEARDOWN/DEAD、打印)集中到泵路径。
- 无 runnable 且全 idle:忙轮询 UART(PL011 中断线未接 GIC,wfi 会
  睡死输入;真板阶段接 SPI 再改 wfi,注释留演进位)。
- 抢占判定形态对齐参考实现:tick 只置 `need_resched` 标志,**真正的切换
  集中在 IRQ 出口**(置帧转写,见 §2)。
- runqueue 不建独立队列:静态 vm_table 遍历(YAGNI,双 VM 规模)。

## 2. 抢占路径:lower-EL IRQ 出口帧转写

- vectors.S 的 lower-EL IRQ 槽改接新桩 `el2_trap_guest`(与 el2_trap
  同款 excp_stack 存帧布局,256B);宿主两槽的 el2_trap 原样不动
  (宿主态 tick 不消耗量子,只服务 timer)。
- C 分发拆 `el2_irq_host`(原 el2_irq)与 `el2_irq_guest`:
  guest 版 tick 分支递减 cur_vcpu 所属 VM 的 slice_left,耗尽置
  need_resched;vtimer/未知分支同 host 版语义。
- 桩出口检查 need_resched:置位则 bl `preempt_save_frame`——把
  excp_stack 帧逐项转写进 cur_vcpu 陷阱帧(gprs[31] 按 vcpu.h 偏移
  契约,x0-x28 出帧,SP_EL1/ELR_EL2/SPSR_EL2 出 sysreg),C 侧已置
  `exit_reason=VCPU_PREEMPT(4)`,随后 vcpu_exit() 弃帧而走(与 HVC
  退出同构;host_ctx.sp 重置宿主栈,excp_stack 帧遗弃无害,入口归位)。
- VCPU_PREEMPT=4 追加原因码;泵收到 PREEMPT 不转状态,继续轮转。
- `struct vcpu` 尾部安全追加 `u64 wait_state`(0=不在等/1=等待/
  2=已醒)——帧偏移契约尾部扩展,asm 桩引用偏移于 vcpu.h 注释同步。

## 3. WAIT 升级与 vtimer 唤醒链(单等待者)

- 泵收到 VCPU_WAIT:wait_state=WAIT + pc+=4(代完成收编),不陪睡;
  waiting VM 不进泵。
- 27 到点(el2_irq_host/guest 皆可能):**只降优先级不挂 LR**,
  `msr cntv_ctl_el0=0` 关线(防电平重挂起风暴),wait_state=WOKE。
  LR 若预挂会被"正在跑的另一 VM"串台消费(per-pCPU 单 LR 通道),
  故注入延迟到该 VM 再被泵到时:泵/vcpu_run 前 wait_state==WOKE →
  `vgic_inject_vtimer()` 挂 LR → 进 guest → ICV 投递 → guest 原
  handler(再 disable 无害)→ 虚 EOI 硬件联动撤线。
- 已知限制(登记+README 说明):单等待者假设——同时仅一个 VM 的
  CNTV 武装着(guest 仅 sleep 命令武装);双 sleep 后武者互相覆盖,
  先睡者永不醒。per-vCPU vtimer_ctx(参考实现形态)留下一站(偏差 #10)。

## 4. UART RX 所有权(vmconsole)

- `vmconsole <id|host>`:全局 console_owner。owner=guest 时 shell
  不读 RX(guest 直读真硬件,RX 缓冲在 QEMU chardev 不丢);owner=
  host 时 shell 读。TX 共用,交错输出是并行性判据素材。
- 自动回收:guest 死亡/暂停路径置 owner=host。
- 形态对齐参考实现 console 归属概念,实现取最小(让位而非陷阱借道)。

## 5. guest 侧:spin 命令(抢占测试体)

- `spin`:忙循环 ~3 秒(cntvct 读秒,无中断依赖)后回 prompt——
  天然的不可让出负载,验证宿主能从任意指令边界切走再无损续跑。

## 6. 门禁手术 20 → 24

异步语义波及全部 VM 步骤(banner 出现时机变异步,read_until 天然
兼容)。新增四判据:
- `vmstat`:双 RUNNING + preempts>0(并行与抢占双证)
- 双 VM 交错:vm0/vm1 各自 token 双回显(vmconsole 切换交互)
- vm0 `spin` 期间 vm1 输出持续(被抢占不让 CPU)→ spin 毕 vm0 续跑
- vm0 `sleep`(WAIT)期间 vm1 持续输出(不让位饿死)→ vm0 slept 后双活
原 20 步保留(tick 递增/sleep 全链/post-guest tick 复验)。

## 7. 参考实现基准对齐(预登记,Task 4 入册)

| # | 形态 | 参考实现 | 新站 | 定性 |
|---|---|---|---|---|
| 11 | 调度器 | 全量线程化(osx_tcb 1:1)+O(1) 128 级优先级位图+同级 RR+ARINC653 可选 | shell 泵+vm_table 轮转+出口抢占 | 同构"置标志+出口切",规模缩减(YAGNI) |
| 12 | console | vuart MMIO 陷阱借道执行宿主 CLI+归属切换 | RX 所有权让位+直读 | 概念对齐,实现最小化 |

参考实现调度全自研(MIT,参考RTOS 派生为零),clean-room 结构参考合规;
流程纪律:只取形态结论,不复用实现细节。

## 8. 风险与纪律

- 帧转写偏移契约是硬编码 asm(vcpu.h 注释同步)——续跑正确性由
  门禁 spin 续跑判据把关
- 异步时序:门禁沿 read_until 位置下界纪律;两轮连过才真绿
- 判绿退出码+LC_ALL=C(中文 locale "错误" 教训)
- 抢占发生在"宿主在跑 guest"的 tick 上;宿主自身代码不可抢占
  (参考实现同哲学:guest 总是被动在出口被切走)
