# 站 40 · YouTube 口播稿

**[开场]**
eshine-os 第四十站,服务收编加 busybox。这站的判据只有一
句话:同一个两百六十四 KB 的 musl 静态 busybox,在我们自
己的内核上跑一遍,在真 Linux 上再跑一遍,md5 相同。真用户
态工具,两个操作系统,一台 hypervisor。

**[怎么做到的]**
需求面零臆测。busybox 到底调用哪些 syscall,不是查文档猜
的——qemu-user 加 strace,逐条实测:openat、getdents64、
newfstatat、brk、clock_gettime、uid 族,全部按真 Linux
arm64 号落进内核。getdents64 每次调用返回一个名字,POSIX
允许短读,慢一点,但正确。newfstatat 走一个新 wire 操作,
mode 和 size 打包在回执里,musl 的 stat 结构一百四十四字节,
零填加定点写。

服务收编补了三个新服务器:随机数、procfs、系统日志,再加
时钟真号。init 读 inittab,四个 action,sysinit 落一个标记
文件,busybox 亲自 cat 出来——文件系统、服务器、loader、
进程,全链自洽。

**[第一受害者定律]**
但今天最值得讲的不是功能,是三颗雷。站 40 把 fork/exec 从
"每次演示一两发"变成"每条命令一发"的循环机器——结果一
个下午,三家潜伏了十一个站的缺口接连爆雷。第一,ASID 从来
没有还过位,站 29 的注释写着"已尽释",代码只还了页表——
第十六次进程创建,当场 EAGAIN。第二,el0 域当年漏了复位清
单,深潜任务复用了早期子进程的槽,栈限位函数判成 EL0,静
默不潜。第三,阻塞 wait4 从来没有"非子进程立即返回"的
分支,同步 respawn 一撞已收的 pid 就永挂。

三案同一形状:缺口一直在,等的是第一个高频循环者。判据面
越像真用户,遗产缺口越无处藏——这大概就是用真程序当测试
的全部意义。

**[收尾]**
一百一十一门,两连全绿。下一站 CAPR 安全框架,最后一站,
不停。我们下站见。
