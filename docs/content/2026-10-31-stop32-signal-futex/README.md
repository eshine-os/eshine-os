# 站32 口径:signal+futex,musl 的两张内核脸

**eshine-os** · 站 32(信号+futex)· 2026-10-31

## 本站做了什么

站 33 要带 musl(完整 libc)进门,它需要两张内核脸:**futex**
(musl 所有锁原语的底座)与 **signal**(POSIX 运行时面)。
两原语同站的架构理由=互为判据:真实"等待→唤醒"对由跨进程
信号打断 futex 等待完成(EINTR 契约)。号面用真 Linux arm64
原号(futex=98/kill=129/rt_sigaction=134/rt_sigreturn=139)。

- **futex**:16 桶,key={proc,va} 随等待者;WAIT=入口值校验
  (-EAGAIN)+站 21 单窗阻塞+四路分诊(超时/唤醒/打断/值动);
  WAKE=桶内 key 过滤摘链——不读用户字,纯队列操作。
- **signal**:位图 pending+**syscall 出口投递**(帧手术:整帧
  272B 存+elr=handler、x0=sig);handler 在目标自身用户上下文
  执行,末尾 rt_sigreturn 恢复。fork 继承 handler/blocked
  (Linux 同律),pending 不继承。

## 门禁与判据

90 门 ×2 双绿(新增 [85] futexdemo 四脸+[86] sigdemo 全链)。
sigdemo 一链证:跨进程 kill→子 futex -EINTR→handler 自身用户
上下文执行→sigreturn→exit(7)→父收尸。

## 本站战果:SP_EL0 域定谳

修完第一颗雷后,子进程三元判据打出**别家函数的字面**(带新鲜
dt=4 的活测量)。反汇编+rodata 直证:打出的恰是 shell_line
**序言预存槽**的字符串指针。真身:**SP_EL0 驻 t->usp(vec.S
硬件域),不在投递手术保存的 272B 帧里**——handler 在中断
SP 下压自家帧,sigreturn 跳过其 epilogue,用户 SP 永久下移
一个 handler 帧,子代码错窗读局部。修=投递对存还 usp。

**帧手术完整性清单+1:凡帧手术必清点 272B 之外的用户可见态;
凡手术必配对——存什么还什么。**

## 诚实边界(v0)

无 siginfo 排队(同号可丢);默认动作=忽略;无 ERESTARTSYS;
robust list/PI/REQUEUE/共享 key=站 33+。扑克任务代演同址空间
第二执行流(真 threads=站 33+)。
