<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 第十九站内容包:四块合成站(闪断追查/事件组+队列/per-task FP lazy/guest SMP)

口径(README 索引):
- **用户令"下一站全部需要开发"**:A [41] 闪断追查 / B 事件组+消息
  队列 / C per-task FP lazy / D guest SMP(vm2 第二 vCPU,-smp 3)。
- **D 块 tick 坍缩三层解剖**:①TLB 风暴根因——vcpu_run/exit 每次
  `tlbi vmalls12e1is` 在 QEMU TCG 是三线程同步全量 flush,睡眠型
  guest 每 park→wake 周期吃两发,100Hz tick 下模拟速率塌 ~1/1000
  (同一构建实测 0.4s↔180s 双速);修复=per-pCPU 表值对比,换表才
  flush。②专泵缺 26——到期扫描唯一周期驱动源是 26(宿主相),
  睡客蒸发后没有 26 就没有再发现途径;补 26|27 双配+本地物理
  定时器。③收尸窗误杀在跑客——第三支写 `CNTV_CTL=0` 是 per-pCPU
  一刀,可以落在跑动客身上(WFI 已入、簿记未竟),出栈存得
  ctl=0 就按 RX 归类停泊——一个 tick 即永眠;改只降线保挂。
- **残余(诚实)**:vm2 靴仍冻死在 SMP 交接窗(~0.2s 内核时),
  62 门 [1]-[30] 过、[31] 区 vm2-Linux 控制台判据超时,双绿未达,
  62→67 重编暂缓;判据行实况纠偏(设计所引 "Bringing up 1
  secondary CPU(s)" 不存在,真行为 "smp: Brought up 1 node, 2
  CPUs")。

文件:
- zhihu-gongzhonghao.md:知乎/公众号长文(主线:三核一钟下 Linux
  boot 停在 0.2s——TLB 风暴+26+收尸)
- xiaohongshu.md:小红书短文
- x-thread.md:X 8 推
- youtube.md:YouTube/B 站分镜
