# 站三十五 devlog:线程(clone+TLS)——musl 契约兑现日与弹栈重入窗定谳

日期:2026-11-01 · 作者:Julian Chu + Claude Code
状态:**已收口**——94 门(新增 [89] pthread,旧 [89-93] 顺移
[90-94])连续 2 轮全绿(r2/r3 相邻两绿;序列中另有一次无输出
黑轮,面不明,管道滤掉了全部输出含站 25 门红取证,诚实记档)

## 0. 本站做了什么

给 eshine-rtos 装上**真线程**:musl 1.2.5 的 pthread_create→
__clone(220) 全链跑通——4 线程争一把 PTHREAD_MUTEX_INITIALIZER,
join 收齐,counter=10 分毫不差。这是站 33 "真号面纪律"的兑现日:
clone/gettid 用真 Linux arm64 号,futex 契约对齐 musl 实测语义
(NULL=无限等/timespec 指针=真超时/PRIVATE 旗掩/SETTLS→TPIDR_EL0
每切换存还/CHILD_CLEARTID=线程退场内核写 *ctid=0+futex 唤醒)。
站 32 的"扑克内核代演第二执行流"正式退役——mutex 争用路径上
跑的是真线程对。

## 1. 机制落点

- **rt_thr.c**(新):sys_clone_impl——校验线程族旗集(NEEDS=
  VM|THREAD,ALLOW 全集外拒)/帧整拷(fork 同款)+x0=0(musl
  __clone 子支契约:回 svc 后继指令零号返)/SP=musl 压好
  func/arg 的栈(父先压,子不重压)/同 proc 共享(同 ttbr、同
  fd 表、同信号 handler 面,零拷贝)/SETTLS→tpidr_el0 初值/
  CHILD_CLEARTID→ctid 记档/tid=发号器(0x100 起,免与 pid 混)。
- **退场二分**:非 leader 线程退=纯任务死(rt_thr_exit:CLEARTID
  兑现=*ctid 写 0+futex wake,然后 rt_task_exit);leader(或
  exit_group)退=先清扫线程群(rt_thr_group_kill:READY 摘 rdy/
  pend 队自摘/清 wait_mask/置 DEAD——孤儿 futex 持有者不悬空)
  再走站 29 进程退场链。
- **TPIDR_EL0 随任务走**:vec.S 簿记宏补存还对(@736,站 32
  SP_EL0 同款"272B 帧外清点"纪律——帧手术完整性清单再+1)。
- **池与窗扩容**:RT_POOL_MAX 8→16,RT_AS_VMA_MAX 6→32
  (musl 每线程栈+TLS mmap 的地图余量)。
- **slot 扫描翻案**:旧扫 `entry==NULL||RT_DEAD` 会偷走"已建
  未跑的帧改写子"(fork/clone 子 entry 恒 NULL,READY 态在旧扫
  眼里=空槽)——新增 **RT_FREE(5)** 态+开机池初始化,扫描改
  `state==RT_FREE||RT_DEAD`;ps/stackcheck 过滤同步。
- **自拆的旧雷顺手拆除**:rt_as.c 里**陈旧本地 #define
  RT_AS_VMA_MAX 6** 一直遮蔽 rtos.h 的 32——第 4 个线程
  pthread_create mmap EAGAIN 的真身。本地宏遮蔽头文件=四类
  "redefined"警告的常驻来源,拆之。

## 2. 战果定谳:帧恢复机器不可重入(本站最大战果)

冒烟一度 **50% 挂率,两脸共一根**,法医三步定谳:

- **panic 脸**:`esr=0x96000006`(EL0 数据中止)+`elr=0x4000b088`
  +`far=0x2a0`。0x4000b088=弹栈尾 `ldr x5,[x0,#672]`(新任务
  usp 装载);0x2a0=[x0+672] 撞 x0=NULL。
- **无声楔脸**:gdb 帧尸——child 标 RT_RUN 却永不执行、CPU 驻
  客 idle park、tick 30 秒走 18 拍(爬行=泵兜底在救命)、pool
  线程槽全 FREE(线程从未创建成功)、帧残骸 0x40004728 等=弹
  栈中途位。
- **根因**:站 28 SVC 语境开中断(daifclr 契约)下,**帧伪造+
  簿记+.Lpop 弹栈全程裸奔**。tick 落窗=嵌套 IRQ 存**嵌合帧**
  (.Lpop 顶 `msr spsr_el1,x6` 已装新任务 EL0 态,elr=尾巴 PC):
  不切换路径 eret 把执行流**以 EL0 身份送回弹栈尾代码**(x0=刚
  弹出的新任务 x0,clone 子=0)→`ldr [x0,#672]`@EL0→far=0x2a0
  三数全咬合;切换路径帧半消费→弹垃圾→eret 野址→RUN 标记永失。
- **为什么 7 站无雷**:此窗要爆,需 tick 恰落窗+**同优先级 RR
  竞争者**让"切走 mid-tail 任务"真发生——pt 的 4 线程同优先级
  是首个命中场景。hello/旧 demo 的嵌套 tick 全走"不切换"路径
  且窗内 spsr 尚为 EL1 入口态,返 EL1 续跑无害。**潜伏窗=
  站 28 起就存在,竞争者到位即爆**。
- **修**:①rt_switch_to 在 push 之后、psr 伪造之前
  `msr daifset,#0xf`(push 段可被抢占无害:帧完整+psr=EL1h 非
  嵌合,续跑即续推);②RT_EL0_RET 宏头同掩(SVC 出栈尾裸奔同
  病同修)。IRQ 向量尾本就被硬件入口掩码,无需动。**站 28 契约
  不破**:泊位 pstate 由 C 侧显式写帧,eret 携用户 psr 自动回
  开中断态;.Lcoop_ret 续跑 psr 携掩前捕获的 DAIF 位。
- 修后:pt 法医 4/4,全冒烟 5/5(背靠背 3.4s 节奏=此前杀它的
  节奏),94 门两连绿。

## 3. 判据翻案战(仪器先于判据,站 25 律三演)

- **污染窗假案**:追"tick 冻结"半轮,vmrx 取证 27 pend+act
  双卡/LR0 滞留——最后发现一个 **60 分钟遗留 QEMU 烧着 4 核**
  (前会话取证残留),我所有测量都在污染窗里跑的。杀掉后通
  过率单调回升。**门禁一折先查宿主负载,站 25 律本次是自我
  违反后的自我执行。**
- **仪器自身不合格**:判据脚本无视 read 锚的 PASS/FAIL、裸锚
  "rtos> "会假吞 vmconsole 冲刷半行(verify 用 `\r\nrtos> `
  配对锚+补 `\n` 唤醒探针)、stage 匹配不用 start 位可吞旧锚
  ——三种松动搅成一锅,先把"站 34 基线也 4/5 挂"的假雷挖出
  来,才把真雷(pt 两脸)从噪声里剥出来。**已知好仪器=verify
  本身;自制探针必须先在已知好世界验证再上差分。**
- **RX 无声丢拍族**(站 25/33 残余):判据脚本快节奏下偶发
  (~1/3 附件频率,连安静窗也在),verify 的补 \n+配对锚形状
  未踩中;94 门三绿未拦路,按站 33 记档"若常态拦路再议"。

## 4. 自雷两枚(冒烟即抓)

- **sigdemo 契约迁移漏网**:futex 新契约只迁了 futexdemo,
  sigdemo 子支还传老契约 x3=400(ticks 字面)——新契约下 400
  被当 timespec 指针解,access_ok 拒→-EFAULT,-EINTR 脸必破。
  修=专用 10s timespec 垫背(fork 前置值,子承页拷)。教训=
  **契约迁移要 grep 全部调用面,不是改一个 demo 就收工**。
- **vec.S 落刀次序**:首版把 `mrs x1,daif` 挪到 RT_PUSH_GPRS
  之前,会把捕获值写进帧 x1 槽=唤醒续跑寄存器损坏;自纠为
  push 后取(帧槽存调用者原值,捕获值只进 psr)。

## 5. 门禁与收口

- [89] pt 门锚:"pthread: 4 threads joined, counter=10 (want
  10)"+"pthread: self="+"run: exited code=0";[87] sigdemo 门
  四锚本就在(handler/EINTR/reaped),EFAULT 雷属"门必抓"范围。
- 三处同步:verify 94 步+shell help 走查表+ARCHITECTURE 验证
  节(站 35 门段新入)。
- devlog+内容包 5 文件同批;commit→镜像机→记忆;**直开站 36
  (shm+xspace),不停**。
