# 站32 · X 8 连推

## 推 1(总览)
musl 进门的两张内核脸:futex(锁原语底座:16 桶 key={proc,va},
WAIT 值校验+单窗阻塞+四路分诊;WAKE 按键摘链不读用户字)+
signal(位图 pending+syscall 出口投递帧手术)。号面=真 Linux
arm64 原号,站 33 直连。90 门 ×2。

## 推 2(futex 四路分诊)
醒后四种可能:超时→-ETIMEDOUT;被 WAKE→0;被信号打断→
-EINTR;值动了→0(协议:改字方必 WAKE)。入口值错→-EAGAIN
即返。阻塞机器第三笔复用(站 17 信号量机器零新等待逻辑)。

## 推 3(signal 帧手术)
投递点=syscall 出口(参考实现 SWITCH_OUT 站内形态):整帧 272B
存 proc->sig_saved+改写 elr=handler、x0=sig。handler 在目标
自身用户上下文执行,非内核代演;末尾 rt_sigreturn 恢复。
fork 继承 handler/blocked,pending 不继承(Linux 同律)。

## 推 4(战果:SP_EL0 定谳)
冒烟抓到怪象:handler 跑了、退出码 7 收了,但子进程打出
"别家函数的字面"还带新鲜 dt=4 活测量。反汇编+rodata 直证:
打印指针恰躺 shell_line 序言预存槽——不是打错字,是栈窗口
错位。

## 推 5(真身)
SP_EL0 驻 t->usp(vec.S 硬件域),不在投递保存的 272B 帧里。
handler 在中断 SP 下压自家帧,sigreturn 的 eret 跳过其
epilogue→用户 SP 永久下移一个 handler 帧→错窗读局部。
修=投递对存还 usp。

## 推 6(教训)
帧手术完整性清单+1:凡帧手术必清点 272B 之外的用户可见态;
凡手术必配对——存什么还什么。exec_setup 幸其本就直写 usp
所以没炸;这次是"存了 A 忘了 A 的影子"。

## 推 7(判据)
[86] sigdemo 一链证全家:跨进程 kill→子 futex -EINTR→handler
自身上下文执行→sigreturn→exit(7)→父收尸。futex 真实等待→
唤醒对=内核扑克任务代演"同址空间第二执行流"(只做队列
唤醒,真 threads=站 33+)。

## 推 8(下一站)
90 门双绿收口。下一站 33:musl——本站 SP_EL0 这一课恰好是
musl restorer/sigreturn 对齐必过的一关,雷提前拆了。
Apache-2.0,结对从零造,结对中国造。
