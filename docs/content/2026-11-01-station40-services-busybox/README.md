# 站40 口径:服务收编+busybox——双世界同字节

**eshine-os** · 站 40(服务生态)· 2026-11-01

## 本站做了什么

- **busybox 双世界**(旗舰判据):同一 264KB musl-static
  精选集字节,vm0 经我们内核+用户态 loader 跑,vm2 经真
  Linux 跑——md5sum 同哈希。同工具,同输入,同输出,一台
  hypervisor,两个 OS。applet 面由 qemu-user -strace 实测
  定谳(零臆测):openat/getdents64/newfstatat/brk/clock
  族/uid 族/fcntl/dup3 全真 Linux 号。
- **服务收编三新服务器**:/dev/random(xorshift64* 混时基,
  fd READ+getrandom 两脸同源)、/proc(任务表文本行+READDIR
  第二消费者)、/dev/slog(环+级别回显策略);时间真号
  clock_gettime/getres/gettimeofday(BOOT_EPOCH+单调)。
- **fd 层真号族**:目录前缀柄(名空间归服务器:直名未挂者
  回退路由 ramfs 元名,basename 枚举+首现去重);read/write
  fd≥3 路由 fd 机器(fd 0=控制台契约不动)。
- **init+inittab 形态**(壳代处理器):sysinit/wait/once/
  respawn 四 action;respawn=门拍制(确定性单拍,自主连拍=
  竞态放大器挂账)。

## 战果:第一受害者定律三连

站 40 把用户态从"每次一两发"变成"每命令一发"的循环机器,
一次性炸出三家 11-12 站的潜伏缺口:

- **ASID 从不还位**(站 29 注释谎称"已尽释"):第 16 次
  进程创建即 EAGAIN。
- **el0 域复用残值**(站 28 漏复位清单):deepdiver 静默
  不潜,水位门红。
- **阻塞 wait4 无 ECHILD 路**(站 29 v0 子集空缺):已收
  pid=永挂。

**判据面越像真用户,遗产缺口越无处藏。**

## 门禁

103→111 门,两连 RESULT PASS([99-106] 新八门,旧门顺移)。

## 诚实边界(v0)

sock/unix 域/drvfw/coredump/sysfs=后弧;熵源非密码级;
realtime 无 RTC(drvfw 后弧);respawn 自主连拍=竞态窗
挂账(站 41 取证弧);verify 散布族=站 25/33 记档家族
(字节窗损,孤立复刻全过)。

## 关键词

ARM64 hypervisor · clean-room · busybox · musl · resmgr ·
双世界 · 容量账 · 第一受害者定律
