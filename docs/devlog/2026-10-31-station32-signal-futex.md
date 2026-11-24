# 站三十二 devlog:signal+futex——musl 的两张内核脸,SP_EL0 域定谳

日期:2026-10-31 · 作者:Julian Chu + Claude Code
状态:**已收口**——90 门(新增 [85] futexdemo/[86] sigdemo)连续
2 轮全绿

## 0. 本站做了什么

站 33 musl 需要的两张内核脸:**futex**(musl 所有锁原语的底座)
与 **signal**(POSIX 运行时面)。两原语同站的架构理由=互为判据:
真实"等待→唤醒"对由**跨进程信号打断 futex 等待**完成(EINTR
契约)。号面用真 Linux arm64 原号(futex=98/kill=129/
rt_sigaction=134/rt_sigreturn=139),站 33 musl 直连。

## 1. 机器落点(rt_sig.c 新文件)

- **futex**:16 桶静态表,key={proc,va} 随等待者(桶内多 key
  共存,WALK 按 key 过滤摘链)。WAIT=入口值校验(*uaddr!=expect
  即 -EAGAIN,Linux 契约)+站 21 单窗形态阻塞+醒后四路分诊:
  timed_out→-ETIMEDOUT/fut_woken→0/sig_intr→-EINTR/值动→0;
  未定(伪醒)回环重挂。WAKE=DAIF 窗内 key 匹配摘链+置位+push
  (rt_handoff 律)——**不读用户字,纯队列操作**(demo 扑克任务
  据此代演同址空间第二执行流,真 threads=站 33+)。阻塞机器
  **第三笔复用**(站 17 信号量→站 30 到达面→站 32 futex,零新
  等待逻辑)。
- **signal**(v0 位图态):action(1..31 存 handler 用户地址)/
  kill(活进程 pending|=位;唤醒 leader:pend 型=rt_timeout_unpend
  摘链+sig_intr=1+push,wait_mask 型=mask 清+push,SLEEP 不搅
  =醒后出口投递)/投递(**syscall 出口**,参考实现 SWITCH_OUT 点的
  站内形态;dispatch 尾调):整帧 272B 存 proc->sig_saved+改写
  elr=handler、x0=sig——handler 在目标自身用户上下文执行,非
  他者代跑;handler 末尾显式 rt_sigreturn(裸 ABI:其余寄存器
  未定义;SA_RESTORER 形=站 33 对齐)。fork:handler/blocked
  继承(Linux 同律),pending 不继承。

## 2. 雷与定谳(本站战果)

- **雷一(低级,冒烟即抓)**:子进程判据写 `pid == 0`——站 29
  契约是 fork 子返 **RT_FORK_MAGIC**。子落穿父路径:第二行
  "child waiting"+魔数当 pid 喂 kill/wait4,双双永挂。自家 ABI
  记岔,两分钟修复。教训:**新 demo 落笔前先 grep 同族旧 demo
  的判据原句**。
- **雷二(本站真正的战果,冒烟+反汇编双定谳)**:修完雷一,
  handler 跑了、exit code=7 收了,但子进程三元判据打出**别家
  函数的字面**("futexdemo: timeout r=-110 dt=4"——带新鲜
  dt=4 的活测量!)。纸上推演四轮无果,反汇编+rodata 直证:
  打出的恰是 shell_line **序言预存槽**的字符串指针([sp+720]=
  wake-ok、[sp+360]=timeout-dt)。真身:**SP_EL0 驻 t->usp
  (vec.S 硬件域),不在投递手术保存的 272B 帧里**——handler
  在中断 SP 下压自家帧,sigreturn 的 eret 跳过其 epilogue,
  用户 SP 永久下移一个 handler 帧,子代码从错窗读局部,三元
  指针命中序言预存槽。修=投递对存还 usp(sig_saved_usp 一域,
  deliver 存/sigreturn 还)。**帧手术完整性清单+1:凡帧手术
  必清点 272B 之外的用户可见态(exec_setup 幸其本就直写 usp;
  凡手术必配对——存什么还什么)。**
- 预排雷:ocb 池贴上限是站 31 的账;本站 futex 桶 16/进程 4,
  key 碰撞是常态(设计点),WALK 过滤摘链即为此。

## 3. 判据面(门 88→90)

- **[85] futexdemo** 四脸:值错拒(-EAGAIN)/无等者 wake=0/
  扑克代演真实对(r=0+dt)/超时真阻塞(r=-110+dt,冒烟实测
  dt=60 分毫不差;门禁 dt 锚从宽=抗调度毛刺,ipcdemo 先例)。
- **[86] sigdemo** 全链:父 fork 子(子承 handler,同 VA=eager
  拷贝的地址语义红利)→子挂 futex→父 sleep 后 kill(2,10)→
  子 futex -EINTR+**handler 已在自身用户上下文执行过**→
  sigreturn 恢复→exit(7)→父收尸 code=7。一链证:跨进程 kill+
  位图 pending+pend 型唤醒+投递帧手术+sigreturn+继承+站 31
  死亡关流回环(fddemo 复跑=fd 面/stdio 幸存回归)。

## 4. 边界与不做(v0 诚实)

无 siginfo/sigqueue 排队(同号可丢=标准 Unix);无 sigprocmask
syscall 面(blocked 位图在);默认动作=忽略(不 terminate);无
ERESTARTSYS 重启;非 futex 的 pend 型(sem/msg 等待者)被信号
唤醒后由其 take 环吸收重挂,投递延至自然醒;SLEEP 型不打断;
robust list/PI/REQUEUE/共享 key=站 33+。扑克任务单发串行
(静态一槽)。

## 5. 站利

- 三张内核脸(fork/exec/sigreturn)共用帧手术纪律;usp 定谳
  给站 33 musl 的 sigreturn 对齐预拆了一颗雷(musl 的 restorer
  落地时同样要过 SP_EL0 域这关)。
- 阻塞机器第三笔复用:信号量的 pend 登记五字段+rt_timeout_
  unpend 摘链,在 futex 桶键化场景原样成立——原语层的红利
  继续兑现。
