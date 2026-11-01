# 站 42 spec:syscall 面补全+观测面(参考实现终审计的补齐弧)

日期:2026-11-01 · 作者:Julian Chu + Claude Code
上游:docs/specs/2026-11-01-station41-post-reference-audit.md(四路普查账册)
参考实现机制白描(clean-room 零拷贝):调度器管理面/信号补全/posix
timer+itimer/robust futex/进程组会话/close_on_exec/调用链回溯/
poll——我们站 27 差距清单漏掉的 syscall 面,按真号面纪律补齐。

## 1. 面(按账册 §1 真遗漏裁剪)

### A. 调度器管理 syscall 组(整组)
- sched_getparam/sched_setparam/sched_getscheduler/sched_setscheduler
  (SCHED_FIFO/SCHED_OTHER 两策略 v0;setscheduler 同设策略+优先级)
- sched_get_priority_max/min、sched_rr_get_interval
- sched_setaffinity/getaffinity(双核 cpu 掩码)、sched_getcpu
- sched_yield(真号;hvc_yield=宿主暂停面教训不重犯:让位=调度器)
- nice(PRD_PROCESS 面换算)
- 约束:改在跑任务优先级=站 18 PI 基准位纪律(boost 还原面自查)

### B. 信号补全
- rt_sigpending(136)/rt_sigsuspend(133)/rt_sigaltstack(132)
- rt_sigtimedwait(137):取挂起信号+siginfo 面(轮询面站 38 已有骨架)
- ERESTARTSYS 重启语义(512-516 内部码,出口统一分诊):无 handler
  信号打断阻塞调用→静默重启;有 handler→改写帧回跳(EINTR 面
  站 32 契约迁移,futexdemo 判据同步改造)

### C. posix timer+itimer
- timer_create/timer_delete/timer_settime/timer_gettime/getoverrun
  (sigevent 通知 v0=SIGEV_THREAD_ID 单任务面)
- getitimer/setitimer(ITIMER_REAL 一档 v0)
- 骑站 20 oneshot/periodic 内核 timer 机器(零新到期基建)

### D. robust futex list
- set_robust_list(99)/get_robust_list(100):musl pthread 启动即注册
- 持锁者死亡=exit 路径遍历 robust 链自动 wake+标记(PI 面后弧)

### F. 进程组/会话
- setsid/getsid/getpgid/setpgid(busybox 作业控制面;rt_proc 增
  pgid/sid 两域,leader 线程组名同步律同参考实现白描)

### G. close_on_exec
- fcntl F_SETFD/F_GETFD 的 FD_CLOEXEC 位;exec 面逐 fd 摘除
- (dup3 已有;fd 表域复用站 31 机器)

### H. 观测面(旗舰)
- calltrace:FP 链内核态回溯,panic 自动触发(EL1 abort 静默驻车
  案的机器背书——站 41 法医弧欠的债)
- ps 扩展:阻塞对象归属列(任务卡在哪类对象+属主,死锁排查);
  procfs 任务行加 state/wait 源
- EL1 abort 终态面补一行打印(fault 面 EL0 先例;零可观测性
  =站 41 三小时法医根因)

### I. poll/select
- poll(fd 事件面:POLLIN/POLLOUT/错误三族;resmgr 服务器侧
  事件注册 v0=轮询管道+fifo 可读游标;站 39 服务器两用)

号面纪律:全部真 Linux arm64 号,从 musl 头(3rd/musl-1.2.5/
arch/generic/bits/syscall.h.in)钉死,零臆测;demo 判据针线从
实输出抄(站 41 教训)。

## 2. 边界(v0 记档)

sigevent 仅 SIGEV_THREAD_ID;affinity 仅 2 核掩码;restart 仅
阻塞族(futex/sem/queue);robust 无 PI;pgid 无终端作业控制
(无 tty 会话面);poll 不含 epoll;calltrace 仅 FP 链(-O2
帧指针保真核验)。

## 3. 判据(116→123;实现期定谳裁剪)

- [112] scheddemo:setscheduler+getparam 往返+yield 活证+rr 间隔
- [113] unamedemo:utsname 六字段(首航:musl busybox uname 顾客)
- [114] pgiddemo:setsid 自会话+fork 承继组
- [115] sigdemo2:屏蔽中自挂 pending 位+altstack 注册回读+
  sigsuspend 醒(handler 自身上下文+显式 sigreturn+掩码还于
  sigreturn=POSIX 掩码序)
- [116] timerdemo:posix timer SIGEV_THREAD_ID 周期≥2 发+delete+
  itimer 单发
- [117] robustdemo:clone 线程持字者死,exit 链遍历自动醒等者
- [118] cloexecdemo:F_SETFD/F_GETFD 往返+父 fd 不受子扰
- 旧 [112-116](热键/迟退/溢出/处决/yocto)顺移 [119-123]
- **裁剪记档(实现期)**:E(ERESTARTSYS 重启语义)改站 32 契约
  面大(futexdemo 判据连锁),H(calltrace/ps 观测)与 I(poll)
  各自成站更干净——三面后弧入账册,站 43+ 候选。

## 3.1 站 42 战雷账(冒烟三连定谳)

- **getpid 槽号≠pid(第一受害者定律新案)**:rt_pid_of 返任务
  池槽+1,壳 28 站全绿只因槽 0 恰等于 pid 1;首个 fork 子 getpid
  现形(报 5 而真 pid 2)。修=proc 面优先,无 proc 任务退槽号。
- **sigsuspend 漏 BLOCK 态**:站 41"调用者驻车"反面教材当日
  重演(直调 rt_sched+RUN 态=WFI 永环);修=站 24 rx 正牌三件套
  (wait_mask+BLOCK+rdy_remove 同 DAIF 窗)+掩码还原移至
  sigreturn(sig_retmask/suspflag 两域,POSIX 掩码序)。
- **clone 子落父 C 帧=野址读**:恢复点在 wrapper 返回帧,新栈
  无父局部([sp+1304]=字面串指针野值,far=ASCII 定谳);修=必
  入自有帧入口函数(musl __clone 同律;fork 子因整栈拷贝无此
  约束——两种 child 两种纪律)。
- **站 28 tp 守卫谓词扩展**:posix timer 池=合法第二驻地
  (守卫原只认任务池;真野值照抓不变)。
- handler restorer=0 面=末尾显式 rt_sigreturn(站 32 契约照抄)。

## 4. DoD

spec→实现→门禁×2(两连 RESULT PASS;**过轮后垫 10 分钟冷却律**)→
三处同步→devlog→内容包 5→commit→镜像机→记忆。
