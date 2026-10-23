<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# Devlog 05 — 调度站（时间片抢占轮转 + RX 仲裁 + WAIT 公平）

日期:2026-10-23 · 版本:27.04-dev · 门禁:22 步真绿 ×2 连过

## 里程碑

双 VM 首次真并行；shell 与调度器共存（宿主权威）；WAIT 语义从
"宿主陪睡"升级为"泵中让位"：

    eshine> vmrun 0        → 立即回 shell（异步，跑动交给泵）
    eshine> vmconsole 0    → console -> vm0（前台，RX 归 vm0）
    vm0> spin              → spinning... → spun 3G iters (preempted ok)
                             （3G 迭代 ~8s，跨 7 个量子被切走再无损续跑；
                               preempts 前后差即门禁判据）
    vm0> sleep             → vm0 让位，vm1/宿主照常活（公平）
                             → slept (vtimer irq)（后台轮转中醒）
    vm0> host              → console -> host（前台主动交还 RX）

抢占链：① guest 态 tick 递减量子，lower-EL IRQ 出口查标志 →
preempt_save_frame 把 excp_stack 帧逐项转写进陷阱帧 → vcpu_exit
弃帧（与 HVC 退出同构）；② getc 轮询风暴的 tick 落宿主段，宿主段
代计入量子，HVC 出口 resume_or_preempt 查尽即切。

## 战况：抢占一次转对，交互面连环塌方

帧转写本身一次写对（第四站的帧契约红利）；本站的仗全打在四个子系统的
**交互面**上——调度状态、RX 所有权、HVC 风暴、WAIT 链。十个坑，
每个都是独立的失效模式：

1. **slice_left 未初始化**：泵入口不重置配额 → 首个 guest tick
   立即 PREEMPT，VM 跑不进第二量子。修复：pump 选中即置
   `slice_left = SCHED_QUANTUM_TICKS`。
2. **preempt_save_frame 偏移错位**：x6 起的成组装载写成
   `frame_offset-24` 而非 `gprs[k]=k*8` 契约 → 寄存器错乱（guest
   x0=0x90 落在 UART 代码上直接 fault）。objdump 反汇编逐对比对
   定位。教训：**asm 偏移契约改一次、验一次，objdump 是终局武器**。
3. **WAIT 状态机被覆盖**：guest wfi 循环每轮重写 wait_state=1，
   把"到点唤醒"的 WOKE=2 盖掉 → 永不醒。修复：首入守卫
   （`if (wait_state == 0)`）——wfi 循环只许首入，WOKE 只许泵消费。
4. **WOKE 串台**：27 到点时 cur_vcpu 可能是别家在跑 VM，把 WOKE
   记在 cur_vcpu 头上 → 等待者永不醒、被跑者莫名醒。修复：
   `sched_waiter()` 全表扫 WAIT 态——**事件的受益者与事件的当值者
   不是同一人时，按状态找人不按当值找人**。
5. **前台 VM 进 WAIT 持 RX**：泵被 owner 挡住别家、宿主读不到回收
   命令 → 系统死等 WOKE。修复：泵在 WAIT 路径归还 owner。这是
   "让位协议"必须闭环的第一课：**进了等待就必须放下独占资源**。
6. **RX 仲裁推翻重设**：设计时以为双 guest 直读 DR + 让位即可；
   实测互相抢输入，宿主命令 vmstat 直接落进 guest 执行。架构修正：
   HVC #3 三态代理（cur==owner 代读真 UART 返回字符/-1；否则恒
   -2；guest 见 -2 即 WFI 让位，TWI 陷阱化下泵去跑别家），TX 保持
   直访。**共享输入面必须有一个仲裁者，宿主就是仲裁者**。
7. **注释陷阱（本站最狠一枪）**：一段多行注释的补丁把
   `if (cur_vcpu && slice_left > 0)` 的语句体吞进注释 → if 变
   空语句 → 顺带把 `gicv3_eoi_dir(intid)` 条件化绑进 if → 宿主
   idle 段（cur_vcpu=NULL）跳过 EOI → 26 电平线不撤 → **tick 在
   首次投递后死亡**。git-bisect 收敛到 exceptions.c 后仍看不出
   源码问题（注释看起来完好），objdump diff 一眼定案：NULL 路径
   直接 `ret`。教训：**单语句 if + 多行注释补丁 = 无声杀手；
   源码"看起来对"不等于汇编对**。
8. **mrs 时基循环弃用**：时基版 spin（mrs cntvct_el0 计时 3s）在
   PREEMPT 边界下计数正确前进（62.5M/s = CNTFRQ 实测）而
   cmp/b.lo 永不落出；帧转写被探针证明无损（pc/sp/关键寄存器
   全稳，x0 前进速率精确），疑 QEMU mrs helper×TB 缓存交互。
   判决：不在调度站追 QEMU 内部——改纯迭代 3G（TCG 实测
   ~0.4G 迭代/s ≈ 8s，跨 ~8 个量子），门禁改"preempts 前后差"。
   **移植性教训：门禁判据不锚定时基，锚定跨量子次数**。
9. **门禁工具自身两枪**：token_echo 第二次 read_until(\n) 匹配到
   缓冲里旧数据的第一个换行 → 改计数循环（token 出现 ≥2 次）；
   socket chardev 的 CRLF 转换让以 \n 结尾的 needle 永不匹配 →
   needle 一律不带行尾。
10. **门禁解析竞态**：read_until 匹配到 "preempts: " 即返回，数字
    还在路上 → 正则扑空。修复：匹配到锚点后再等一行完整换行。
    **read_until 返回 ≠ 该行可解析**。

## 判据设计的迁移

[13] 从"preempts >= 4（累计值）"改为"spin 前后 preempts 差 >= 2"：
累计值会被 spin 之前的前台风暴污染，差值才能证明"**这个负载在这
段时间被切走且无损**"。7 个量子的实测转写无损，是帧契约（第三站）
+ 出口集中切换（本站）两条路线的会师验证。

## 已知限制（登记）

- 单等待者假设（vtimer_ctx 留下一站，偏差 #10/#11/#12 已登记）
- getc 前台轮询是 HVC 风暴（每字符一 trap），设备站做 vuart 仿真
- spin 的 mrs 时基版疑点（QEMU helper×TB）未追，记录在案
