<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 我的 Linux 终于跑到了用户态——顺便承认,我修过一个不存在的 bug

> eshine-os 从零系列 · 第十五站:rootfs。上一站内核死在
> "VFS: Unable to mount root fs",这一站把用户空间补上——
> PID 1 诞生了。

## 先看结果

```
[vm2] Run /init as init process
[vm2] ~ # cat /proc/cpuinfo
CPU implementer	: 0x41
CPU architecture: 8
CPU part	: 0xd07
```

busybox `sh` 以 PID 1 在我们自研的 Type-1 hypervisor 上运行,
`vmconsole 2` 直接落进交互 shell,`cat /proc/cpuinfo` 从**用户态**
读出 CPU 身份:cortex-a57(0xd07)——我们 QEMU 命令行里指定的
那颗。每一层都诚实:QEMU → EL2 → stage-2 → Linux 6.6 →
initramfs → /init → sh → /proc。

## 这一站的形态:bootloader 同构

initramfs 不是内核内嵌的,是**宿主按启动协议装载**的:定长 8MB
未压缩 cpio,放在 guest 物理地址 0x44000000,DTB 的 chosen 节点
写 `linux,initrd-start/end`——和真开发板过 U-Boot 没有本质区别。
内核配方零补丁:6.6 已固化未压缩 cpio 支持(设计期以为要开
CONFIG_RD_NONE,实测该选项根本不存在,070701 魔数直入解析器)。

连 cpio 打包器都是仓内 40 行 python:mknod 要 root 而 fakeroot
缺席,newc 格式一页可读——设备节点用 spec 行虚拟 mknod,
`--dev console:5:1`,干净利落。

## 承认一个旧案的翻案

站十四我修过一个 "FPEN 位错" 的 bug,当时记录是"3<<20 错位,
修正为 3<<22"。这一站首跑,boot 冲过 VFS panic、PID 1 诞生、
项目**第一次返回用户态**——然后死在 `fpsimd_load_state` 首条
SIMD 指令上:EC=0x07,"Unhandled exception"。

排查结论让我脸红:内核 sysreg.h 写着 `CPACR_EL1_FPEN_EL1EN =
BIT(20)`——**FPEN 的真位是 [21:20],站十四的 3<<22 写的是别的
位,对 FPEN 从来是空操作**。那 boot 为什么能跑?因为内核的
`__cpu_setup` 自己写了 3<<20(那个"修复"修了个寂寞)。真正的
凶手是:`msr cpacr_el1` 是**整寄存器写**,我们每回合的 3<<22
把内核布好的 FPEN 顺手清了零。第一个显式 SIMD——用户态 FP
恢复——就崩。

教训:读 spec,别猜位。修复也简单到脸疼:那一行,删掉。
**CPACR 是 guest 的现场,guest 自己做主**——这已经是本项目
第三次栽在"仿真层越权"上(SP_EL0、vtimer、CPACR)。

## 逃生字符的诞生

shell 会话首验:prompt 出来了,后续输入却全喂给了宿主 shell。

根因是第六站给裸机 guest 的体贴设计:"guest 睡在定时器上时,
控制台自动归还宿主,宿主睡期间要能敲命令"。裸机 guest 睡觉
偶尔、短暂;**Linux shell 等你敲键时 tick 永远 armed(250Hz),
按这个判据,控制台每次睡都被收走**——同一台机器,两种生命
节律,协议打架了。

解法把控制台所有权改成**显式接管**,归还走三条路:`vmconsole
host`、VM 死亡、以及一个**逃生字符 Ctrl-](0x1d)**——宿主路由
层截获,永不进 guest,垫的是 Xen/ssh 的老惯例。从此卡死的
guest 夺不走宿主的面,活着的 Linux shell 也守得住自己的。

## 尾声

48 步门禁,连续两轮真绿。站十四的 VFS panic 端点被更强端点
取代——boot 越过 root-mount 直达用户空间。下一站:让这个
shell 里能做的事更多(或按您的方向定夺)。

从零到用户态,最难的不是跑通,是承认每一个"看起来通了"的
地方都可能藏着下一个自己埋的雷。

(eshine-os:Apache-2.0,QEMU virt 基准,从零 clean-room,27.04-dev)
