# X 8 推(站35 threads)

1/8
eshine-os 站35:真线程日。musl 1.2.5 的 pthread_create 原生跑通——4 线程争一把锁,join 收齐 counter=10。内核只兑现 musl 契约:clone(220) 帧整拷+x0=0,SETTLS→TPIDR_EL0 随切换存还,CHILD_CLEARTID→线程退场内核写 *ctid=0+futex 唤醒。零 pthread 知识进内核,契约进。 #embedded #kernel

2/8
最有意思的不是功能,是验它时撞出来的雷:冒烟 50% 挂率,panic 脸 elr 落在切换弹栈尾 `ldr [x0,#672]`,far=0x2a0=672 本尊;x0 是刚弹出的新线程 x0=0。三数全咬合——执行流以 EL0 身份跑进了弹栈代码中间。 #ARM64 #RTOS

3/8
根因:站28 让 syscall 上下文开中断(阻塞调用可被 tick 抢占)——但任务切换的帧伪造+簿记+弹栈机器没跟着原子化。弹栈中途 msr spsr_el1 已装新任务 EL0 态,tick 嵌进来存的就是嵌合帧:内核 elr+用户 psr。 #concurrency #kerneldev

4/8
为什么潜伏七站没爆?这窗口要爆发需要"同优先级竞争者让调度真的从弹栈中途切走"。此前所有负载嵌套 tick 全走不切换路径,窗口内返 EL1 续跑无害。pt 的 4 个同优先级线程=第一个让切走真发生的场景。竞态窗的爆发条件是触发组合凑齐。 #debugging

5/8
修法两枚掩码:rt_switch_to 在 push 后 psr 伪造前 msr daifset#0xf,EL0 返回宏头同掩。帧恢复机器不可重入,eret 自动从 psr 恢复开中断态。站28 的泊位语义一点没动——psr 由 C 侧显式写帧,与这个掩码无关。修后 pt 4/4,冒烟 5/5,94 门两连绿。 #embedded

6/8
另一枚冒烟抓的:契约迁移要 grep 全部调用面。futex 超时从 ticks 字面迁到 timespec 指针,只改了 futexdemo,sigdemo 子支还传 400——新契约下成了野指针。教训:改的是契约,查的必须是调用面全集。 #lessons

7/8
判据仪器自我翻案:先被一个烧 4 核的遗留 QEMU 骗了半轮(全部"冻结"测量都在污染窗里跑),又被自家判据脚本的松锚假 PASS 骗一轮(裸 "rtos> " 会吞 vmconsole 冲刷半行)。仪器先于判据——老铁律这次是违反之后的自我执行。 #testing

8/8
eshine-os:clean-room ARM64 Type-1 hypervisor+RTOS,Apache-2.0。站35/41:94 门×2 双绿,新增 [89] pthread 门。下一站 shm+xspace,直开不停。 #opensource #hypervisor
