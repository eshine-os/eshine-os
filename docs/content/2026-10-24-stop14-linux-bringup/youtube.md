<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# YouTube/B 站分镜脚本 — 第十四站:Linux 上机(三门会师 → VFS panic)

总长目标:12-14 分钟。画面节奏:录屏(QEMU 终端)与讲解脸/画面
对切,风暴段落配可视化。

## 分镜

| # | 画面 | 讲解 |
|---|---|---|
| 1 | 终端滚动:[vm2] Booting Linux → initcalls → console handover | 上一站我们备好了三门地基:设备树、GIC、串口。今天:把真 Linux 6.6 放上去 |
| 2 | 录屏:0.57 秒后一片寂静,光标闪烁 10 秒 | 第一次跑:静默死。没有 panic,没有 Oops,宿主也不崩——最难查的一种死 |
| 3 | 可视化:每秒 670 万个相同异常点连成风暴 | QEMU -d int 录像:670 万次/秒的 data abort 自陷环,停在 enter_from_kernel_mode |
| 4 | 板书:SP_EL0 框图,EL1/EL2 共用一支笔 | 真凶:SP_EL0 是全 EL 共享的。arm64 内核拿它存 current;我们拿它当宿主栈。每次进 guest,current=宿主栈指针 |
| 5 | 板书:向量入口自陷环 + printk 也要 current 的标注 | 第一次读就错,handler 再读再错——连 Oops 都打不出来,因为 printk 也要 current。为什么前十三站绿灯?裸机 guest 从不读它 |
| 6 | 录屏:修复后 0.65s 又停;vmstat jiffies 冻在 INITIAL | 第二关:jiffies 冻死。启动 0.65 秒里一拍 tick 都没消费——内核全靠计数器撑着 |
| 7 | 板书:Linux timer handler 伪代码(读 ISTATUS → 认账 → 自己 mask → 自己装填) | Linux 的定时器 handler 是轮询式。我们注入前"好心"关了定时器,handler 一看 ISTATUS=0:"没来过人"。tick 蒸发,下一拍永不再装 |
| 8 | 板书:两路分治(跑动客禁碰 CNTV/睡客才关线) | 修法哲学:跑动中的 guest,仿真层别替它关灯;它的 handler 自己会收尾 |
| 9 | 录屏:vda 注册成功 → 分区扫描永等;gdb 挂活机读状态 | 第三关:virtio 都成 vda 了,完成中断却永不来。gdb 直读宿主内存:int_pend=1 万事俱备,SPI 使能字=0 |
| 10 | 板书:GICD 家族 word0 vs word1,差 4 字节 | Linux 按 IHI0069 写 0x104(word1=SPI);我们自造约定用 word0,word1 被静默吞掉。别发明标准 |
| 11 | 录屏:Kernel panic - not syncing: VFS: Unable to mount root fs | 终点画面:预期的 panic。1.06 秒,全链路,vda 还当了 root 候选 |
| 12 | 板书:panic=关中断死循环 → PSTATE.I 挡住 EL2 路由中断 → 无硬件抢占 | 连 panic 都要设计:关尽中断的死循环客,单核 ARM 无解,唯一出路是 vmkill 扩权——宿主的绝对处决权 |
| 13 | 录屏:make verify 45 步 ×2 全绿 + 五重根因列表复盘 | 45 步门禁双绿收官。下一站:rootfs,让 /sbin/init 接力 |
| 14 | 结尾卡:eshine-os(Apache-2.0)+ 仓库地址 | 从零到 Linux 上机,最难的不是让它跑——是让每一步"看起来能跑"的地方都经得起真内核的拷问 |

## 标题候选
- 《Linux 在自研 Hypervisor 上死了,这是我们最开心的一次》
- 《670 万次/秒的无声风暴:arm64 current 语义的一个 4 字节陷阱》
- 《从零写 Type-1,第 14 站:三门会师,Linux 上机》

## 置顶评论素材
五重根因清单 + 仓库链接 + "哪一关你最想单独展开?"投票
