# 站 42:补 27 个真号,和 getpid 的十四年等身雷

eshine-os 站 42,源头是站 41 收口后的一次全面对账:四路代理
把参考内核 522 个文件做机制白描普查,和我们的三十文件 RTOS
逐面比对,揪出站 27 差距清单漏掉的一整层——syscall 面。这一站
按真 Linux 号补了二十七个:调度器管理组(sched_setparam/
setscheduler/affinity/yield/rr_get_interval,连 nice 的两向换
算都做成线性自洽)、信号补全(sigpending/sigsuspend/sigaltstack,
其中 sigsuspend 的掩码还原挪进了 sigreturn——POSIX 要求 handler
用挂起窗掩码、完成后才还原挂起前的掩码,这个次序很多 v0 都做
反)、posix timer 全家加 itimer(SIGEV_THREAD_ID 通知,骑在
站 20 的软定时器上,tick 回调语境禁调度,投递走无 rt_sched 的
唤醒版)、robust futex 链(持锁线程死了,内核在 exit 路径遍历
musl 注册的链,把还在等锁的线程自动唤醒——判据就是活证)、
进程组/会话、close_on_exec、uname。

但今天想讲的还是雷。第一颗:getpid 返回的是任务池槽号+1,不是
进程 pid。壳二十八站全绿,只因壳任务恰好占着池槽 0,而它的进程
pid 恰好也是 1——两个身份碰巧相等,十四个站没有一个人在一个
fork 子进程里调过 getpid。直到我写 sigdemo2,子进程自挂信号,
getpid 报出 5 而真实 pid 是 2,一发现形。第一受害者定律又添一案:
缺口一直在,等第一个走新路的顾客。

第二颗更有意思:clone 出来的线程,恢复点是 wrapper 的返回帧,
栈是全新的。我在 demo 里让线程落回父亲的 C 帧——编译器把字符串
字面指针 spill 在父亲帧的 [sp+1304],新栈上那位置是野的,线程
拿着 ASCII 字节当指针解引用,fault 地址直接是字符串内容。
addr2line 一指:sys_puts 的字符环。修法只有一个:clone 子必须
立刻调一个自有帧的入口函数,永不返回父帧——musl 的 __clone 一
直就是这么干的。而 fork 子因为整栈被 eager 拷贝,反而没有这个
约束。同一天里,两种 child,两种纪律。

第三颗是自己的旧课重修:sigsuspend 第一版漏了 BLOCK 态,直调
rt_sched,空队列 WFI 永环——和站 41 那颗"吞行自雷"的调用者
驻车一模一样的病理,隔一天重演。gdb 现场都长一个样:state=RUN,
wait_mask 挂着,CPU 停在 wfi。

裁剪也如实记档:ERESTARTSYS 重启语义、calltrace 观测面、poll
三张面另立后弧。门禁 116→123,两连全绿。
