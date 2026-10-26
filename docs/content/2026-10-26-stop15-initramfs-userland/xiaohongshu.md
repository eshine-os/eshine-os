<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# PID 1 诞生了!Linux 在自研 Hypervisor 上跑进用户态 🎉

从零写 Type-1 第 15 站:上一站死在"没有 rootfs",这一站补上。

**战果**:
```
[vm2] Run /init as init process
[vm2] ~ # cat /proc/cpuinfo
CPU part	: 0xd07   ← 我们建模的 cortex-a57
```
busybox sh 以 PID 1 运行,vmconsole 直落交互 shell,
用户态读出自己的 CPU——全链每一层都诚实 🟢

**翻案现场** 🔍:站 14 我修过一个"FPEN 位错"——这次实测,
3<<22 写的根本不是 FPEN(真位 [21:20]),**从来是空操作**;
而 msr 是整寄存器写,把内核布好的位顺手清了零。boot 能跑
全靠内核自己写了 3<<20,那个"修复"修了个寂寞 😅
用户态第一发 SIMD 直接 "Unhandled exception" 恐慌。
教训:读 spec,别猜位。第三次栽在"仿真层越权"
(SP_EL0 → vtimer → CPACR,同一哲学:guest 的现场 guest 做主)

**新协议诞生** ⌨️:裸机客"睡着自动还控制台"遇上 Linux 壳
250Hz 恒 armed——控制台每睡被收,会话形同虚设。解法:
显式接管 + **逃生字符 Ctrl-]**(垫 Xen/ssh 惯例),
宿主的面永远夺不回不来

连 cpio 打包器都是仓内 40 行 python(mknod 要 root,fakeroot
缺席——从零气质:自己写一个)

门禁 48 步 ×2 真绿;站 14 的 VFS panic 端点被更强端点取代 ✅

#从零打造操作系统 #Hypervisor #ARM64 #Linux #开源
