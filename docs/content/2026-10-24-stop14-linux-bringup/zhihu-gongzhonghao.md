<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 让 Linux 在自研 Hypervisor 上跑起来,最难的不是让它跑,而是让它"死得明白"

> eshine-os 从零系列 · 第十四站:Linux bring-up。前一晚三门地基
> (设备树注入/GIC 仿真/PL011 完整化)刚验收,今天把真的 Linux 6.6
> 内核放上去——结局是一个 panic,但那是我们等了一整站的 panic。

## 先看结果

```
[vm2] [    0.540872] virtio_blk virtio0: [vda] 128 512-byte logical blocks
[vm2] [    1.048053]  driver: virtio_blk
[vm2] [    1.050670] Kernel panic - not syncing: VFS: Unable to mount root fs
[vm2] [    1.060746] ---[ end Kernel panic ]---
```

内核认出了我们的设备树、用上了我们的 GIC 仿真、把我们的 virtio
块设备注册成了 `vda`、拿它当 root 候选去挂载——没有文件系统,
干净地 panic。**1.06 秒内核时间,一整条启动链路。**

而这个"干净"背后,是五个埋在下面的根因。每一个都值得单独讲。

## 一、600 万次/秒的无声风暴

Linux 起飞后推进到 0.5-0.6 秒就没了下文:没有 panic,没有 Oops,
连宿主都没崩。用 QEMU 的异常录像(`-d int`)一看:每秒 **670 万次**
同样的 data abort 在 EL1 自陷成环,风暴中心停在
`enter_from_kernel_mode`——ARM64 内核异常入口读 `current` 的地方。

而 `current` 在 arm64 内核里,存在 **SP_EL0** 这个寄存器里。

问题来了:SP_EL0 是**全 EL 共享**的。而我们的 hypervisor,宿主
C 世界一直跑在 SP_EL0 上(每次 eret 进 guest,SP_EL0 都带着宿主
自己 .stack 的指针)。裸机 guest 从不读它,十三站绿灯;Linux
第一次碰 `current`,解引用出来的就是宿主栈上的一个地址——
level-0 翻译错,异常,handler 又读 `current`,又错,死循环。

连 Oops 都打不出来:printk 自己也要走 `current`。

**修法**:宿主世界整体迁到 SP_EL2,SP_EL0 从此归 guest;陷阱帧
扩一格 sp_el0(存/还随帧走)——这正是真 hypervisor 该有的
vCPU 现场纪律。

## 二、轮询式 handler:仿真层别替 guest "关灯"

SP 修完,内核推进到 0.65 秒又停了。这次连异常都没有——
一切看起来正常,但 `jiffies` 冻死在初值:整个启动**一拍 tick 都
没走过**,内核靠纯计数器打点撑到了 0.65 秒,然后在第一个
msleep 上永睡。

Linux 的 `arch_timer_handler_virt` 是**轮询式**的:中断来了,
它先读 CNTV 的 ISTATUS——硬件说"确实到点了",它才认账,
然后自己 mask 掉中断线、自己装填下一拍。

而我们沿用了裸机时代的注入路径:注入前先帮 guest 把虚拟定时器
"关线防风暴"——handler 一读 ISTATUS:0。"没来过人。"
IRQ_NONE。这一拍 tick 蒸发了,下一拍永远不会再装。

**修法**:跑动中的 guest,仿真层禁碰 CNTV——它的 handler 自己
会收尾;"关线"只属于睡着的 guest(没人替它收)。两路分治。

## 三、差 4 字节的世界

`virtio_blk` 都注册成 vda 了,分区扫描却永等一个不来的中断。
gdb 挂上活机读仿真设备状态:完成标志 `int_pend=1`,万事俱备;
再读仿真 GIC:`dist_isenabler[0] = 0`——**Linux 的 SPI 使能
一位都没写进来**。

GICv3 规范里,GICD 的 ISENABLER 家族:word0 管 INTID 0-31
(SGI/PPI,属重分发器的"银行视图"),word1(偏移 0x104)才是
SPI 32-63。我们站 12 的译码按自制约定把 word0 当成了 SPI 面,
word1 落进越界分支**静默吞掉**——裸机 guest 和我们同约定,
十四站绿灯;Linux 按规范写 0x104,一位都没落地。

**修法**:译码对齐 IHI0069(word0 诚实 RAZ/WI,word1 起是 SPI),
guest 侧偏移同步 +4。又是一堂"别发明标准"的课。

## 四、连 panic 都要设计:僵尸处决权

内核 panic 后是 `local_irq_disable` + 死循环。而我们调度器的
1 秒抢占全靠物理定时器中断——**PSTATE.I 连 EL2 路由的中断
都挡得住**。关尽中断的死循环 guest,在单核 ARM 上没有任何
硬件抢占手段,宿主 shell 跟着永冻。

真正的 Type-1 对付这种客人只有一个办法:**管理面绝对权威**。
`vmkill` 从"只许杀未跑的"扩权到 RUNNING——宿主处决僵尸,是
架构级必需品,不是运维便利。

## 尾声

五重根因,45 步门禁(新增 8 步全链判据)连续两轮真绿。下一步:
给这个 panic 配一个 rootfs,让 `Run /sbin/init` 接过接力棒。

从零到 Linux 上机,最难的不是让它跑起来——是让每一步"看起来
能跑"的地方,都经得起一个真内核的拷问。

(eshine-os:Apache-2.0,以 QEMU virt 为基准机,从零 clean-room,
27.04-dev)
