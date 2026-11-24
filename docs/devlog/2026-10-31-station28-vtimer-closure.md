# 站二十八收口+vtimer 专站 devlog:六雷定谳,81 门双绿

日期:2026-10-31 · 作者:Julian Chu + Claude Code
状态:**已收口**——81 门(新增 [77] utest)连续 2 轮全绿;WIP 检查点
(f0a2602)挂账的"[24+] 游走失败"与站 24 路由的 vtimer 陈疾全案终结

## 0. 案情:一个挂账,三个真身

WIP devlog 立档时把 [24+] 游走失败整体挂账给"站 24 vtimer 陈疾"。
收口猎捕证明挂账下面压着**三个互相独立的宿主/客户机缺陷**+两处
修法自雷——"回复在而 tick 冻结"是同一症状的三种死法:

## 1. 十三雷:.Lel1 弹栈错库——引导栈棘轮(WIP 首脸的真身)

**症状**:门 [24+] 游走;guest 在 tick~250± 处 panic,`far=0x340`,
pidemo 曾一轮 idle-PC 野值。gdb 栈直方图:idle 引导栈(8KB)内
~0x180B 帧形块层层下沉,含 rt_idle_loop 返回址/coop 签名/0x345。
**差分三采样**:剥掉 EL0 壳(纯 idle+tick)idle->sp 三点纹丝不动
(0x40027130)——棘轮只随壳阻塞-唤醒周期走。

**机理**:SPSel=0 语境(EL0 异常处理链,壳 sys_read 阻塞)切 EL1h
目标时,`.Lpop` 顶 `ldp [sp],#16` 把 SP_EL0 推到 GPR 块基,旧舞蹈
`ldr x5,[x0]; msr spsel,#1; mov sp,x5; msr spsel,#0` 弹栈推进全记
在 SP_EL0;eret 后 EL1h 的 sp=SP_EL1 **仍是未弹旧顶**——每轮自
EL0 语境向 EL1h 派发净漏 272B。idle 8KB 栈 ~20 周期踩穿:tick 的
guest_irq 帧(272B)落进 .bss `tp[]` 软定时器表,rt_timer_tick 解
引用 tp[1]=0x340 → data abort → panic_park(psr 0x3C5 掩 DAIF)→
泵唤醒而虚拟 tick 永递不进 = "回复在而 tick 冻结"的 guest 版。
站 16-27 无 EL0 无此路径,故七站双绿掩盖至今。

**修法**:换库前 `mov x5, sp` 抓现值(GPR 块基,库无关——RT_EL0_RET
#11 修的同款习语),`msr spsel,#1; mov sp,x5` 后**留在 SP_EL1 库
弹到底**:弹栈推进即归目标库,eret 后 sp=帧顶+272 全量回卷,与
SPSel=1 原生路径逐字节同构。

**修法自雷(一并入档)**:初版以帧基 `[x0]` 归位补写 SP_EL1 后回
SP_EL0 库弹——漏 +16(`.Lpop` 顶部 ldp 已消费 elr/psr!):弹栈整体
左移一字,寄存器全串位,heartbeat `elr=0x0` IABT(esr=0x86000006)
即时定谳。教训:**帧基 ≠ GPR 块基,重载+补偏的重复记账必错,
抓现值习语消灭之**。

## 2. 十四雷:"无主"三分支漏第四类——滴答永死的宿主真身

**症状**:猎捕器(全任务混合+pidemo 循环)iter 1-25 间随机冻结;
guest rt_ticks 冻结,宿主双 vmstat 照涨(tick/preempts 俱进),
宿主壳活;vmrx 实测 `cntv ctl=0x5`(ENABLED+**ISTATUS=1** 恒放线)、
cval 落后 now ≈40s、`gicr0 pend|act` bit27 双置、vLR0 陈尸 pending
而 guest 未掩、hcr/vmcr 全健康。**trail 全环定谳**:CPU0 最后
两条 27 事件 = `ev15 → ev8`(27 宿主语境起火→"无主"分支),此后
CPU0 27 绝迹。

**机理**:站 18A 三分支(sweep 中泊者/inflight 中 LR 在途/无主)
漏了第四类——**可跑客的活火落在宿主片隙**(量轮换片隙/壳命令窗):
sweep 只认泊者(wait_state!=0),inflight 只认 LR 在途,双不中即
被"无主"收尸(仅 eoi_drop):物理 27 被 claim 成**永 active** 且零
注入;线恒高、active 挡同优先级再投递,滴答永死。协作式世界装没
看见,直到 tick 依赖自旋(pidemo)把它撞上判据面——死点比可见
冻结早 ~40s,首轮全环 trail 因此扑空。

**修法**(el2_irq_host 27 分支第四类):本核 `sched_vtimer_last`
业主可跑且 PPI27 使能→照 el2_irq_guest 律注入,**并回写业主帧
lr_vtimer 槽**(泵下一圈进场装回;无此写则入场恢复以旧帧覆掉
注入=站 16"活不过切换"律),guest 消费后虚 EOI 经 HW-LR 联动撤
物理 active,闭环。

**修法自雷(哨兵碰撞)**:`sched_vtimer_last = (id<<4)|vi` 对
**vm0.vcpu0 打包值=0**,与数组 bss 初值不可分。首版 `own==0 早退`
把唯一受害者拒之门外——一轮猎捕 ev8 后无 ev16 为证,滴答照死。
泵入场(685)必先写,27 能起火则本核必已入过场,0=真业主是唯一
诚实读法;真守门员=VM_RUNNING 态判。

## 3. 十五雷:帧复用清单漏站 16 字段——[79] 向量自陷风暴

**症状**:全量 verify 4/4 死在 [79](重生 vm0 二次引导,"entering
EL1"后全静);壳死(\x1d 无应答)、QEMU gdbstub 连接超时、-d int
单轮 **230MB**。

**机理**(int 日志直书):CPU0 `Taking exception 3 [Prefetch
Abort] ELR=FAR=0x40006200 = VBAR_EL1` 的**向量自陷无限循环**
(L2 翻译错→取异常向量→向量页未映射→再 L2 翻译错…)。根源:
vmcreate 的"槽位复用不复带旧值"复位清单停在站 5-8/14 字段,
**站 16 入帧的共享物理面(sctlr/ttbr0/tcr/vbar/cpacr/lr_vtimer/
vmcr/fpcr/fpsr)从未入列**——[78] exit 存下前世 MMU-on 的 SCTLR,
新客带陈转译入场:引导的 bss 清零抹掉正在翻译的页表,取指即炸。
暖重置最小复现侥幸过关的原因:陈 MMU 窗内恰无 IRQ/TLB miss,
TLB 热路径熬到 mm0_init 重建;verify 语境 vm2-Linux 在场,泵拍
必至,风暴必发。

**修法**:复位清单补全九字段(sctlr=0 MMU 关起步/…/vmcr=0 guest
经 ICV 自配),与冷靴 bss 帧完全同构。

## 4. 十六雷:跨 VM 串台窗——[69] hb 门的脸

**症状**:双绿第二轮死在 [69](`hb replies < 2`),前文撕裂行
`vmco[vm1] vm1> nsole 0`;壳死+gdb 死同 [79] 面。

**机理**:vm1 spinner 复活([64])与 vm0.v0 共 CPU0 后,27 在
"**别人跑**"的片里到点:旧 el2_irq_guest 27 分支无条件注硬
LR0——虚 27 被送进**跑者**的 GIC 面;跑者无 27 处理面永不认领,
而 HW-LR 挂起由恒高物理线驱动→跑者被喂成认领-EOI-再挂无限
风暴(TCG 满速磨穿,泵"跑到出"楔死宿主壳)。站 16 注释早有此律
("在跑的别的 VM 会串台消费"),只封了预挂面,在途拍面漏网。

**修法**:跑者==业主照旧硬注;跑者≠业主且业主可跑→**只写业主
帧 lr 槽**(vgic_make_vtimer_lr 同源值构造,不碰跑者硬 LR),
业主下一圈进场装回、消费、虚 EOI 撤物理 active 闭环;业主泊者
不注入(站 25C 每拍扫描 cval 再发现/WOKE 注入照走)。判据:[69]
门 3 次 hb 回复、ticks 5119..5523 夹 Linux 靴流行进。

## 5. 十七雷:mrs CurrentEL 在 EL0 落 UNDEF——utest 门自雷

**症状**:[77] utest 门 `EL0 fault: esr=0x2000000 elr=0x40200a94
far=0x0` 诚实收尸。

**机理**:cmd_utest 首版以 `mrs CurrentEL` 做 EL0 自证——**ARMv8
ID 类寄存器 EL1+ 方可读**,EL0 访问即 UNDEF(EC 0),a57 诚实行刑
(WIP 期从未上判据,命令级雷)。

**修法**:EL0 存在性证词改由 syscall 面本身出——SVC 仅 EL0 有
正当通道(EL1 的 0x15 走 rt_sync 未知 EC 停车面),getpid>0/
NI -ENOSYS/sysinfo 回填三判据在此面即自证。门接线同步修:[76]
收尾 owner=vm2 busybox,宿主命令字节会被喂进 Linux,切台必走
热键 \x1e0(与 [78] 同律)。

## 6. 工艺沉淀(跨站复用)

1. **差分采样定位泄漏宿主**:剥交互三采样(纯 idle+tick 世界
   sp 纹丝不动)证明棘轮随阻塞-唤醒周期走,把 bisection 面从
   "整个调度器"缩到"壳切换路径"。
2. **rt_ticks 看门狗捕活火**:tick 死后协作世界照常运转,可见
   冻结比死点晚 ~40s——gdb 轮询 rt_ticks(PA 经宿主语境线程)
   冻结即拉全环 trail,死亡现场必在环内。
3. **trail 是终局判据**:ev15→ev8 两行胜过一切纸面推演;全环
   16384 条拉满(vmtrail 16384)+ 按 cpu 过滤,别被 CPU3 的
   400 条/秒刷屏骗了窗。
4. **帧即真相同律要数清清单**:struct 每加字段,复位清单同步
   加——"槽位复用不许带旧值"的纪律只对显式列出的字段生效,
   站 16 的九字段漏了七站才炸([79] 的 4/4 假"暖靴"脸)。
5. **修法自雷双证**:+16 左移(elr=0 IABT)与哨兵碰撞(ev8 后
   无 ev16)都被"修后首轮猎捕"当场抓回——**新修必配最小
   复现器的第一轮反证**。
6. **QEMU 法医面**:mrs 可读的寄存器写进 vmrx(cntv/cnthp/hcr/
   vmcr/gicr0 pend/act/en/p26/p27)≈ 自带 20 号探针;gdb 系统寄存
   不可读(cnTHP 等不在 stub XML),监控 passthrough(xp 物理读)
   是替补;`wait=on` 的 chardev 在首个串口连接前 gdbstub 不绑,
   仪器脚本先连串口再挂 gdb。

## 7. 判据账

- 81 门(新增 [77] utest;[77-80] 顺移 [78-81]),连续 2 轮全绿
  (RESULT PASS ×2),[24]/[30]/[69]/[77]/[79]/[81] 各专项全过。
- 猎捕器分层回归:看门狗 30/30(修前 iter2 即死)、重混合
  40/40(修前 iter1-25 随机冻)、冻结复现 6/6(修前首轮即冻)。
- 三处同步:verify(81 门+utest)/宿主壳走查表(81 步)/
  ARCHITECTURE(utest+81 步)同批落地;guest help 补 utest。

## 8. 下一步(推荐序)

站 29(process+AS)接管:per-process 页表/ASID/缺页;rt_el0_fault
诚实面退役为缺页入口(注释里的 see station-29 已预埋)。站 28 的
EL0 机器(两区 stage-1/蹦床/syscall ABI/壳)为其地基。
