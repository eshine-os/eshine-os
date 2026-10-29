<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 两个用户报障,一天闭环的一站

从零写 hypervisor+RTOS 的第 24 站,用户两条报障:
🔇 "进 rtos shell 一直刷心跳" → 心跳改成只在壳
   自主睡时说话,加了 hb 命令按需打印
🐌 "linux shell 非常卡顿" → 真根因不是慢,
   是输入蒸发:泊位后唤醒丢失,0ms 或 30s+ 两态

诊断三课:
1. 我的第一假设(tlb flush 风暴)被自己的实验判负——
   900 字符 echo 0.00s 反证了陷阱路径零税
2. 所有"慢命令"数字细看全是超时假象,不是执行时间
3. vmtrail 事件环的 cval/now 配对,唤醒延迟直接实算:33µs

修完探针:深闲置后输入 0.00s,大 dump 后输入 0.00s。
门禁 79 门,判据就地强化,连续双绿 ✅
Apache-2.0,每天 1-2 小时,从零打造。
#从零打造 #RTOS #hypervisor #ARM64 #响应性
