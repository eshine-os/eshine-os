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

## 5. 下一役(交接)

1. **RT_EL0_RET 绊线(68C-20 同律补位)**:在
   `ldp x0,x1,[sp],#16` 后补 elr 窗检(镜像 .Lpop 的
   movz/cmp/b.lo/b.hs→.Lbk_viol_elr 分址桩)——无检路径封口,
   毒帧消费的出生瞬间(受害者/路径/sp 三元)当场钉住。
2. 复跑 `--stop-after=38` 保活解剖,取出生点现场(x10 的毒从
   哪条 ldp 来→回溯写手)。
3. Face B(字符级互插+cpu 0x1)同族异脸:毒落在跨核上下文。
   打印机互插=panic 裸 puts_uart 与壳并发的原子性破绽,单独
   低优修复(panic 语境打印机锁)。
4. 验收:门 [37]/[38] 连过×2 → 全量 183 两连(站 DoD 纪律)。

## 6. 教训

1. **验尸痕迹与生前伤分家**:panic/异常入口的压栈会改写现场——
   栈上"毒"必须先问"是压进来的还是原本就在"(本次 elr 槽的毒
   =验尸压栈的 x10)。
2. **QEMU -d int 是廉价全息黑匣子**:EC 路由谜题(EI 从低 EL?)
   一行日志翻案;wfi"UNDEF 楔死"假警报 94k 行后才识别为泵停泊。
   定谳前先看 `-d int`,再谈机制。
3. **保活现场>自动取证**:ESHINE_KEEP_ON_FAIL 把"取证失败=现场
   灭失"变成"取证失败=现场在手,重试即得"。stub 偶发超时不再
   丢证据。
