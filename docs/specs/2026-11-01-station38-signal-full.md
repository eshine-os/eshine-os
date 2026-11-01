# 站 38 spec:signal 完整面——sigqueue/SA_RESTORER/默认终止/timedwait

日期:2026-11-01 · 作者:Julian Chu + Claude Code
真号面:musl arm64 __NR_rt_sigtimedwait=137/__NR_rt_sigqueueinfo=138
(零偏差兑现日);机制白描(参考实现):siginfo 携值/SA_RESTORER 回栈
trampoline/默认动作 terminate/定向线程。

## 1. 实现

- **rt_sigaction 4 参全脸**(134):handler+restorer 同装;
  投递改写 **x30=restorer**(帧槽@264),handler ret 落其内
  自调 rt_sigreturn——与站 32 旧契约(handler 末尾显式调)两立。
- **rt_sigqueueinfo**(138):pid/tid 双定向——tid 先解析(任务
  级待决位图 @760+载荷 @768,fork/clone 新槽必清),不中落
  pid 进程面(per-sig 单值数组);投递:任务级先于进程面,
  x1=sigval。
- **默认动作=终局**:无 handler 信号→投递返 128+sig→
  dispatcher 以退出码走 sys_exit 同链(fd 清+线程群扫+进程
  退+收尸唤醒),帧不再 eret(POSIX 面;SIGCHLD 忽略档=后弧)。
- **sigtimedwait**(137):v0 轮询面(命中耗并返号,否则
  -EAGAIN;阻塞全弧=后弧)。
- **附定谳**:唤醒走查四处的 t->entry 谓词=站 29 帧改写子
  盲区(恒 entry==NULL 被永久排除),首个自然睡 fork 子
  睡死实抓;活判据改状态面 TASK_LIVE(站 35 RT_FREE 同族)。

## 2. 判据(99→100)

[95] sigqdemo:queued val=48879+handler sig=10 val=48879
(携值+定向)+resumed(restorer 回栈)+default terminate 140
(无 handler 的 12=终局)+empty poll -11。旧 [95-99] 顺移
[96-100];三处同步 100 步。

## 3. 边界

siginfo 结构面(x2=ucontext)不做;ERESTARTSYS 重启弧不做
(阻塞被打断返 EINTR,调用方重试=语义等价,v0 记档);
sigtimedwait 阻塞不做;SIGCHLD/忽略档不做;每任务 pending
位图=tid 定向面,排队多值(sigqueue 队列)=后弧。

## 4. DoD

spec→实现→100 门×2→devlog→内容包 5→commit→镜像机→记忆→
直开站 39(FS 深面),不停。
