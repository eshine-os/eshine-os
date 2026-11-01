# 站38 口径:signal 完整面——携值/restorer/默认终止

**eshine-os** · 站 38(signal full)· 2026-11-01

## 本站做了什么

musl arm64 真号兑现日(rt_sigtimedwait=137/rt_sigqueueinfo=
138,零偏差):

- **SA_RESTORER**:rt_sigaction 4 参全脸,投递改写 x30=
  restorer——handler ret 落其内自调 rt_sigreturn,与旧显式
  契约两立(musl 二进制的信号回栈从此真兼容)。
- **sigqueue 携值+定向**:pid/tid 双解析,tid 先(任务级
  待决位图先于进程面),x1=sigval。
- **默认动作=终局**:无 handler 的信号以 128+sig 走 exit 链
  (POSIX 面)。
- **sigtimedwait** 轮询面。

## 本站战果:唤醒走查的 entry 谓词盲区

sigqdemo 首跑睡死实抓:四条走查的 `t->entry` 过滤器是站 16
帧自铸时代的活判据——fork/clone 子恒 entry==NULL 被永久
排除。**首个自然睡的 fork 子即爆**。修=活判据改状态面
(TASK_LIVE)。谓词的时代错位,一直在等第一个走新路的人。

## 门禁

100 门 ×2 双绿(新 [95] sigqdemo;旧 [95-99] 顺移 [96-100])。

## 诚实边界(v0)

siginfo/ucontext 结构面不做;ERESTARTSYS 不做(打断返 EINTR
+调用方重试=语义等价);timedwait 阻塞不做;忽略档不做。

## 关键词

ARM64 hypervisor · clean-room · RTOS · signal · SA_RESTORER ·
sigqueue · POSIX · 帧手术
