# bsh 子壳:两个潜伏十四站的 musl 契约缺口

站 65,批次 F 首站:rtos> 主壳的并排共存面——`bsh <cmd>` 起 busybox
ash 子壳。

## 一个问题引出一锹考古

用户问:为啥 rtos 没有和 Linux 一样有 shell 命令行?定谳:vm0 有壳
(rtos> 手搓判据走查面),busybox 也能跑(站 40 双世界)——那就加个
`bsh` 命令起子壳。结果 busybox ash 一进来就"can't fork"。

考古:fork 的雷。**aarch64 号面根本没有 __NR_fork**(asm-generic
64 位段不定义),musl 的 fork() 和 vfork() 都实现为
`clone(SIGCHLD, 0)`。而我们的 sys_clone 是站 42 的线程面——只认
CLONE_VM|CLONE_THREAD 旗集,musl 这种"零线程旗"的 fork 全拒 -22。

为什么十四站没炸过?仓内所有 musl 程序(hello/beat/initmark/
jsontest/ztest/coreparse)全都**不 fork**。busybox ash 是仓内第一个
fork 的 musl 进程——第一个上门的客人,把潜伏的号面缺口暴露了。

修:sys_clone 零线程旗转发 sys_fork(进程复制语义)。

## 取证工具抓了自己的第一个真雷

修好 fork,ash 又炸:far=-0xA4 的野访。**站 64 昨天刚落的 ELF core
文件正好把死者三元送上门**:elr=busybox+0x24058,反汇编一眼——
`mrs x0, tpidr_el0; sub x0, x0, #0xA4`,musl `__pthread_self` 的内联。

真相:ash fork 出的孙不 exec、直接跑 busybox 代码,而 fork 面没拷
父的 TLS 指针(tpidr),孙的任务槽是 0,任务切换把 0 载入硬件——
__pthread_self 算出 -0xA4,野访。修:sys_fork 拷父 tpidr(站 35
线程面 SETTLS 的 fork 缺角,vec.S 切换存还链是现成的)。

昨天造的取证工具,今天抓了自己的第一个真雷。这就是工具链闭环的
意思。

## 诚实边界

`bsh echo xxx`、`bsh V=hi; echo $V` 全活(ash 解析器内建面);无参
交互模式(vm0 全停)是深水,gdb 定格现场(PC=pump_run_vcpu,tick 断
但泵活)记档专站收。printf 这类 applet 走 execve=ENOSYS——exec 是
用户态 loader 红线,ash 不认识它,记档。

门禁 182→183。
