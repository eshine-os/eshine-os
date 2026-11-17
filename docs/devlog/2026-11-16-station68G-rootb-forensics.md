<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 站 68G 续(26.11-3w 周一开站):[37] 污染族 root-b 法医战报

> 本篇是 26.11-3w 开站(分支 dev/26.11-3w)的第一役:[37]/[38] 双核
> demo 深层污染族(root-b)的证据链固化。修复未落——出生点(毒值
> 首次进入寄存器的写点)待下一轮带 RT_EL0_RET 绊线捕获。交接即战报。

## 0. 复现与判据

- `--stop-after=38`,五轮:三轮 Face A(确定性 3/3),一轮 Face B,
> 一轮撞 boot 停摆族(站 25 残面,与 [37] 无涉)。
- **Face A**:`ipidemo` 窗内
  `rtos panic: unhandled sync exception / task: ipimain (cpu 0x0) /
  esr=0x86000004 elr=0xa5a5a5a5a5a5a5a5 far=0xa5a5a5a5a5a5a5a5`。
- **Face B**(第 4 轮):`ipi: rx1=2 / ipi: rx0=0 roundtrips` +
  panic 打在 **cpu 0x1** + 控制台字符级互插(panic 行与壳提示符
  逐字符交错)。
- 每轮失败后串口级取证均断流(chardev 停摆,站 25 残面)——
  **gdb stub 是失败现场唯一可靠取证通道**。

## 1. 仪器化(本站入库)

- `verify_el2.py` 增 `_forensic_gdb_r37()`(失败路径专用,绿门零
  影响):pool 全槽六元组+每具名任务帧顶四元+栈内非 0xA5 毒区
  跨度表+rt_cur_arr/ipi 计数/echo/need_switch/rdy_map。
- `ESHINE_KEEP_ON_FAIL=1`:门红后 QEMU 不收(PID/gdb 端口/sock
  打印在案),留给手工活体解剖;用毕人工 kill。本轮 3 次有效
  使用(串口死+stub 偶发超时下,保活窗内重试 attach 必成)。
- 手工解剖脚本形态(池+队列链+帧+原始 hex)不入仓,现场重铸。

## 2. 证据链(活体解剖 5 号轮,Face A)

**池(门红时刻)**:
- slot 13 `ipimain`:st=1(RUN) cpu=0 prio=8 **身份完好,无槽复用**
  (名字/entry/cpu 全净)——"槽复用毒填"假说被否。
- slot 5 `ipi1`(sleeper):st=2(SLEEP) wake=0x981(rt_ticks=2348,
  首醒未到——**cpu1 的"黑脸"是健康空转等待**,见 §3)。
- rdy_map 双核 0(全阻塞),无双挂。
- cur0=pool[13](ipimain),cur1=pool[5](ipi1)。

**栈解剖(slot 13)**:
- t->sp=初始帧位下方 320B(=协作帧深度,t->sp 落 ipi_main 执行
  深度附近——任务跑过、阻塞过,协作帧合法在位)。
- 协作帧 [t->sp,+272):[psr]=0x5(EL1h 合法)、[x0]=pool[1] 指针
  (合法保存 GPR)——但 **[elr]=0xa5a5 毒**。
- **死后痕迹分离**:panic 处理器(guest_sync 入口 RT_PUSH_GPRS)
  的压栈恰好覆盖协作帧 elr 槽——**推入值=当时寄存器 x10=
  0xa5a5a5a5a5a5a5a5**。栈上"毒帧"半数是验尸假象;**真毒在
  寄存器里**(x10=毒,且 ELR_EL1/PSTATE 均毒)。

## 3. QEMU -d int 中断流(决定性)

- **0x40000798 = rt_sched idle 循环的 wfi**(CPU3 大循环 94k 次
  =TWI 陷阱,QEMU 标作 UNDEF,是泵停泊机制,健康);tbnz/UART
  MMIO 陷阱序列(rt_puts_u64 的发送轮询)全部正常。
- **致命窗(CPU0)**:`IRQ→壳向量→eret 回 0x4000079c(idle)→
  eret 到 .Lcoop_ret×2(两次协作帧恢复,均过窗检)→ Prefetch
  Abort:ESR 0x21/0x86000004,FAR=ELR=0xa5a5a5a5a5a5a5a5,
  from EL1 to EL1`。
- **EC 定谳:0x21=当前 EL 取指中止**(QEMU 直证"from EL1 to
  EL1";旧表记"来自低 EL"是笔误——E.I.窗检当年按当前 EL 设计
  无恙)。
- PSTATE(异常前)=0xa5a5→M=0x5=**EL1h**——致命转移落在 EL1h@
  0xa5a5。**全日志无任何 "Exception return ... PC 0xa5a5" 行**
  ——毒 PC 的进入未经 QEMU 记账的 eret 转移(候选:无检 msr 对
  的 eret,或带毒寄存器的 ret/br 分支)。

## 4. 机制链定谳(投递环)

1. `.Lpop` 的 68C-20 elr 窗检([0x40000000,0x80000000))**本应
   拦下 0xa5a5**——没拦=致命投递不走 .Lpop 受检路径。
2. 全 RTOS 仅 5 个 eret 位点;其中 **RT_EL0_RET(0x40019850)是
   唯一无检的 elr/psr 装载+eret**(ldp 直装 msr 对)——首要
   嫌疑投递路径(.Lpop/.Lel0/.Lel1 均在窗检后;pop_same 族
   eret 用硬件现值)。
3. 毒源(出生点)未捕获:0xa5a5a5a5a5a5a5a5 进入寄存器的首写
   ——候选族:带毒 sp 的半推帧消费/帧手术错槽写/陈旧协作帧
   错位消费。**下一轮的绊线就架在出生点**(见 §5)。

## 5. 第二轮(绊线武装后的判决)与终审定谳

**RT_EL0_RET 绊线已上**(ldp 与 msr 对之间的 elr 窗检,镜像
68C-20 形,.Lbk_viol_elr_ret 桩;5 处 movz 检查=旧两处+新三处;
36 门+[37] 窗全程零误报)。第 7 轮结果:**绊线静默,毒 panic
照旧**——投递不经 RT_EL0_RET。结合中断流"无 eret 转移到
0xa5a5 的记录行",投递=**毒寄存器分支**(ret/blr)。

**帧解剖终审(slot 13 全帧字图)**:
```
+000: elr=0xa5a5(毒) | psr=0x5(EL1h,合法)
+016..+112: x0-x10,x12=调度器活局部量(pool[1]/rdy_lk/
      rdy_head/0x1f/…)——某次调度器语境的部分压栈残留
+112..+256: x11,x13-x30 全毒(从未写入)
```
⟹ t->sp 指向的是**半推残帧**:低半=压了 ~7 个 stp 后被击断
的调度器语境,高半=毒(未推)。".Lpop 弹栈无 GPR 检查"——
毒 x30(帧+264)弹入寄存器,eret 落合法 elr,首个 ret 跳
0xa5a5 → 取指中止 panic。**68C-20 只检 elr 槽,半帧的毒在
GPR 区,窗检形同虚设。**

## 6. 机制链(root-b 终形)与修复设计

**链**:任务经 rt_switch_to 阻塞→RT_PUSH_GPRS 压栈**中途被
IRQ 击中**(站 35 记档"push 段可被抢占无害"——该无害前提=
IRQ 同任务续跑续推)→IRQ 出口 `rt_irq_dispatch` 返回
**next==rt_cur(自切,rt_main.c:200 `return next` 可返回同
任务——68C 记档"self-switch next==old"正身)→走完整切换簿记
→`str x3,[x2]` 把中途压栈位存进 t->sp(半帧,elr 槽=毒)
→后续恢复消费半帧:或 elr 毒(窗检可拦)或 GPR 毒(x30,
**无检**——本例)→毒 ret→当前 EL 取指中止→panic。

**修复设计(下役)**:
1. `rt_irq_dispatch` 自切守卫:`next==rt_cur → return NULL`
   (走 .Lpop_same 续跑=续推被击断的 push,站 35 的"无害"
   前提由此成立)。一行,先落。
2. push 窗口收口:rt_switch_to 的 DAIF 掩码从"push 之后"前
   移(注意 psr 伪造的 DAIF 捕获语义——x1 捕获点设计要重看);
   或半帧检测(bookkeep 存 sp 前验 [x2 的 ksp 距离与帧顶对齐)。
3. Face B(字符级互插+cpu 0x1)同族异脸:毒落跨核上下文;打印
   机互插=panic 裸 puts_uart 并发原子性破绽,低优单独修。
4. 验收:门 [37]/[38] 连过×2 → 全量 183 两连(站 DoD 纪律)。

## 7. 第三轮-第四轮:断案槽探针与剩余一环

**自切守卫已落(rt_main.c)但仍红**:第 8 轮同签名(绊线
武装下);器械轮次记录:

- **IRQ 出口轨迹环**(rt_irq_trace,最近 16 次 dispatch):
  失败窗时间线=ipi1/ipimain 睡眠、idle 空转、heartbeat 定时
  唤醒;tripwire 时 true next=heartbeat st=RUN——切换本身合法。
- **出生绊线真身**(第 10-12 轮):`rt_switch_to` 入口 x30=
  0xa5a5,rt_cur=ipimain 且 st=READY(唤醒令牌语义的正常瞬态
  ——"rt_cur 是 READY"不是 bug,别误判)。
- **断案槽探针**(第 12 轮,判定前置到压栈前):epilogue 读的
  x30 保存槽 [caller_sp-24] **与 x29 槽 [caller_sp-32] 双双为
  0xa5a5 毒填值**——序言 `stp` 写过这两槽(写即净),epilogue
  读回却是毒 ⇒ **"写后被毒填/毒区覆盖"成立**(非 epilogue
  读错址)。
- **16B 错位线索**:o_sp+272(上次恢复 sp=top-80)与本次
  caller_sp(top-64)差 16——恢复链与调用链的 sp 关系在
  272B 帧基记账上存在 -16 偏移带,正叠在毒填区边界。

**剩余问题(挂账,下一刃)**:谁在 rt_sched 序言之后把
[caller_sp-32, caller_sp-16) 的 16B 抹成毒填值——候选:
①IRQ 帧/.Lpop 恢复把 sp 记偏 16B 后深区的毒填窗口被读;
②rt_sleep_ticks 的 32B 帧与 rt_sched 保存槽的相邻边界
(top-96/top-88 与初始帧 x20/x21 槽同址)在某个恢复路径上
被毒填值回灌。

**第四轮(恢复环)定谳(第 14 轮全息)**:`.Lregs` 入口恢复环
(64 环,scratch 零成本——入口 GPR 即将被弹栈覆写)实锤:

```
RES sp 0x40170230 elr 0x4000079c psr 0x80000005 tp 0  (×多次)
chk_sp 0x40170340 ⇒ 序言帧基 sp_p=0x40170320,保存槽 [20]/[28]
恢复 eret sp=GPR基+256=0x40170330 = sp_p+16 !!
```

**结论**:rt_sched 的 wfi 循环实际运行在**比本序言帧高 16B** 的位置
——序言把 x29/x30 存于 [20]/[28],而循环/尾声按 sp=0x40170330
执行,epilogue 的 `ldp x29,x30,[sp],#32` 读进 [30]/[38]=初始帧
x18/x19 槽(**毒填默认值**,rt_frame_init 只写 elr/psr/x0/x1)。
数学上这只有一种解释:**"序言"与"循环/尾声"分属两次不同的
rt_sched 调用**——任务被从落后的 IRQ 帧(u_sp=0x40170220,
打断于 rdy_remove 深度 0x40170330)复活进 routine 的中段,
而本调用的序言保存在另一深度。**=68C"陈旧帧复活(mid-function
resurrection)"族的纯粹形**:帧基记账与函数帧基相差 16B。

**下刃(修法候选)**:
① .Lpop 恢复后校验:eret sp 与 rt_cur 的"预期深度"不符即
   直落 violation(exected sp 记账=每任务维护 lo/hi 深度带);
② rt_sched 不前保存在"调用者栈"而改存每核静态槽(消除
   帧基依赖,循环体与 epilogue 同源)——微小改,消灭整族;
③ 定位源头:为什么恢复进 0x4000079c 的帧基是 rdy_remove 深度
   (追踪该 IRQ 的 guest_irq 帧与 rdy_remove 的 sp 关系)。

**终章(第五道修真,门级闭案)**:全息链回读闭环——毒不是"覆写未写槽"
而是**一次真实的 create 毒填**:死槽现役缺陷。`rt_task_exit()` 全灭
兜底在**刚 DEAD 的任务栈上**转入 `rt_idle_loop()` 空转(rt_cur 保持
指向死者),而槽复用判=FREE/DEAD 立刻放行——新任务的 create 把
task_stacks[slot][272,4368) **整片毒填**,正覆写该栈上 idle 语境
的活帧(rt_sched 序言保存槽 → 0xA5A5 → wake 切换时 epilogue 弹毒
→ ret 跳 0xa5a5)。68C 记档原句"rt_cur=已退池任务 st 0"即此景
(绊线 old 名=同址新对象,旧死者已复用改名)。

**修(rt_task.c task_alloc_kernel 死槽现役闸)**:DEAD 槽若仍是
某核 rt_cur(栈现役)则本轮跳过不复用;rt_cur 改指他任务(成功
让位)后自然放行。

**验证**:
- 门 [37] 首绿 ×2 连过:`ipidemo: 21 SGI roundtrips + 3 cross-core
  wakes, window quiet`;门 [38] 双精确 ×2;
- `--stop-after=38` 两连 RESULT PARTIAL-PASS(快环判据);
- 站收口=全量 183 门两连(安静窗,待跑)。

**教训**:毒填是"新生命"的仪式,却是"旧生命残帧"的毒药——**复用
判据必须与栈现役判据同源**;DEAD≠可复用,当 DEAD 仍是当前执行
语境时。


## 8. 修复(已落:自切守卫,门未绿——余环见 §7)

**病灶一行**:`rt_irq_dispatch()`(IRQ 出口)缺 `rt_sched()` 的
同位自切守卫——协作路径站 19 早已设(摘己选己=回流令牌兑付,
只回队不碰状态),IRQ 出口旧漏:自切照返 next,于是
①切换簿记把本帧 sp 存进 t->sp(RUN 中任务被"存档");
②令牌二次入队(运行中任务回流就绪队列)→ 双上下文/半推帧
错位消费 → 毒 GPR 弹栈(x30)→ elr=0xa5a5.. 取指中止 panic
(站 68G 活体解剖 4/4 定谳的送达链)。

**修**(rt_main.c,镜像协作路径语义,零新律):
```c
if (next == rt_cur) {
    rt_ready_requeue(next);   /* 令牌兑付:摘出的还回去 */
    return NULL;              /* 调用侧 .Lpop_same 续弹本轮
                               * IRQ 帧(硬件 ELR/SPSR 原值,
                               * eret 回被打断处) */
}
```

**验证(如实)**:自切守卫落第 8/9/10/12 轮仍红(同为末环毒 x30 绊线命中)——守卫是正确卫生修(IRQ 出口与协作路径对齐+令牌单一),但**不是**本族的封口手术;封口待 §7 余环落定(谁把 rt_sched 保存槽抹成毒填值),届时门 [37]/[38]连过×2 再收账。


## 9. 教训

1. **验尸痕迹与生前伤分家**:panic/异常入口的压栈会改写现场——
   栈上"毒"必须先问"是压进来的还是原本就在"(本次 elr 槽的毒
   =验尸压栈的 x10)。
2. **QEMU -d int 是廉价全息黑匣子**:EC 路由谜题(EI 从低 EL?)
   一行日志翻案;wfi"UNDEF 楔死"假警报 94k 行后才识别为泵停泊。
   定谳前先看 `-d int`,再谈机制。
3. **保活现场>自动取证**:ESHINE_KEEP_ON_FAIL 把"取证失败=现场
   灭失"变成"取证失败=现场在手,重试即得"。stub 偶发超时不再
   丢证据。
4. **检查的覆盖面=检查的字段**:68C-20 检 elr 槽,而半帧的毒
   驻 GPR 区(x30)——"帧完整性"需要的是结构级谓词(半推识别),
   不是单字段窗检。


## 10. 续站首触:[39..183] 段首日接触记录(26.11-17)

[37]/[38] 闭案后全量首次推进到 **121/183**(历史性首次跨过双核门)。
途中两次"确定性挂"经活体解剖判为**已知 console 丢行族放大**,非新内核缺陷:

- **[19] boot 停摆 ×4**(同日):四核上线后静默,判据锚
  `\r\nrtos> ` 超时;短轮 [19] 快走 3/3 绿——纯时间窗。
- **[81] exedemo ×2**:池账本=健康形(shell BLOCK wmask=0x4
  RX 等待位 at-prompt;全服务器 parked;tick 活);缺失行=
  `rtos> ` 提示符单行;复连串口击键不引 vIRQ(注入无应答)——
  与站 25/33 chardev 丢字节签名同构。
- **[122] apsdemo ×2**:360s 无输出;相位探针(ph1..ph4)已入码,
  待安静窗取数据(最近轮次连撞 [19]/[81] 未达)。
- 环境账:同期对端容器 QEMU(非本 harness,`-nographic` 形)
  400% CPU 长烧 1.5h+;按盘查纪律记档"先负载后机制"。

**结论**:站收口(全量两连)仍待安静窗;当前阻塞=上游丢行族,
先证伪内核。若常态化再议 ev9 见证位/行级重试。
