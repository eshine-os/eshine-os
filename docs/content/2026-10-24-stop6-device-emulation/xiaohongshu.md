<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 小红书:设备仿真站——把真硬件从 guest 脚下换掉

## 正文(≤800 字)

第六站:设备仿真。guest 驱动一行不改,宿主把真 UART 换成仿真设备 🔧
做法:撤掉 stage-2 映射 → 每次访存都是陷阱 → 宿主按寄存器语义应答。
**这就是 KVM/QEMU 天天在干的事,我们从零干了一遍。**

🔥 名场面 1:13,748 连环陷阱环
仿真器写好了,guest 一字不出。
-d int 一看:同一条 ldr 被陷阱 13,748 次。
原因:HVC 陷阱后 ELR 指向下一条指令;
**data abort 陷阱后 ELR 指向出错指令本身**。
不 pc+=4 代它退休,ERET 回去就原地重执行→再 fault→死循环。
同一根 ELR,两副面孔。每一环仿真都"对",环本身就是错的。

🔥 名场面 2:x1 的赎金(三站前埋的雷)
修完还卡。gdb 两层取证:帧里 x1=0x10(宿主回填正确),
**活体 x1=0x400010e0(gprs[0] 的值)**。
eret 尾"x0 最后加载"借 x1 当中转,每圈把 gprs[0] 灌回 x1。
超调用只回 x0,三站无害;MMIO 回填任意寄存器,第一发就爆。
寄存器腾挪的账单,可能三站之后才寄到 📬

🔥 名场面 3:HPFAR 公式探针定案
IPA 重建我错了两次(全信 FAR ✗ / 只信 HPFAR ✗)。
最后拿 guest 的 fault 命令当活探针:
**QEMU 的 HPFAR=IPA>>8,页内偏移取 FAR[11:0]**。
不抄记忆,先探针——学费交得心服口服。

战果:HVC IO 退役(超调用只剩控制类)、selftest 升级
"100 次 MMIO 陷阱"、门禁 24 步双绿 ✅
下一站:把轮询也送走——UART 虚拟中断注入。

每晚 1-2h 与 AI 从零造 Type-1,devlog 06 全记录。

## 配图清单

1. 封面:`[vm0] eshine-guest VM0 @EL1 (vuart @trap)` 特写
2. -d int 连环陷阱截图(13,748 现象)
3. gdb 帧值 vs 活体值对照截图(x1 两副面孔)
4. HPFAR=0x300000 探针行特写
5. make verify 24 步 PASS ×2 截图

## 标签

#虚拟化 #hypervisor #ARM64 #操作系统 #内核开发 #嵌入式 #QEMU
#AI编程 #从零开始 #开源项目 #eshine-os
