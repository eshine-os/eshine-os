# YouTube 分镜(站 65 bsh 子壳)

标题:musl 的 fork 不是 fork——bsh 子壳与两个潜伏十四站的契约缺口

## 分镜

1. **冷开场(0:00-0:45)**:bsh echo bsh65-ok 实跑输出——"这一行,
   背后是两个埋了十四站的坑"。
2. **号面考古(0:45-2:30)**:aarch64 syscall 表无 __NR_fork 的展开;
   musl fork()=clone(SIGCHLD,0) 源码(_Fork.c/vfork.c);我们的
   clone 线程族白名单如何拒掉它;为何十四站无人踩(没人 fork)。
3. **取证闭环(2:30-4:20)**:far=-0xA4 野访→站 64 core 文件送来
   三元→反汇编 elr=__pthread_self 内联→fork 不拷 tpidr 的缺角。
   "昨天造的锤子,今天敲中第一颗真钉子"。
4. **修法与判据(4:20-5:40)**:sys_clone 转发+sys_fork 拷 tpidr;
   bsh 判据面(ash 内建);场景 13/13 绿。
5. **诚实边界(5:40-6:30)**:printf=applet→execve ENOSYS;无参交互
   =vm0 全停深水(gdb 定格:泵活 tick 断)——挂账专场预告。
6. **收尾(6:30-7:00)**:门禁 182→183;"生态兼容的第一锹=号面考古"。

## 素材清单

- bsh echo 实跑输出
- musl _Fork.c/vfork.c 源码段(clone(SIGCHLD,0))
- busybox 反汇编(mrs tpidr_el0+sub 0xA4)
- 冒烟 13/13 PASS 输出
