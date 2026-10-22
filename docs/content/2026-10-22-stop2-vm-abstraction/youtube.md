# YouTube / B 站分镜脚本 — 第二站:VM 抽象(双 VM + 生命周期)

## 标题(YouTube 英文)
The night my hypervisor learned to manage VMs (2 guests, 3 real bugs)
## 标题(B 站中文)
一晚给 hypervisor 加 VM 管理器:双 VM 同跑 + 三个 gdb 实锤的坑

## 描述
From-scratch ARM64 Type-1 hypervisor, stop 2: the VM becomes a real
object with an embedded per-VM stage-2 address space, a manager shell
(vmls/vmcreate/vmrun/vmkill), and two same-source guest images running
in non-overlapping physical windows. Three debug war stories, all
verified under gdb. Open source: github.com/eshine-os/eshine-os
Apache-2.0 · built nightly, 1-2h/day, human + AI pair programming.

## 分镜脚本

| # | 时长 | 画面 | 口播要点 |
|---|---|---|---|
| 1 | 0:00-0:25 | 黑屏淡入:终端 `vmls` 两槽 INIT | 开场:上集我们让第一个 guest 跑通;但那是硬编码。hypervisor 该管 VM,不是跑 guest |
| 2 | 0:25-1:00 | vmcreate 0 → vmrun 0 录屏 | 管理器命令面:创建/运行/杀。VM 是对象,生命周期表驱动,表外一律 panic |
| 3 | 1:00-1:50 | 两镜像对比录屏(vm0>/vm1> banner) | 同源双镜像:-DGUEST_ID 编出两个身份。关键:同 IPA 不同 PA,窗口不重叠——隔离是页表的物理事实 |
| 4 | 1:50-2:50 | 坑一:栈溢出现场(旧代码 + ESR 0x82000005) | 20KB 结构体 vs 16KB 栈:砸穿栈底清零页表,guest 首取指即挂,宿主却"正常"——栈溢出的形态是改坏别人家数据 |
| 5 | 2:50-3:50 | 坑二:gdb dump:pgd[0]=0x4020b003 指向自身 | 描述符掩码砍低 12 位:表不对齐 → 指针折回 pgd 页首。第一站的静态表各带 aligned(4096) 才是它能跑的真相 |
| 6 | 3:50-4:40 | 坑三:vm1509949441: VM_DEAD 输出特写 | ERET 后 callee-saved 全是 guest 的:宿主把 id 打印成 ESR 值。修法:host_ctx 整块保存恢复 |
| 7 | 4:40-5:20 | make verify 14 步连跑两次 | 门禁工程:双回显判据 + 发送时刻下界 + 吃掉尾随 prompt。测试器也会撒谎 |
| 8 | 5:20-5:40 | 仓库页 + 官网 | Apache-2.0,AI 署名 git log 可查;下集:完整 vCPU 上下文切换 |

## 录屏清单
1. make run:vmls/vmcreate/vmrun 全流程(含 vm1 身份)
2. gdb 复现坑二(可选进阶:现场演示掩码折叠)
3. make verify 连续两次 14/14
4. GitHub 仓库首页 + commit 历史(AI 署名特写)
5. www.eshine-os.org

## 标签
YouTube: hypervisor, ARM64, virtualization, operating system, from scratch, QEMU, embedded
B 站: 虚拟化 hypervisor ARM 操作系统 底层 开源 系统编程 从零开始
