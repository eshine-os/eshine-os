<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# YouTube / B 站分镜:设备仿真站(把真硬件从 guest 脚下换掉)

## 标题候选

- 中文(B 站):从零写 Type-1 虚拟化 #6:guest 一行不改,宿主把真
  硬件从它脚下换掉(device emulation)
- EN:From-scratch Type-1 Hypervisor #6: Trap-based Device Emulation —
  same driver, no real hardware

## 描述

第六站:trap 型设备仿真。stage-2 撤掉 UART 映射,DR/FR 访问全部
data abort 陷阱化,宿主 per-VM vuart 按寄存器语义应答(IPA 重建/
窗口默认拒绝/SRT 回填/代退休)。战况:ELR 两副面孔(13,748 连环
陷阱环)、x1 的赎金(第三站暗雷,帧对活体错)、HPFAR=IPA>>8 探针
定案。HVC IO 退役,selftest 升级 100 MMIO 陷阱,门禁 24 步双绿。
代码:github.com/eshine-os/eshine-os · www.eshine-os.org。
每晚 1-2h,AI 结对,从零造 Type-1。

## 标签

B 站:虚拟化 hypervisor ARM64 操作系统 内核 嵌入式 QEMU AI编程
开源 从零开始 eshine-os
YouTube:hypervisor arm64 virtualization osdev type-1 qemu device
emulation from scratch

## 分镜脚本(约 12 分钟)

| # | 时段 | 画面 | 口播/字幕要点 |
|---|---|---|---|
| 1 | 0:00-0:45 | 录屏:vmrun 0 → [vm0] banner | 钩子:这行字 guest 以为自己在写真 UART——实际每个字符两次陷阱。设备虚拟化的契约不变、履约方更换 |
| 2 | 0:45-2:30 | ARCHITECTURE 设备仿真节滚动 | 机制六步:撤映射→abort 陷阱→ESR 解码→HPFAR→窗口(默认拒绝)→仿真回填;guest TX 代码零改动实拍 |
| 3 | 2:30-4:30 | 重点:-d int 截图(13,748) | 战况一:ELR 两副面孔——HVC/WFI 指下条 vs abort 指 faulting;仿真完不 pc+=4 = 无穷环;每一环都"对"而环本身错 |
| 4 | 4:30-6:30 | gdb 双屏:帧值 0x10 vs 活体 0x400010e0 | 战况二:x1 的赎金——eret 尾 x0 中转、三站暗雷、SRT=1 首爆;帧值≠活体值,watchpoint 盯帧槽 |
| 5 | 6:30-8:00 | HPFAR 探针行特写 | 战况三:错两次(FAR 全信/HPFAR 只信)→探针定案 HPFAR=IPA>>8;不抄记忆先探针 |
| 6 | 8:00-9:00 | 录屏:selftest 100 点 + fault 隔离 | selftest 升级 100 MMIO 陷阱;窗口外默认拒绝 → VM_DEAD 宿主活 |
| 7 | 9:00-10:00 | 录屏:vmconsole 前后台切换 + [vmN] 前缀交错 | RX 三态→轮询的演进简述;后台行缓冲前缀;前台裸透传 |
| 8 | 10:00-11:00 | make verify 24 步全绿 ×2 快放 | 门禁判据升级:后台前缀/RX 唤醒链;selftest 文案换代 |
| 9 | 11:00-12:00 | 尾板 | 弧线:每代等待机制被下代替换(HVC 风暴→轮询→下站虚拟中断);仓库+站点;订阅 |

## 录屏清单

1. vmrun 0 异步 + [vm0] banner + vmconsole 交互(分镜 1/7)
2. selftest 100 点 + fault(分镜 6)
3. make verify 双轮(分镜 8)
4. gdb 截图素材:帧/活体对照 + watchpoint 交替(分镜 4,静态图即可)
5. -d int 日志截图(分镜 3,静态图)
