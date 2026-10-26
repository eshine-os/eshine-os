<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# YouTube/B 站分镜脚本 — 第十五站:PID 1 诞生(rootfs 站)

总长目标:11-13 分钟。节奏:终端实录 + 白板讲解交替,"翻案"段落
放情绪高点。

## 分镜

| # | 画面 | 讲解 |
|---|---|---|
| 1 | 站 14 尾屏:VFS panic | 上一站 Linux 死在这里——没有用户空间。今天的任务:把 init 接力的那一棒造出来 |
| 2 | 终端:`Run /init as init process` → `~ #` → `cat /proc/cpuinfo` → CPU part 0xd07 | 结果先行:PID 1 是 busybox,shell 里读出了我们建模的 CPU——项目史上第一个用户态 |
| 3 | 白板:initramfs 槽位图(IPA 0x44000000 / 宿主 0x5C000000 / DTB initrd 属性) | 装载形态:bootloader 同构——定长 8MB 未压缩 cpio,DTB 写死边界,零运行时 patch;内核配方零补丁 |
| 4 | 代码屏:tools/mkinitcpio.py 40 行 | mknod 要 root、fakeroot 缺席——那就自己写个 newc 打包器,设备节点走 spec 行 |
| 5 | 终端实录:首跑死法(panic backtrace 特写) | 第一次跑:过了 VFS panic、PID 1 都诞生了,死在 fpsimd_load_state 首条 SIMD |
| 6 | 白板:CPACR 位图,[21:20] vs [23:22] 两个箭头 | 翻案环节:FPEN 真位是 [21:20];站 14 的 3<<22 从来是空操作——boot 能跑全靠内核自己;而 msr 整寄存器写,把我们没懂的位清了零 |
| 7 | 字卡:读 spec,别猜位 | 三次栽在"仿真层越权":SP_EL0、vtimer、CPACR——同一哲学:guest 的现场 guest 做主 |
| 8 | 终端:shell 会话输入喂给宿主的实录 | 第二个坑:裸机客"睡着自动还控制台"遇上 Linux 壳 250Hz 恒 armed——控制台每睡被收,会话形同虚设 |
| 9 | 白板:三条归还路径(vmconsole host / VM 死亡 / Ctrl-] 0x1d) | 解法:显式接管 + 逃生字符——垫 Xen/ssh 的老惯例,宿主的面永远夺得回 |
| 10 | 终端:make verify 48 步 ×2 全绿 | 门禁双绿;站 14 的 VFS panic 端点被更强端点取代——这是判据的演进,不是丢弃 |
| 11 | 结尾卡:eshine-os 仓库 + 下一站预告 | 从零到用户态,最难的不是跑通,是承认每个"看起来通了"的地方都可能藏着自己埋的雷 |

## 标题候选
- 《PID 1 诞生:Linux 在自研 Hypervisor 上跑进用户态》
- 《我修过一个不存在的 bug:FPEN 翻案现场》
- 《从零写 Type-1,第 15 站:rootfs 与逃生字符》

## 置顶评论素材
CPACR 位图 + 三次越权清单 + 仓库链接 + "你想在 shell 里跑什么?"征集
