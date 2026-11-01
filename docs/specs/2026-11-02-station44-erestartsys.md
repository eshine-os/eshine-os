# 站四十四:ERESTARTSYS 可重启系统调用语义

**日期**:2026-11-02 | **序**:批令 43-48 第六站之二 | **基准**:参考实现白描
(Linux ERESTARTSYS 机制);站 42 裁剪记账 E 项兑现日。

## §1 问题

站 32 契约:信号打断 futex 等待 → 直接返 -EINTR。Linux 真语义更细:
**无 handler(默认动作)→ 内部 -ERESTARTSYS,帧手术回拨 elr 重执行
svc;有 handler 且无 SA_RESTART → -EINTR**。-EINTR 直返丢两件事:
①调用方拿假错须自行重试;②帧手术约定(存什么还什么)被"半途
而废的 syscall"打破——x0 槽该还是入参语义。

## §2 v0 站内形状(契约决定)

- **无 SA_RESTART 旗面(v0)→ 站 44 后 futex 打断恒重启**(隐式
  always-restart;flags 面留后弧站)。-EINTR 只保留 POSIX 特例
  **sigsuspend**(恒 -EINTR,Linux 同律,站 42 sigdemo2 判据不动)。
- **唯一生产者=futex WAIT**(站 44 审计面):sem/msg 打断不返错
  ——被其 take 环吸收重挂,醒后 syscall 出口投递(站 32 既有
  契约);ppoll 轮询环同。poll 家族 Linux 本就 never-restart。
- **机制(通用,分发器单点)**:syscall 返回 -512 → ①x0 槽
  **不写**(保入参=重执行同参再入);②elr 回拨 4(ELR 指向 svc
  下一指令,减 4=重取 svc);③随后的出口信号投递接管:整帧 272B
  存进 sig_saved——**含回拨后的 elr 与未动的 x0**——handler 落
  自家用户上下文,rt_sigreturn 恢复整帧,eret 落回拨 elr=重执行。
  无 handler 默认动作=终局走 exit 链,回拨无害。
- **帧手术完整性清单**(站 32 律):动 elr 一处、免写 x0 一处,
  272B 帧外用户可见态(SP_EL0)不动 ✓。

## §3 判据(sigdemo 迁移——"futexdemo 判据迁移"兑现)

- 子挂 futex(10s 兜底 ts)→ 父 1s 处 kill(10)→ **handler 内
  自戳字 sig_word=1** → sigreturn → **重启再入** → 入口值校验
  *w!=expect → -EAGAIN。
- **一判决两事**:elr 回拨 4(没回拨=执行 svc 后指令错窗垃圾)、
  x0 保持入参(没保持=拿 -512 当 uaddr 野访 panic)。
- 新针线:"sig: futex restarted -> -EAGAIN ok"(替旧
  "sig: futex -> -EINTR ok");旧脸回归雷:若重启破,r=-4 落
  FAIL 行,门红。
- 门 [87] 原位改针(编号不动);回归面:futexdemo 四脸不动/
  sigdemo2 -EINTR(sigsuspend 特例)不动。

## §4 非目标(记档)

- ERESTARTNOHAND/ERESTART_RESTARTBLOCK(restimer 族)——无消费者
  不落码。
- restart_block 对象(Linux nanosleep 重启剩余时长)——v0 无
  nanosleep 号面。
- 跨 syscall 的 flags 面(SA_RESTART)——musl 侧真需求出现再议。
