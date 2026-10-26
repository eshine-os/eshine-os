<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# Devlog 16 — RTOS 站(参考RTOS 味任务内核上 VM0)+ SIMD per-VM 帧

日期:2026-10-26 · 版本:27.04-dev · 门禁:55 步真绿 ×2 连过

## 里程碑

VM0 从 185 行裸机超级循环升格为真任务内核。VM1 留任裸机对照组,
VM2 是 Linux——三个异构客在同一颗 EL2 下分时:

    [vm0] eshine-rtos 27.04-dev @EL1 via stage-2 (vuart @trap)
    [vm0] rtos: up (eshine-rtos 27.04-dev) 32-prio bitmap, tick 100Hz
    [vm0] rtos> ps
      shell prio 5  RUN
      heartbeat prio 10  SLEEP
      idle prio 31  READY
    [vm0] fp OK: 500000500000      ← SIMD 帧验收:1e6 加法跨任务/泵切换存还
    [vm0] rtos: alive tick=N        ← Linux boot 流中夹行:真并发

## 形态

- **参考RTOS 公开设计为基准**(用户四案之首):32 级优先级位图(ctz 摘
  最高)、同级 tick 轮转、`rt_task_t{sp@0}`、`rt_*` 命名;参考实现
  `rtos-kernel/` 树一页未翻(参考源纪律不破)。
- **统一任务帧 272B**:IRQ 抢占帧/让出帧/首启动帧三源同构——elr 槽
  自描述续跑点(中断处/`rt_switch_to` 的 .Lcoop_ret/trampoline),
  续跑只有 `.Lpop` 一条路;C 只做选择,asm 做簿记(旧->sp=帧顶,
  rt_cur=新,sp=新帧顶)。
- **IRQ 驱动壳**:RX 中断入环即 ready 壳任务——裸机客轮询循环与
  RTOS 的人格分水岭。心跳任务 0.5s 一行,行级 DAIF 临界区打印
  (行间交错正是并发判据)。
- **miniuart.h 的 static 环是个陷阱**:头文件 per-TU 各持一份 RX 环,
  多编译单元会串不成一环——RTOS 自建 rt_uart 环,ISR 与壳共享。
- **fp 自证**:`-mgeneral-regs-only` 仓级生效,guest 侧 FP 只能手写
  (scvtf/fadd/fcvtzu 串 1e6 项 double 和,闭式校验 500000500000,
  精确表示 <2^53)——防常量折叠天然成立,FPEN 由 RTOS 自设(CPACR
  归 guest,站十五哲学第三次兑现)。

## 实现期第一轮:两个 RTOS 本体根因

1. **`rt_switch_to` 绝不能标 noreturn**。续跑路径经 .Lcoop_ret 回到
   rt_sched 调用点之后——标了 noreturn,GCC 在调用点后排的全是环
   回头/死代码,续跑落进去就踩:第一次是空指针炸(帧里 x1=NULL+0x18
   的 FAR),前一个 build 是落进 puts_uart 无限打。摘掉属性,GCC 排
   正常收尾,续跑经 epilogue 干净回 rt_block_rx 的调用者。
2. **`rt_sched` 空转语境必须 WFI**。壳 BLOCK 后若 pick 不到人就热
   自旋;WFI 陷阱入泵、醒来重扫,IRQ 出口可能已直接切走我们。

## 实现期第二轮:异构双客炸出共享物理面(五组收编)

同源双镜像时代(vm0/vm1 同一裸机 bin),全 EL 共享一份的物理面
泄漏**不可见**——两个镜像的向量表地址恰好相同。站十六 RTOS+Linux
异构并跑,一天之内炸出全部:

| 泄漏面 | 症状 | 修法 |
|---|---|---|
| **VBAR_EL1** | vm1 引导换向量表,vm0 下一个注入中断跳错门——boot 尾三行原样重演后停摆(幽灵输出),int 日志定谳 | 帧收编,泵边前装后存 |
| **SCTLR/TTBR0/TCR** | vm0 向量取指经 Linux 的 stage-1 表翻到垃圾 IPA,指令中止死(HPFAR 对不上向量页) | 同上,三件套入帧 |
| **CPACR_EL1** | vm0 设 FPEN,他客若清零则 FP 断腿(未爆,同类预防) | 同上 |
| **ICH_LR0/ICH_VMCR** | GIC 虚拟面单点:泵为 vm0 注入唤醒复用同颗 LR0,覆写 vm2 未认领的 tick——Linux jiffies 冻死、靴停首个 msleep | 同上;**恢复必须在 WOKE 注入之前**,放后面会吃掉刚注入的 v27(22 步回归实测) |
| **SIMD(计划内)** | 双 FP 客 Q 寄存器串台(站 14/15 记案) | 16 对 stp/ldp + FPCR/FPSR |

站十四的"出生清一次 stage-1"只保新鲜引导;异构双活客互换时照样
串。帧尾五组收编后与 vtimer/sp_el0 同族——**全 EL 共享物理面的
per-VM 化,是本站给这个 15 站架构补的最后一块结构性地基**。

## 实现期第三轮:陷阱量子的生灭(最贵的一课)

[47] 并发判据初版要求心跳出现在 Linux boot 早期行流间。但 Linux
早期 **PSTATE.I=1**:host-tick 进不了 EL2,1Hz tick 量子永不耗尽,
泵单客模式直跑——活客被饿死不是宿主失职,是 guest 自掩蔽。

为救这个判据,我造了**陷阱量子**(512 MMIO 陷阱/片,el1_sync 出口
双扳机抢占)。它让 Linux 掩蔽相的片有了尽头,却把宿主相频率拉高
两个数量级,撞上一组时序组合拳:

- `vgic_wake_waiter` 对**非等待客**的到期也无差别"关线"
  (`cntv_ctl=0`),落在泵出口保存窗内会把 armed 存成 0 → WAIT 归
  类 RX → 永久停摆(冻结尸检:vm2 帧 `wait_reason=RX+ctl=0` 铁证);
- **未认领的挂起 27 活不过 VM 切换**:HW LR 的 pending 由物理线
  驱动,下一位客装自己的 vtimer 时线一落,该拍即蒸发——旧 1Hz 慢
  轮转里拍总在同片认领,快轮转跨片挂起成常态。

对照二分(纯提交基 3/3 稳;+共享面 3/3;+陷阱量子即 2/3~50%
闪断,逐项撤除均不愈)把责任钉死在陷阱量子引入的轮转频率上。
**判据窗口改到解掩蔽段后**(GICv3 针后被动收 5s,泵 1s 一片、
心跳两行/片,余量足),陷阱量子整组退役,回归站五 tick 量子。
泵出入口 DAIF 双屏(出口段 vtimer 最先存、入口段 vbar/cpacr/
stage-1 先装 vtimer 最后装直进)保留封窗。

附带:EL2 的门禁相对论——日志里 "exception 1 [Undefined
Instruction]" 其实是 WFI-TWI 陷阱类(EC=1),QEMU 的标签会骗人;
裸探针的"冻结"可能只是没装 initramfs 的预期 VFS panic(X14-X17
寄存器里躺着 "vfs: Unable to mount" 的 ASCII,认尸要认全)。

## 战法记事

- 二分纪律的坑:python 锚点 assert 失败会静默放弃写入,而下一条
  make/verify 照跑——"撤了还冻"的结论可能来自一次没落盘的手术。
  每刀之后必须 grep 确认改动真在文件里。
- 25 秒探针窗口分不清"冻结"和"慢靴";判读前先放宽窗口看末行。
- 门禁收尾时 [46] 无锚 read_until 会吃进下一针的陈货,[47] 用
  rindex 锚末次出现。

## 残余与后续

- 陷阱量子退役后,掩蔽引导期(Linux 早期)泵单客直跑,活客短暂
  饿死——诚实记录为 guest 掩蔽语义,判据窗口避开该段。
- per-task FP ctx 仍未入帧:仅 fp 任务用 FP,壳/心跳/示闲 FP-free
  (安全前提已记 spec §6)。
- 统一帧的 elr 自描述设计可再省一笔:rt_frame_init 只写 4 个槽,
  其余靠 bss 零值——干净但依赖静态阵列清零,链上别的段若踩 bss
  会静默变质。
- spawn 单缓冲名互踩(demo 尺度一次一个,记 spec §6)。
