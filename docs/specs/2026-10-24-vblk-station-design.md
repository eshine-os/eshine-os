<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 27.04 第九站:块设备仿真站(vblk:门铃 + 完成中断)设计 spec

- 日期:2026-10-24 · 版本:27.04-dev · 前置:eac6cbc(第八站 UART IRQ,28 步门禁双绿)
- 用户批准:四决策全选推荐(站点=vblk 块设备/数据面=门铃+guest 内存命令块/
  完成=异步 vSPI 34/持久化=跨 VM 生命周期保留)

## 0. 目标与真绿定义

第二个仿真设备:RAM 磁盘 vblk。**内存即传输面,寄存器即门铃**——
virtqueue 的种子;完成中断把第八站的"单发中断面"升级为**完整设备
生命周期**(请求→服务→完成→guest ISR);RAM 磁盘跨 VM 销毁/重建
保留——"vmkill 后数据还在"的宿主资源世界观。门禁 28 → 31 步 ×2。

## 1. 设备模型(drivers/vblk.c + eshine_vm 内嵌 regs)

仿真窗口:IPA 0x0a000000/4KB(devices[1],EMULATED)。寄存器(u32):

| 偏移 | 读 | 写 |
|---|---|---|
| 0x00 CMD | 0 | 门铃:[7:0]=op(1=read,2=write),[23:8]=sector——写入即触发 |
| 0x04 ADDR | 0 | 数据缓冲 IPA(guest 自有 RAM,512B 对齐免拆) |
| 0x08 STATUS | bit0 done(读清)bit1 err | 忽略 |
| 0x0c CAP | 128(扇区数) | 忽略 |
| 0x10 IMSC | 回读 | bit0 完成中断使能 |

磁盘:`static u8 blk_disk[2][128*512]`(每 VM 槽位 64KB,**静态数组
不经 vm_create 复位——销毁重建数据保留是结构保证,非代码路径**)。

**服务在 CMD 陷阱内同步完成**(RAM 介质无真实延迟),**异步的只有
完成中断**:ris 置位 → 泵见 irq_ready 注 **LR2 vSPI 34**(通道编号:
LR0=vtimer HW/LR1=vuart/LR2=vblk,a57 四槽刚好)→ guest ISR 读
STATUS 清挂起 → EOI。请求参数错(越界扇区/缓冲不落 RAM 区)置
err,照常完成中断——设备级默认拒绝,不杀 guest。

## 2. 宿主首次直访 guest 物理内存(静态翻译)

数据面零陷阱:guest 把 512B 缓冲放在自己 RAM(它是唯一写者),
宿主按 `vm->mem[]` 静态映射翻译 IPA→PA 后 memcpy。新助手
`vm_ipa_to_pa(vm, ipa, size)`:扫 mem[] 区,含全尺寸返回 PA,
不含返回 0(vblk 置 err)。宿主 EL2 恒等映射,PA 即宿主虚址。

## 3. 设备表化(多仿真设备,表驱动分发)

`struct vm_device` 增 `dev` 字段(VM_DEV_VUART/VM_DEV_VBLK);
mmio_dispatch 窗口命中后按 dev 分发(vuart 保持 fg 语义;vblk 无
前台概念)。多设备后泵注入条件并列:`uart_irq_ready || blk_irq_ready`
各自注各自 LR。**等待原因泛化**:VCPU_WAIT_RX 语义扩为"设备中断
等待"(RX/块设备等非 TIMER 源),泵内再唤醒网同步泛化
(reason!=TIMER 且任一设备 irq_ready 即再唤醒)。

## 4. guest 侧(guest/blk.h + main.c)

- 初始化:写 IMSC=1 → 自报 `blk irq on`(判据 needle)
- ISR(向量表分支出 34):读 STATUS(清挂起)→ 计数器++ → EOI
- 命令:`dwrite`(模式串写 sector 7 → 等完成中断 → `blk wrote`)
  / `dread`(读 sector 7 → `blk read: <内容>`)
- 访存器全部经 volatile asm 访问器(ISV=1 契约,第八站教训)

## 5. 门禁 28 → 31(只增不减)

- 新增 [10]:`[vm0] blk irq on`(IMSC 使能判据)
- 新增(前台段):dwrite/dread 往返——`blk wrote` +
  `blk read: ESHINE-BLK`(完成中断全链 + 数据完整性)
- 新增(vm0 退场后):vmcreate 0 重建 + dread → **旧数据仍在**
  (销毁重建持久化判据——本站最强演示点)

## 6. 基准对齐预登记(#14)

参考实现:virtio 完整设备模型(队列协商/DMA/多队列)→ 新仓:vblk
门铃+共享内存命令块的最小同构种子(同步服务+异步完成中断,无
队列深度/无协商/单扇区同步)——virtio 站的直系前传,形态差登记。
