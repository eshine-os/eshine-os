# bsh 子壳:musl 的 fork 不是 fork 🐚

站 65 批次 F 首站!rtos> 旁起 busybox ash:
🐚 bsh <cmd> = busybox sh -c 子壳(echo/变量展开内建全活)
⛏️ 号面考古:aarch64 无 __NR_fork,musl fork=clone(SIGCHLD,0)——
   我们的 clone 只认线程族,全拒了十四站没人发现
🔍 站 64 core 文件抓到自己的第一个真雷:far=-0xA4 →反汇编 elr=
   __pthread_self 内联 → fork 不拷 tpidr 的 TLS 缺角
✍️ 修两条:clone 零线程旗转发 sys_fork+子继承父 tpidr

门禁 182→183,冒烟 13/13 ✅ 交互深水挂账专站 📌
