# 站 65:bsh 子壳(内容包口径)

## 一句话

rtos> 主壳旁起 busybox ash 子壳(bsh <cmd>)——两个潜伏了十四站的
musl 契约缺口(fork 号面+子 TLS 继承)由第一个 fork 的 musl 进程上门讨。

## 技术叙事

1. **号面考古**:aarch64 没有 __NR_fork——musl 的 fork() 其实是
   clone(SIGCHLD,0)。我们的 clone 只认线程族(VM|THREAD)把 musl fork
   全拒;只是仓内十四站没有任何 musl 程序 fork 过,busybox ash 是第一个。
2. **forensics 闭环**:ash fork 的孙野访死在 far=-0xA4——站 64 刚做的
   ELF core 文件把死者三元送上,反汇编 elr 一眼定谳 __pthread_self
   =tpidr-0xA4 的 TLS 缺角。取证工具在自己身上兑现。
3. **诚实边界**:sh -c 子壳全活;无参交互模式(vm0 全停的深水)挂账
   专站,gdb 定格现场记档在案。

## 金句

- musl 的 fork 不是 fork,是 clone(SIGCHLD)——arch 号面考古是生态
  兼容的第一锹。
- 昨天造的取证工具,今天抓了自己的第一个真雷。

## 数字

- 门禁 182→183;两个契约缺口修复;13 步冒烟全绿;交互深水挂账 1 项。
