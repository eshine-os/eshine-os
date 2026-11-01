# 站 42 devlog:syscall 面补全(参考实现终审计补齐弧)

日期:2026-11-01 · 作者:Julian Chu + Claude Code
spec:docs/specs/2026-11-01-station42-syscall-completion.md
门禁:116→123(新 [112-118];旧 [112-116] 顺移 [119-123])

## 0. 一句话

站 41 收口后的参考实现差距终审(四路代理机制白描普查,账册
a2243a6)揪出站 27 清单漏掉的 syscall 面,本站按真号面纪律补齐
二十七号:调度管理组+信号补全+posix timer+itimer+robust futex
链+进程组会话+close_on_exec+uname。

## 1. 面

- **调度管理组**(118-127/168/140/141):setscheduler+getparam
  往返;apply_prio 写 base_prio(站 18 PI 纪律:基准位全树唯一
  回收点,boost 中只动基准);affinity 双核掩码(下次入队生效);
  yield=rt_sched(同级 FIFO 轮转);nice=16±n/2 线性两向一致。
- **信号补全**:sigpending 136/pigaltstack 132(注册回读,切栈
  后弧)/sigsuspend 133——站 24 rx 正牌三件套(wait_mask+
  BLOCK+rdy_remove 同 DAIF 窗)+掩码还原移 sigreturn
  (sig_retmask/suspflag:POSIX 掩码序=handler 用挂起窗掩码,
  完成还挂起前)。
- **posix timer+itimer**(107-111/102/103):SIGEV_THREAD_ID
  面单形;全局池 8 槽骑站 20 软定时器;回调语境禁调度→
  rt_sig_queueinfo_ctx(无 rt_sched 版唤醒);overrun=上发未收
  计数;ITIMER_REAL 同池骑坐到 leader。
- **robust futex**(99/100):set/get_robust_list+exit 路径遍历
  (musl 律:节点基=锁字,next@+0;用户窗范围门+100 跳保险丝)
  ——持锁者死,等者自动醒,判据活证。
- **进程组/会话**(154-157)+**close_on_exec**(fcntl F_SETFD/
  F_GETFD 真位,exec 语境逐 fd 摘除,父侧不受子扰)+uname 160。

## 2. 战雷(冒烟三连定谳,全息入 spec §3.1)

1. **getpid 槽号≠pid**:rt_pid_of 返任务池槽+1——壳 28 站全绿
   只因槽 0 恰等于 pid 1;第一个 fork 子里 getpid 现形(报 5
   而真 pid 2)。第一受害者定律新案:14 站无雷只因无人 fork
   子调 getpid。
2. **sigsuspend 漏 BLOCK 态**:站 41"调用者驻车"反面教材当日
   重演(wait_mask 置了但 RUN 态直调 rt_sched=空队列 WFI 永
   环,gdb 现场与站 41 尸检同貌)。
3. **clone 子落父 C 帧=野址读**:恢复点在 wrapper 返回帧,新栈
   无父局部;[sp+1304] 读出字面串指针野值,far=ASCII 串字节+
   addr2line 直指 sys_puts 字符环定谳;修=必入自有帧入口函数
   (musl __clone 同律)。**fork 子因整栈拷贝无此约束——两种
   child 两种纪律。**
4. 站 28 tp 守卫谓词扩展(posix timer 池=合法第二驻地,真野值
   照抓不变);handler restorer=0 面=末尾显式 rt_sigreturn(站
   32 契约照抄,漏则 ret 落 0xa 真身)。

## 3. 裁剪(诚实记档)

E(ERESTARTSYS 重启语义——改站 32 契约面大,futexdemo 判据
连锁)/H(calltrace+ps 观测扩展)/I(poll)三面后弧,账册在案,
站 43+ 候选。

## 4. 收口

123 门×2(过轮后垫 10 分钟冷却律——站 41 定谳的过轮尾余压
机制);三处同步(verify 123+壳走查 123+ARCHITECTURE 站 42 节);
内容包 5;commit;镜像机;记忆。
