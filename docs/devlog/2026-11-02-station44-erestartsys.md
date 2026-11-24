# 站四十四:ERESTARTSYS 可重启系统调用语义

**日期**:2026-11-02
**状态**:实现完成,冒烟 7/7(首试全绿,零战雷);门 [87] 原位改针
(总 126 不变)按批令不跑
**spec**:docs/specs/2026-11-02-station44-erestartsys.md

## 契约迁移

站 32 的"-EINTR 直返"升级为 Linux 真语义的站内形状:信号打断
futex WAIT → 内部 -512(不越 ABI),分发器单点帧手术——**elr
回拨 4 重取 svc**(ELR 本指 svc 下一指令)+ **x0 槽不写保入参**
(重执行同参再入)。随后的出口信号投递整帧存 sig_saved(含回拨
elr+原 x0),handler 归还即重执行;无 handler 默认动作=终局,
回拨无害(不存在死循环面)。

- **-EINTR 只存 POSIX 特例 sigsuspend**(恒返,Linux 同律;
  站 42 sigdemo2 判据零动)。
- **唯一生产者=futex WAIT**(审计面):sem/msg 打断被 take 环
  吸收重挂(醒后出口投递,站 32 既有契约);ppoll 轮询环同;
  poll 家族 Linux 本就 never-restart。

## 判据迁移(sigdemo)

子挂 futex → 父 kill(10) → **handler 内戳字 sig_word=1** →
sigreturn → 重启再入 → 入口值校验 *w!=expect → **-EAGAIN**。
一判决两事:elr 没回拨=执行 svc 后指令错窗垃圾;x0 没保入参=
拿 -512 当 uaddr 野访 panic。旧脸若回归(r=-4)落 FAIL 行,
门红自抓——判据的反面雷自埋。

sig_word 上提文件域(handler 要戳;子承 .user 页拷同 VA,
COPY_UCONTEXT 律)。

## 纪律执行记录

- 警告必读:build 三警告逐一核对(git stash 干树同款=存量,
  非本站引入);**pop 后重建**(站 31 流程雷,stash 测试会拿旧
  树覆二进制)。
- 首试全绿:分发器帧手术一击即中——站 32 "帧手术完整性清单"
  (动什么还什么、272B 帧外态清点)沉淀的红利。

## 非目标(记档)

ERESTARTNOHAND/ERESTART_RESTARTBLOCK(restart_block 对象、nanosleep
剩余时长)——无消费者不落码;SA_RESTART flags 面留后弧站。
