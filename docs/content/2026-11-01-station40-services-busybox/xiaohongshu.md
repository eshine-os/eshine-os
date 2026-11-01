# 站 40:busybox 双世界 🐝(第一受害者定律三连)

同一 264KB musl busybox 字节跑两个 OS——vm0 我们内核,
vm2 真 Linux,md5sum 同哈希铁证。

✅ 真用户态:applet 需求面 qemu-user strace 实测定谳
   (openat/getdents64/newfstatat/brk/clock 族全真号)
✅ 服务收编:/dev/random + /proc + /dev/slog 三新服务器
   +clock_gettime 时间真号
✅ init+inittab 形态:sysinit/wait/once/respawn 四 action
✅ 容量账:POOL 20/CH 16/CONN 24/NAME 32+栈 hint 回位

## 第一受害者定律 🕵️

站 40 把 fork/exec 变成"每命令一发"的循环机器,一天炸出
三家 11-12 站的潜伏缺口:

① ASID 从不还位(站 29 注释谎称已尽释)→ 第 16 次 fork
   即 EAGAIN
② el0 域复用残值(站 28 漏复位清单)→ deepdiver 静默不潜
③ 阻塞 wait4 无 ECHILD 路(站 29 空缺)→ 已收 pid=永挂

**缺口一直在,等的是第一个高频循环者。**
判据面越像真用户,遗产缺口越无处藏。

eshine-os · clean-room ARM64 hypervisor+RTOS
Apache-2.0 · 111 门两连 RESULT PASS · 站 40/41
#嵌入式 #RTOS #busybox #内核开发 #程序员日常
