<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# YouTube / B 站分镜:调度站(双 VM 真并行 + 注释谋杀案)

## 标题候选

- 中文(B 站):从零写 Type-1 虚拟化 #5:两个 VM 抢一个键盘,以及
  一段注释如何谋杀整个中断系统
- EN:From-scratch Type-1 Hypervisor #5: Scheduling — two VMs, one
  keyboard, and the comment that murdered the tick

## 描述

第五站:调度。shell 即调度泵(宿主权威,命令=原子段),vmrun 异步化,
1 tick 量子轮转;抢占在 trap 出口集中触发(tick 置标志 + IRQ 出口
帧转写 + HVC 出口查量子);键盘三态 HVC 仲裁(-2 → WFI 让位);
WFI 升级为真让出 CPU——公平即让位协议。战况:十坑全在交互面,
本站最狠一枪是多行注释吞掉 if 语句体导致 EOI 空洞、tick 单响即死
(git-bisect 定文件,objdump 定罪);另有一桩 mrs 计数不退的 QEMU
悬案如实记录。22 步门禁三连绿,spin 3G 跨 7 量子无损续跑。

时间轴 / 章节见分镜。代码:github.com/eshine-os/eshine-os ·
站点 www.eshine-os.org。每晚 1-2h,AI 结对,从零造 Type-1。

## 标签

B 站:虚拟化 hypervisor ARM64 操作系统 内核 嵌入式 QEMU AI编程
开源 从零开始 eshine-os
YouTube:hypervisor, arm64, virtualization, osdev, qemu, type-1,
from scratch, AI pair programming

## 分镜脚本(约 12 分钟)

| # | 时段 | 画面 | 口播/字幕要点 |
|---|---|---|---|
| 1 | 0:00-0:50 | 录屏:vmrun 秒回 shell,vmconsole 轮转,双 VM token 交错 | 开场钩子:单核上两个 VM 凭什么同时活?本站答案+本站翻车预告(十个坑全在交互面) |
| 2 | 0:50-2:20 | ARCHITECTURE.md 调度节 + 泵代码滚动 | 设计:shell=最高优先级任务;泵只在等输入处跑;命令=原子段;vmrun 异步。类比:宿主是"永远优先的服务员" |
| 3 | 2:20-4:00 | 录屏:spin → spun 3G(可 4 倍速)+ vmstat 前后差特写 | 帧迁移判据:任意指令边界切走再无损放回;判据为什么用前后差不用累计值——"锚跨量子次数,不锚时基" |
| 4 | 4:00-5:00 | 录屏:仲裁前名场面(宿主命令落 guest)/或文中截图 | 键盘战争:直读 DR 互抢;共享输入面必须有唯一仲裁者;三态 HVC #3(-2 → WFI 让位)+ HVC #4 |
| 5 | 5:00-7:30 | **重点**:左边源码段"看起来完好",右边 objdump diff 一行行对;EOI 消失处红框 + 音效 | tick 之死:注释吞 if 体 → EOI 条件化 → 电平线不撤 → GIC 静默。教训三条:单语句 if+多行注释=无声杀手;源码对≠汇编对;bisect 定文件 objdump 定罪 |
| 6 | 7:30-8:30 | 探针输出:计数 62.5M/s 前进、循环不退的输出 | mrs 悬案:如实记录不追;模拟器不开膛的证据哲学 |
| 7 | 8:30-9:30 | 录屏:sleep 后 vm1 活、slept 后台醒 | 睡与醒:WFI=真让出 CPU;前台睡必须还键盘(让位协议闭环);WOKE 归等待者不归当值者("按状态找人不按当值找人") |
| 8 | 9:30-10:30 | make verify 全程 ×2 快放,[13] 步特写 | 门禁 22 步:判据逐条念一遍;连续两轮才真绿 |
| 9 | 10:30-11:30 | git log --oneline + 偏差记录 #11/#12 | 形态取舍:参考实现全量线程化 vs shell 泵(取其形不取其层);vuart 仿真 vs 三态代理(设备站再对齐) |
| 10 | 11:30-12:00 | 尾板:仓库+站点+下一站候选 | 每晚 1-2h AI 结对;下一站 vtimer_ctx 或 vuart 设备模型;订阅催更 |

## 录屏清单(一次录齐)

1. vmrun 0/1 异步 + vmconsole 轮转 + token 交错(分镜 1)
2. spin 全程 + vmstat 前后差(分镜 3)
3. sleep 公平链(分镜 7)
4. make verify 两轮 PASS(分镜 8)
5. (如可复现)仲裁前互抢输出(分镜 4);否则用文章截图替代
6. objdump 对比需提前准备:源码段截图 + `objdump -d` 定位到两分支,
   EOI 缺失处高亮(分镜 5 核心素材)
