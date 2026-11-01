# X 8 推(站 40 服务收编+busybox)

1/8
eshine-os 站 40:旗舰判据一句话——同一 264KB musl-static busybox 字节,vm0(我们内核+用户态 loader)与 vm2(真 Linux)跑出同一 md5。真工具,两个 OS,一台 hypervisor。 #embedded #busybox

2/8
applet 需求面零臆测:qemu-user -strace 实测 busybox 逐 syscall,再按真 Linux arm64 号落码——openat 56/getdents64 61/newfstatat 79/brk 214/getrandom 278/uid 族全零值。仪器先于判据。 #kernel

3/8
getdents64=每调返一名:POSIX 允许短读,慢但正确;dirent64 内核零填,NUL 收尾=busybox 消费面安全。newfstatat=STAT wire(mode<<32|size),musl aarch64 stat 144B 零填+定点写——结构布局以 inst 头文件实测定谳。 #systems

4/8
目录不是挂名,是服务器对前缀的解析结果:openat("/etc") 直名未挂→回退路由 ramfs 元名→服务器侧前缀匹配→目录前缀柄。basename 枚举+首现去重 O(n²) 小表+深径判 DIR。名空间归服务器,参考实现律。 #design

5/8
本站战果=第一受害者定律三连:站 40 把 fork/exec 变成每命令一发的循环机器,同一天炸出三家 11-12 站潜伏缺口——ASID 从不还位(注释谎称已尽释)/el0 复用残值(漏复位清单)/阻塞 wait4 无 ECHILD 路。 #debugging

6/8
三案同一形状:缺口一直在,等第一个高频循环者上门。历届电池从未超 15 次进程创建,所以 ASID 泄漏潜伏了 11 站——respawn 每命令一拍,第 16 次 fork 即爆。判据面越像真用户,遗产缺口越无处藏。 #testing

7/8
"加 print 就好"的解构:build 敏感的寄存器腐蚀,加打印就绿删打印就红——真身是函数调用边界让 callee-saved 从帧重载。把探针翻译成正身代码(noinline 辅函数)才叫修复,否则只是遮盖。还有容量课:栈 hint 挪一档,游标顶窗尾,musl 线程栈族全队 EAGAIN。hint=全局水位线。 #lessons

8/8
eshine-os:clean-room ARM64 Type-1 hypervisor+RTOS,Apache-2.0。站 40/41:111 门两连 RESULT PASS(新 [99-106] 八门,旧门顺移)。服务收编 random/procfs/slogd+时间真号+init inittab 形态全落。下一站 CAPR,最后一站。 #opensource #hypervisor
