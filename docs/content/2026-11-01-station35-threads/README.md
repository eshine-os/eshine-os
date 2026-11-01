# 站35 口径:线程(clone+TLS)——musl 契约兑现日

**eshine-os** · 站 35(threads)· 2026-11-01

## 本站做了什么

给 eshine-rtos 装上**真线程**:musl 1.2.5 的 pthread_create
全链跑通——4 线程争一把 PTHREAD_MUTEX_INITIALIZER,join 收齐,
counter=10 分毫不差。内核侧只做 musl 实测契约的兑现面:

- **clone(220)**:帧整拷(fork 同款)+x0=0(musl __clone 子支
  契约:回到 svc 后继指令拿零号)+SP=musl 压好 func/arg 的栈;
  同进程共享地址空间/fd 表/信号 handler 面(零拷贝)。
- **SETTLS**:TPIDR_EL0 随任务切换存还(vec.S 簿记补一对——
  站 32 "SP_EL0 不在 272B 帧内"同款清点纪律)。
- **CHILD_CLEARTID**:线程退场由内核写 *ctid=0+futex 唤醒
  (musl join 的用户态 futex 靠它收口);PARENT_SETTID/gettid
  同步落位;leader 退场先清扫线程群再走既有进程退场链。
- **futex 契约对齐**:x3=NULL=无限等/timespec 指针=真超时/
  PRIVATE 旗掩——站 32 的"扑克内核代演第二执行流"退役,
  mutex 争用路径上跑的是真线程对。

## 本站最大战果:弹栈重入窗定谳

冒烟一度 50% 挂率,**两脸共一根**:panic 脸(elr=弹栈尾
`ldr [x0,#672]`、far=0x2a0=672 本尊、esr=EL0 味)与无声楔
(任务标 RUN 永不执行、tick 靠泵兜底爬行、帧尸停在弹栈中途)。
根因=**站 28 开中断契约下,帧伪造+簿记+弹栈全程裸奔**:tick
落窗即嵌套中断存嵌合帧(spsr 已装新任务 EL0 态+elr=尾巴 PC),
eret 把执行流以 EL0 身份送回弹栈尾代码。**7 站无雷只因没有
同优先级竞争者让"切走 mid-tail 任务"真发生——pt 的 4 线程是
首个命中场景**。修=两枚掩码(rt_switch_to 与 RT_EL0_RET
宏头),帧恢复机器不可重入,eret 自动回开中断态,站 28 契约
无损。

## 门禁

94 门 ×2 双绿(新 [89] pthread;旧 [89-93] 顺移 [90-94])。

## 诚实边界(v0)

per-proc 位图 pending(线程定向信号=站 38);futex PI/robust
不做;线程单核亲和;非线程族 clone 拒;tid=发号器非 pid 语义。

## 关键词

ARM64 hypervisor · clean-room · RTOS · musl pthread ·
clone(220) · TPIDR_EL0 · CHILD_CLEARTID · futex ·
协作切换 · 帧契约 · 可重入
