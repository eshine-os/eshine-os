# 站三十二 spec:signal+futex——musl 的两张内核脸

日期:2026-10-31 · 作者:Julian Chu + Claude Code
基线:参考实现普查 spec §10(futex 16 桶/命令集 Linux 全集/robust;
signal pending+blocked+投递点=返回用户态,SWITCH_OUT 在被投递
任务自身栈造 ucontext、PC=handler、_sigreturn 返回;syscall 重启
=ERESTARTSYS 同点)。红线:机制参考、零代码拷贝、参考实现权属私藏。
前置:站 31 收口 ec312c8(88 门双绿,fd=通道三元组在役)。

## 0. 目标

站 33 musl 需要的两张内核脸:**futex**(musl 所有锁原语的底座)
与 **signal**(POSIX 运行时面)。两原语在本站互为判据:真实
"等待→唤醒"对由**跨进程信号打断 futex 等待**完成(EINTR 契约)
——signal 与 futex 同站的架构理由。

## 1. 机制映射(参考实现 → 新仓站内尺度)

| 参考实现(§10) | 站 32 新仓形态 |
|---|---|
| futex 16 桶哈希+自旋锁,key={tgid,addr,is_shared} | 16 桶静态表,key={proc,va}(无 shm=is_shared 恒私);同核单写者,DAIF 窗代自旋锁(站 21 单窗律) |
| 命令集 Linux 全集+robust list+PI | v0 两脸:FUTEX_WAIT(值校验+阻塞+超时)/FUTEX_WAKE(n);robust/PI/REQUEUE=站 33+ 后议(诚实边界) |
| 等待队列内嵌 参考RTOS blk_obj 复用 pend/post | 等待者挂 rt_wq 链,五字段 pend 登记+rt_timeout_unpend 自摘(站 17/21 机器零新等待逻辑,第二笔红利续) |
| signal pending/sigqueue 链/静态池 | v0:u32 位图 pending(每进程),无排队语义(同号不排队,标准 Unix 可丢语义) |
| blocked 掩码 | u32 位图 sig_blocked,kill/投递都过掩码;sigprocmask 面=站 33(musl 自管) |
| 投递点=SWITCH_OUT/do_ret_to_user,自栈 ucontext,PC=handler,LR=restorer | **投递点=syscall 出口**(rt_syscall_dispatch 调用后):整帧 272B 存 proc->sig_saved,改写帧 elr=handler、x0=sig;handler 末尾显式 rt_sigreturn 系统调用恢复(SA_RESTORER 形=站 33 musl 对齐) |
| syscall 重启(ERESTARTSYS) | v0 不做(EINTR 直返;重启语义=站 33+ 记档) |
| 致命信号→coredump | v0:默认动作=忽略(未装 handler 不杀),记档;terminate 动作=站 33+ |
| 每任务 pending | 每进程位图(单任务/进程的世界,fork 后子 pending 清零、blocked/handler 继承=Linux 同律) |

## 2. 结构

- **futex(rt_sig.c)**:`rt_futex_wait(proc,va,expect,ticks)`:
  先读 u32(调用者 mm 直读,access_ok 纪律):≠expect → -EAGAIN;
  否则站 21 单窗形态挂桶链(窗内终判+登记+摘 rdy),醒后按
  timed_out→-ETIMEDOUT / fut_woken→0 / sig_intr→-EINTR / 值动→0
  分诊,未定则回环重挂。`rt_futex_wake(proc,va,n)`:桶内
  key 匹配摘链+置 fut_woken+push(DAIF 窗内,站 21 handoff 律;
  FUTEX_WAKE 不读用户字——纯队列操作)。
- **signal(rt_sig.c)**:`rt_sig_action`(1..31 存 handler 地址)
  /`rt_sig_kill`(活进程查 rt_proc_lookup;pending|=位;唤醒
  leader:pend 型=rt_timeout_unpend+sig_intr=1+push;wait_mask
  型=mask 清+push;SLEEP 型不搅,醒后出口投递;RUN/READY 型
  自然等出口)/`rt_sig_deliver(frame)`(dispatch 出口查
  pending&~blocked:最低位号,清位,无 handler=忽略,在役
  (sig_active)=挂起留下轮;投递=sig_saved 整帧+改写 elr/x0)
  /`rt_sig_return(frame)`(sig_saved 回拷,x0 同值回写=分发器
  一字两用,exec_setup 同律)。
- **task 尾追加**:fut_proc/fut_va(键随等待者)/fut_woken/
  sig_intr(asm 契约断言不涉,尾加纪律);**proc 尾追加**:
  sig_hand[32](u64 地址)/sig_pending/sig_blocked/sig_active/
  sig_saved[34](272B)。
- **号面=真 Linux arm64**:futex=98/kill=129/rt_sigaction=134/
  rt_sigreturn=139(asm-generic 原号,musl 直连);errno
  EAGAIN=11/EINTR=4/ETIMEDOUT=110/ESRCH=3。v0 偏差记档:
  futex 4 参(uaddr,op,val,ticks——Linux 第 4 参是 timespec 指针)
  /sigaction 2 参(sig,handler——Linux 3 结构指针);站 33 对齐。

## 3. 判据面(门禁只增不减:88→90)

- **[85] futexdemo** 四脸:值错拒(-EAGAIN 即返)/无等者
  wake=0/**真实等待→唤醒对**(内核扑克任务代演同址空间第二
  执行流:只做队列唤醒不碰用户字,dt≈30 r=0——真 threads=站
  33+,扑克=机制级替身)/超时真阻塞(dt=60 r=-110)。
- **[86] sigdemo** 全链:父 fork 子(子承 handler,同 VA 有效=
  eager 拷贝的地址语义红利)→子挂 futex 等→父 sleep 后
  kill(pid,10)→子 futex 返 -EINTR 且 **handler 已在自身用户
  上下文执行过**("sig: handler ran")→sigreturn 恢复→子
  exit(7)→父 wait4 收 code=7。一链证:跨进程 kill+位图 pending+
  pend 型唤醒+投递帧手术+sigreturn+继承+站 31 死亡关流回环。

## 4. 边界与不做(v0 诚实)

无 siginfo/sigqueue 排队(同号可丢,标准 Unix 语义);无
sigprocmask 面(blocked 位图在,syscall 面=站 33);默认动作=
忽略(不 terminate);无 ERESTARTSYS 重启;handler 嵌套=挂起
不丢;非 futex 的 pend 型阻塞(sem/msg 等待者)被信号唤醒后由
其 take 环吸收重挂,投递延至自然醒(v0 边界:信号感知面=futex
+wait_mask+RUN/SLEEP);SLEEP 型不打断;robust list/PI/
REQUEUE/共享内存 key=站 33+。

## 5. DoD

spec→实现→90 门×2 双绿→devlog→内容包 5→commit→镜像机→记忆。
