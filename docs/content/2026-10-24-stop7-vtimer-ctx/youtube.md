<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# YouTube / B 站分镜:vtimer ctx 站(两个 VM 同时睡,谁先醒?)

## 标题候选

- 中文(B 站):从零写 Type-1 虚拟化 #7:两个 VM 同时睡,谁先醒?
  (per-VM vtimer 上下文 + 多等待者)
- EN:From-scratch Type-1 Hypervisor #7: Two Guests, One Timer —
  per-VM vtimer contexts

## 描述

第七站:per-VM vtimer 上下文切换(切入还/切出存,cortex-a57 无
CNTV 陷阱位只能切换对齐,参考实现同构)+ 每拍到期扫描(物理恒为当前客,
别家扫描兜住)+ 醒序门禁判据。战况:表序找人复发(27 被领给 cval
未到的先武者=假唤醒,醒序反了),改按 cval 到期归属;判据数据源跟
所有权走。门禁 27 步双绿。代码:github.com/eshine-os/eshine-os ·
www.eshine-os.org。每晚 1-2h,AI 结对,从零造 Type-1。

## 标签

B 站:虚拟化 hypervisor ARM64 操作系统 内核 嵌入式 QEMU AI编程
从零开始 eshine-os
YouTube:hypervisor arm64 virtualization osdev type-1 qemu timer
from scratch

## 分镜脚本(约 10 分钟)

| # | 时段 | 画面 | 口播/字幕要点 |
|---|---|---|---|
| 1 | 0:00-0:50 | 录屏:双睡醒序现场 | 钩子:两 VM 各自睡,谁先醒?单核单定时器,怎么做到互不干扰 |
| 2 | 0:50-2:30 | 代码:切换对(泵) | a57 无 CNTV 陷阱位(KVM 新核可陷阱,我们不能)→切换对齐;物理恒为当前客的不变量 |
| 3 | 2:30-4:00 | 代码:每拍扫描 | 别家错过扫描兜住;"同一张时刻表"论(WOKE 生效本就要等下一泵) |
| 4 | 4:00-6:00 | 录屏:首跑反例输出 | 表序找人复发:sched_waiter 把 27 领给 cval 未到的 vm0(假唤醒解剖)——按到期归属修正 |
| 5 | 6:00-7:00 | 代码:WAIT 原因判定 | 数据源跟所有权走:读本人 ctx 不读物理(他人视角陷阱) |
| 6 | 7:00-8:30 | 录屏:sleep 4/sleep 2 全链 | 正确醒序 + 双醒后双 shell;门禁 [19] 位置断言讲"分别等到会掩盖顺序错误" |
| 7 | 8:30-10:00 | make verify 27 步 + 尾板 | 偏差 #10 收口;下一站预告(UART 中断注入);仓库+站点 |

## 录屏清单

1. 双睡醒序(sleep 4/sleep 2 → [vm1] 先醒)
2. 首跑反例(若有录像;无则用文内截图)
3. make verify 27 步 ×2
