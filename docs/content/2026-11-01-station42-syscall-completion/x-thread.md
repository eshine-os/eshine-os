# X 8 推(站 42 syscall 面补全)

1/8
eshine-os 站 42:对参考内核 522 文件做四路机制白描普查,揪出站 27 差距清单漏掉的整层——syscall 面。本站补 27 个真号,全部从 musl aarch64 头钉死,零臆测。 #kernel #syscalls

2/8
调度管理组全落:setparam/setscheduler/getparam/getscheduler/affinity(双核掩码)/yield/rr_get_interval/getcpu+setpriority/getpriority。改在跑任务优先级只写 base_prio——PI 基准位是全树唯一回收点,boost 中只动基准,卸载自然归位。 #scheduling

3/8
信号补全三号:sigpending 136/sigaltstack 132/sigsuspend 133。sigsuspend 的掩码还原挪进 sigreturn(POSIX 掩码序:handler 用挂起窗掩码,完成才还原挂起前)——很多 v0 做反的地方。 #signals

4/8
posix timer+itimer(107-111/102/103):SIGEV_THREAD_ID 通知骑站 20 软定时器,tick 回调语境禁调度→无 rt_sched 版唤醒投递;overrun=上发未收计数;ITIMER_REAL 同池骑坐。 #posix

5/8
robust futex 链(99/100):musl pthread 启动即注册;持锁线程死,内核 exit 路径遍历链(节点基=锁字 offset 0)自动唤醒等者。demo:clone 线程等锁,leader 持锁退出,等待者被死亡唤醒。 #robust

6/8
本站第一雷:getpid 返回的是任务池槽号+1,不是 pid。壳 28 站全绿只因槽 0 恰等于 pid 1——第一个 fork 子调 getpid 的 demo 当天现形(报 5 真身 2)。第一受害者定律新案。 #debugging

7/8
第二雷:clone 线程落回父 C 帧=[sp+1304] 读出字符串字面当指针,far 直接是 ASCII 字节。addr2line 指向 sys_puts 字符环。修=clone 子必入自有帧入口函数(musl __clone 同律);fork 子整栈拷贝无此约束——两种 child 两种纪律。 #lesson

8/8
站 28 的 tp 守卫谓词同站扩展:posix timer 池成为合法第二驻地,真野值照抓不变。门禁 116→123 两连绿;ERESTARTSYS/calltrace/poll 三面如实记档另立后弧。从零打造,第 42 站。 #eshineos #rtos
