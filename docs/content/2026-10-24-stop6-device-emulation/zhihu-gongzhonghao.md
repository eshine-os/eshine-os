<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# guest 一行不改,宿主把真硬件从它脚下换掉:MMIO 设备仿真站

> 27.04 连载第 6 篇。前五站 guest 一直在"直访真硬件":UART 直通、
> 控制台走超调用。这一站做 Type-1 hypervisor 的看家本领——**trap 型
> 设备仿真**:撤掉直通,让每次访存都陷进宿主,宿主按寄存器语义
> 逐次应答。这是通向 virtio 和 Linux guest 的核心技术门。

## 先看结局

    eshine> vmrun 0        → 立即返回(异步)
    [vm0] eshine-guest VM0 @EL1 via stage-2 (vuart @trap)
                             ← 这行字是 guest"以为"自己在写真 UART
                               实际每个字符两次陷阱,走宿主仿真设备
    vm0> selftest          → 100 个 '.'(100 次 MMIO 陷阱,寄存器
                               存活性判据比 HVC 更硬)
    vm0> fault             → 窗口外访存 → fault → VM_DEAD(默认拒绝)

guest 的驱动代码 TX 路径零改动——它写的还是 PL011 那套 DR/FR 寄存器,
物理行为从"直访"变成了"陷阱+仿真",它毫无感知。**这就是设备虚拟化
的全部魔法:契约不变,履约方更换。**

## 机制:一次访存的完整旅程

stage-2 页表撤掉 UART 映射 → guest 一碰 0x09000000 就是 stage-2
translation fault(DFSC=6)→ data abort 陷阱到 EL2 → 宿主解码 ESR
(寄存器号 SRT/读写 WnR/访问宽度 SAS)→ **HPFAR 重建 IPA** → 匹配
仿真窗口(不认识的一律 fault 死,默认拒绝)→ vuart 模型按寄存器
语义应答 → 回填 guest 寄存器 → **代其退休,ERET 回 guest**。

两个"教科书不会加粗"的不变量,各吃掉我一晚:

## 战况一:ELR 的两副面孔(13,748 连环陷阱环)

修完分发器,首测一字不出。QEMU -d int 揭底:**同一条 ldr 指令被
陷阱 13,748 次**。每环的仿真都"正确",环本身就是错的。

根因:HVC/WFI 是"已完成"语义,ELR 指向下一条指令;**同步 data abort
不退休,ELR 指向 faulting 指令本身**。仿真完访存效果不 pc+=4,ERET
回去就原地重执行——再次 fault——无限循环。同一根 ELR,两副面孔,
架构手册里它们只是相邻的两小节。

## 战况二:x1 的赎金(第三站埋的雷,今天才响)

ELR 修完,guest 仍卡在 `tbnz w1,#5` 自旋。gdb 两层对照才看清:

- 帧内 gprs[1] = 0x10(宿主回填正确落帧 ✓)
- **活体 x1 = 0x400010e0**(gprs[0] 的值,字符串指针 ✗)

watchpoint 盯帧槽:保存 0x400010e0 → 回填 0x10 → 再保存 0x400010e0……
**eret 尾的"x0 最后加载"舞步借了 x1 当中转**,每圈把 gprs[0] 灌回
x1。第三站起的超调用只往 x0 回值,x1 从不是回值寄存器——三站无害。
本站 MMIO 按 SRT 回填**任意**寄存器,第一个 SRT=1 的访存(恰好是
guest 的 FR 读)立刻引爆。

> 帧契约是全局不变量,asm 尾舞步属于契约的一部分。一次"聪明"的
> 寄存器腾挪,账单可能在三站之后才寄到。

## 战况三:HPFAR 的重建公式(探针定案)

IPA 重建我先后错两次:先全信 FAR(stage-2 fault 的 FAR 页地址无效),
再只信 HPFAR(只有页粒度,页内偏移丢失,FR 读全落 DR 偏移)。
最后用 guest 的 fault 命令当活探针定案:**QEMU 的 HPFAR = IPA>>8,
页基 = HPFAR<<8,页内偏移取 FAR[11:0]**(纯 stage-2 下 VA 即 IPA)。
两次错误各自留下一段"看似有理"的实现——这就是"不抄记忆,先探针"
的学费,交得心服口服。

## 弧线:每一代等待机制,都被下一代替换

本站的 RX 走 FR 轮询 + WFI 让位,替换掉了第五站的 HVC 三态代理。
而轮询自己,在下一站被虚拟中断 vSPI 33 替换(中断驱动 getc)。
设备虚拟化这条线的叙事,就是一部等待机制的更替史——每代都为
上一代收尸,又为下一代当踏脚石。

## 数字与下一步

- 门禁 22 → **24 步双绿**(selftest 改 100 MMIO 陷阱;后台输出
  [vmN] 前缀判据;RX 唤醒链判据)
- HVC 超调用收缩为控制类(#1 exit/#2 yield/#4 host),IO 面全仿真
- 下一站预告:per-VM vtimer 上下文(两个 VM 同时睡、按各自时长醒)
  和 UART 中断注入(把轮询也送走)

代码、devlog 06、28 步门禁:github.com/eshine-os/eshine-os ·
www.eshine-os.org。每晚 1-2 小时,与 AI 结对,从零造 Type-1。

### 平台微调说明

- **知乎**:全文可发;transcript 建议换 GIF;13,748 现象图放战况一
- **公众号**:"弧线"一节前置作钩子;末尾加系列目录
- **B 站**:见 youtube.md 分镜
