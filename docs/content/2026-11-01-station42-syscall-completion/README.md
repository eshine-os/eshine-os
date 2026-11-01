# 站42 口径:syscall 面补全(参考实现终审计补齐弧)

**eshine-os** · 站 42(syscall 补全)· 2026-11-01

## 本站做了什么

- **调度管理组真号**(118-127):setscheduler/getparam/affinity/
  yield/rr_get_interval/getcpu+setpriority/getpriority(nice 线
  性两向一致);PI 基准位纪律全守。
- **信号补全**:sigpending/sigsuspend(掩码还原移 sigreturn=
  POSIX 掩码序)/sigaltstack(注册回读)。
- **posix timer+itimer**(107-111/102/103):SIGEV_THREAD_ID 单
  面单形,全局池骑站 20 软定时器,tick 语境禁调度版投递,
  overrun 记账。
- **robust futex 链**(99/100):持锁线程死,exit 路径遍历 musl
  链自动唤醒等者——判据活证。
- **进程组/会话+close_on_exec+uname**(154-157/fcntl 位/160)。
- 号面全部真 Linux arm64 号,musl 头钉死零臆测。

## 战雷三枚(冒烟定谳)

- **getpid 槽号≠pid**:壳 28 站全绿只因任务槽 0 恰等于 pid 1,
  第一个 fork 子 getpid 现形——第一受害者定律新案。
- **clone 子落父 C 帧=野址读**:新栈无父局部,[sp+1304] 读出
  字面串指针;两种 child 两种纪律(fork 整栈拷贝 vs clone 自有
  帧入口)。
- **sigsuspend 漏 BLOCK 态**:站 41"调用者驻车"反面教材当日重演。

门禁 116→123,两连 RESULT PASS;三处同步同 commit。
