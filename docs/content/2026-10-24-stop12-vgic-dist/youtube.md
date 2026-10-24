<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# YouTube / B 站分镜:vgic GICD/GICR 仿真站(distributor 成为权威)

## 标题候选

- 中文(B 站):从零写 Type-1 虚拟化 #12:给 guest 一个真 GIC——
  仿真 distributor/redistributor,注入按 guest 使能位门控(三战况)
- EN:From-scratch Type-1 Hypervisor #12: Emulating the GIC
  Distributor — the guest's enables become the injection authority

## 描述

第十二站:中断面 OS 化。仿真 vGICD(0x08000000/64KB)+vGICR
(0x080A0000/RD+SGI 双帧),镜像 QEMU virt 真布局;寄存器分层
(探测/使能/唤醒真语义,状态存储回读,未知 RAZ/WI 设备级默认拒绝);
泵注入三通道按 guest 使能位门控(distributor 权威);guest 真启动式
bring-up(PIDR2/CTLR/WAKER/ISENABLER/回读验证);intc 节点请回树。
三战况:stage-2 单页铁律拦真布局;寄存器族解码 bug 被 guest 回读验证
当场抓获(判据即测试);SAS 编码值当字节数传(自证无误≠实测无误,
探针实锤)。门禁 34→36 步双绿。代码:github.com/eshine-os/eshine-os ·
www.eshine-os.org。每晚 1-2h,AI 结对,从零造 Type-1。

## 标签

B 站:虚拟化 hypervisor ARM64 操作系统 内核 嵌入式 QEMU AI编程
GIC 中断 从零开始 eshine-os
YouTube:hypervisor arm64 virtualization osdev type-1 qemu gicv3
interrupt emulation from scratch

## 分镜脚本(约 13 分钟)

| # | 时段 | 画面 | 口播/字幕要点 |
|---|---|---|---|
| 1 | 0:00-0:50 | 录屏:gicd v3 ok / gic on: 27 33 35 | 钩子:guest 中断的"户口管理机构"上岗;预告门控语义变化 |
| 2 | 0:50-3:00 | 对比图+代码:泵门控前后 | distributor 成为权威:宿主从"替 guest 做决定"到"执行 guest 的决定";没使能就不注入不代完成 WFI |
| 3 | 3:00-4:40 | 代码:guest gic.h bring-up | 真 OS 姿势:PIDR2 探测→CTLR→WAKER→ISENABLER 逐 INTID→回读验证;~10 次陷阱全链 |
| 4 | 4:40-5:50 | 代码:vgic.c 分层 | 寄存器三档:真语义(写即完成 RWP 恒 0,Linux 轮询零等待)/存储回读/RAZ-WI;offset 手册复核(站八纪律) |
| 5 | 5:50-7:10 | 战况一:stage2 单页校验 diff | 真布局跨页(64KB/128KB)被单页铁律拦;EMULATED 放行多页——窗口大小只是陷阱区;直映射仍单页 |
| 6 | 7:10-9:10 | 重点:族解码表+回读验证 | 战况二:(off-0x080)>>2 把 ISENABLER0 算 n=32 越界静默 WI;guest 回读当场抓;"判据即测试:把验证放进系统,bug 会自己走进来" |
| 7 | 9:10-11:00 | 战况三:探针输出 | SAS 编码值当字节数传:4 字节读变半字,0x08000000 读回 0;纸面自证三遍全对,探针两行实锤;"自证无误≠实测无误" |
| 8 | 11:00-12:00 | 录屏:树 diff + 全流程 | intc 节点请回树(站 11"没有的设备不进树"的兑现);its/psci 仍缺席 |
| 9 | 12:00-13:00 | make verify 36 步 + 尾板 | 门禁判据两新增;下一站预告(PL011 完整化/Linux bring-up);仓库+站点 |

## 录屏清单

1. gic 全流程自报 + echo/sleep 经门控照常(分镜 1/9)
2. 门控前后语义对比图(分镜 2,静态图)
3. 寄存器族解码表(分镜 6,静态图)
4. 探针打点输出 W/R 对照(分镜 7,静态图)
5. make verify 36 步 ×2(分镜 9)
