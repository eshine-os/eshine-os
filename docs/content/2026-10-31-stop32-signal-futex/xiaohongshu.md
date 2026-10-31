# 一个 bug 让子进程说胡话:SP 不在帧里 🕵️

造 hypervisor 第 32 站:signal+futex,90 门 ×2 双绿✅

两张内核脸(musl 进门必备):
futex=musl 锁原语底座
  WAIT 值校验+阻塞+四路分诊
  WAKE 按键摘链,不读用户字
signal=位图 pending+syscall 出口投递
  帧手术:272B 整存+PC=handler+x0=信号号
  handler 在子进程自己上下文里跑,非内核代演🎭

本站战果=SP_EL0 定谳:
handler 跑了、退出码 7 也收到了
但子进程打出"别家函数的字面"
还带新鲜 dt=4 活测量?!

反汇编+rodata 直证:
打印的指针恰好躺在 shell_line
序言预存槽 [sp+360]/[sp+720] 里
→ 不是打错字,是栈窗口错位💥

真身:SP_EL0 驻 t->usp(vec.S 硬件域)
根本不在我保存的 272B 帧里!
handler 在中断 SP 下压了自家帧
sigreturn 的 eret 跳过了它的 epilogue
→ 用户 SP 永久下移一个 handler 帧
→ 被打断代码从错窗读局部

修=投递对存还 usp(存什么还什么)📖
帧手术完整性清单+1:
272B 之外的用户可见态必清点

另一颗雷(冒烟即抓):
子进程判据写 pid==0——
自家站 29 契约明明是 RT_FORK_MAGIC
自家 ABI 记岔,子落穿父路径永挂😅

futex 真实等待→唤醒对怎么证?
内核扑克任务代演"同址空间第二执行流"
(只做队列唤醒,不碰用户字)
真 threads=站 33+

#eshineOS #hypervisor #微内核 #musl #操作系统 #从零造轮子 #开源
