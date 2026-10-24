<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# eshine-os Architecture（Week-1 骨架）

## 启动流（当前）

    QEMU virt, virtualization=on, gic-version=3
      -kernel eshine.elf (ELF 入口 0x40200000)
        |
        v  EL2
    _start (hypervisor/arch/arm64/start.S)
      校验 CurrentEL == EL2，否则 WFI 停车
      -> DAIF 关中断（本周轮询模型）
      -> 清 BSS
      -> 装载 VBAR_EL2（.vectors，2KB 对齐）
      -> 设栈（.stack 16KB）
      -> main() -> console_init -> banner -> shell_run

## VM 状态机（生命周期先行，spec §4.1）

    VM_INIT -> VM_CREATED -> VM_RUNNING <-> VM_PAUSED
                  |             |
                  +-> VM_TEARDOWN <-+
                          |
                       VM_DEAD -> VM_INIT（27.04 第二站：槽位复用）

- 表驱动 `allowed[current][next]`，表外转换一律 panic（默认拒绝）
- 第二站新增两条：CREATED→TEARDOWN（vmkill 未跑先销毁）、DEAD→INIT（复用）
- 命令层用户错误礼貌拒绝打消息；`vm_transition` 保持 panic 护栏

## VM 抽象（27.04 第二站：双 VM + 生命周期管理）

    vm_table[2] 静态全量内嵌（无堆）：
      struct eshine_vm = state + vmid + vcpus[1] + mem[4] + devices[4] + vm_space
    预定义配置（vm_configs 思路固化于 vm.c）：
      VM0: RAM IPA 0x40000000/16MB -> PA 0x50000000   entry 0x40000000
      VM1: RAM IPA 0x40000000/16MB -> PA 0x51000000   entry 0x40000000
      共享设备页：UART IPA 0x09000000 -> PA 0x09000000（顺序运行，无争用）
    PA 窗口不重叠 = 隔离是映射层的物理事实（VM0 无法经自身映射摸到 VM1）

命令面：`vmls` / `vmcreate <id>` / `vmrun <id>` / `vmkill <id>`（单数字 id）。

## Stage-2（per-VM，27.04 第二站重构）

    struct vm_space：每表独立 4KB 对齐页（DSC_TABLE 掩码砍低 12 位，
    任何表不落 4KB 边界都会被指错 — gdb 实证后固化）
    stage2_build(sp, vmid, mem[], n, dev[], n)：
      RAM 区 -> l2_ram 2MB block 8 项（16MB）
      设备页 -> l2_dev[0x48] -> l3_dev 4KB page，强制 XN
      vttbr = pgd | vmid<<48（VMID 8-bit）
    VTCR/描述符规则与第一站一致：AF bit10 铁律、RAM=0xF、设备=0x1+XN
    拓扑不变：pgd[0]->设备段 L2、pgd[1]->RAM 段 L2；boot 单套静态表已删

## vCPU 出入口（27.04 第二站补完，第三站并入陷阱帧节）

    host_ctx = x19-x28 + sp + lr（整块宿主现场）
    ERET 后 guest 覆盖一切可覆盖寄存器 — ret 回宿主前必须全量还回，
    否则调用方 callee-saved 变成 EL2 异常链残留（实测 vmrun 曾把 id
    打印成 HVC 的 ESR 值）
    第三站起 guest 侧现场由陷阱帧全量保存恢复 — 第二站记账的
    "编译器巧合"债务已清偿（见下节与 devlog 03）

## vCPU 陷阱帧（27.04 第三站：帧即真相）

    struct vcpu = gprs[31](x0-x30) + sp + pc + pstate   ← 陷阱帧（asm 契约偏移）
                 + host_ctx(x19-x28 + sp + lr)          ← 宿主现场
                 + exit_reason(EXIT/YIELD/FAULT/WAIT/PREEMPT)
                 + wait_state/wait_reason/slice_left(第五/六站尾部扩展,
                   asm 转写只到 pstate,不触及)
    el1_trap:    向量槽入口,停 x0 于 excp_stack → 全 GPR+SP_EL1/ELR/SPSR 入帧
                 → SP_EL2 归位栈顶 → el1_sync(x0=vcpu)
    trap_resume: 帧加载尾部(x0 最后回填)→ eret
    vcpu_enter_guest: 存 host_ctx → 复用帧加载 → eret
    vcpu_return_host: 还 host_ctx → ret 回 vcpu_run 调用点

- 新跑 = vm_create 初始化帧；yield 后帧 = 断点；续跑同一条进入路径
- **EL2 handler 从此可自由用任何寄存器** — 第二站"编译器巧合"债清零
- SP_EL2 每次陷阱入口归位栈顶：handler 链不退栈、eret 不还栈，
  不归位则每次净泄一截、累计碾穿栈底（实测第 79 发陷阱踩 cur_vcpu）
- guest 侧 selftest：sum 跨 100 次 HVC 存活（x19/x30 实证落点），
  帧破则 4950 校验红

## 异常级分工（27.04 第四站后）

| 异常 | 处理 | 状态 |
|---|---|---|
| current-EL sync | 打印 ESR_EL2/FAR_EL2 -> WFI（宿主自身异常仍 panic，默认拒绝） | 已实 |
| current-EL irq  | el2_trap → el2_irq：26=宿主 tick（重装后 EOIR+DIR）；27=vtimer（注入 LR 后只降优先级）；spurious 不动 EOIR | 已实 |
| lower-EL sync   | el1_trap 保存帧 → el1_sync：HVC #1/#2/#4（控制类；#0/#3 第六站退役，未知 imm 默认拒绝）+ data abort（EC=0x24 → mmio_dispatch 仿真窗口分发）+ WFI 陷阱（EC=0x01 → VCPU_WAIT）；HVC/MMIO 出口经 resume_or_preempt 查量子（尽则 PREEMPT 弃帧）；其余（含指令中止 EC=0x20）= guest fault → 帧诊断+HPFAR + VM 隔离死 | 已实 |
| lower-EL irq    | el2_trap_guest：guest 态物理中断宿主栈处理；tick 递减其量子，耗尽置 need_resched；出口查标志 → preempt_save_frame 帧转写 → vcpu_exit（抢占切走） | 已实 |
| fiq / serror / 其余 lower-EL 槽 | 默认拒绝 panic + ESR | 占位 |

## Guest fault 隔离（27.04 第三站）

非 HVC 异常 → `fault: ESR/FAR/pc/sp/x0-x3`（读自陷阱帧，展示野访问
现场）→ vcpu_exit retire → vm_run `faulted -> VM_DEAD`。**坏 guest 杀不死
hypervisor**；宿主自身异常仍是 panic+WFI（边界反转之处）。

## SP bank 归一（27.04，gdb 实证后固化）

    _start: msr spsel,#0        宿主 C 世界 -> SP_EL0
    vcpu_init(): SP_EL2 = excp_stack   异常进入专用栈
    异常入 EL2: 硬件强制 SPSel=1 -> SP_EL2（任意时刻都有真栈）
    guest@EL1h: SPSel=1 -> SP_EL1（与宿主 bank 无交集）

QEMU 直启 EL2 复位 SPSel=1，宿主曾整体跑在 SP_EL2 上：vcpu_run 的
excp-stack 舞步会覆盖活栈、切到从未初始化的 SP_EL0=0，导致 ERET 前
宿主帧全为 flash 垃圾（host_ctx={0x20,0}，ret x17 落在 PA 0）。

## Stage-2 IPA 布局（两 VM 同构，仅 RAM 输出物理不同）

    VTCR: 4KB/T0SZ=32/SL0=L1/PS=40bit；SCTLR_EL1.M=0（纯 stage-2，VA 即 IPA）
    guest ELF 均链接在 IPA 0x40000000，QEMU loader 载入各自 PA 窗口
    描述符铁律：AF（bit10）必置位（实测 ESR 0x8200000A 指令中止）；
    RAM MemAttr=0xF，设备=0x1（S1 关闭时），设备页强制 XN

## 设备树注入（27.04 第十一站：Linux 前传第一幕）

guest 对世界的认知从硬编码改吃设备树。树源 = QEMU dumpdtb 裁剪补丁
（tools/dtb/eshine-vm.dts，裁剪规则在文件头：只描述我们提供的设备，
无 GIC/psci 节点=不说谎）；dtc 编译 blob 随仓提交（构建不依赖 dtc），
hypervisor `.incbin` 入 .rodata。注入 = vm_create 整拷进 guest RAM
固定偏移 0xF00000（guest 视角 0x40F00000，bootloader 同构语义；
预算 64KB，magic/totalsize 头校验，坏 blob 拒绝 create）。双 VM 共树
（树描述 IPA 空间，两 guest IPA 硬件一致；per-VM 差异待真实分歧出现）。
guest 引导期走查（guest/fdt.h mini-FDT 只读解析：头校验+路径深度
匹配+compatible 首串）自报三节点——设备身份从树查出而非硬编码
（门禁 [9-11]）。保留节点：memory（16MB 补丁）/cpu@0/timer（仅虚拟
定时器 PPI 11=INTID 27）/virtio_mmio（SPI 3=INTID 35）/pl011
（SPI 1=INTID 33）/chosen/apb-pclk。

## 超调用 ABI（HVC，第六站后仅控制类）

| imm | 语义 | x0 | 其余寄存器 |
|---|---|---|---|
| 1   | exit：guest 主动退出 → VM 层 TEARDOWN→DEAD | - | x1-x17 易失（EL2 可自由使用）；x18-x30 由陷阱帧全量保存恢复（第三站兑现） |
| 2   | yield：主动让出 → VM 层 RUNNING→PAUSED，返回点即续跑断点 | 入 0/出 0 | 同上；返回 = 从断点继续 |
| 4   | host：前台 guest 主动交还 RX（宿主置 owner=NULL） | - | 同上 |

第六站退役：#0 putc / #3 getc——IO 面整体走 vuart MMIO 陷阱仿真
（见"设备仿真"节），超调用只剩控制类；未列 imm 默认拒绝（guest
fault 隔离死）。guest 内联 asm 必须声明 x1-x17 clobber；跨超调用的
活跃值落 x18-x30（selftest 实证 x19/x30），由帧保存恢复。

## 中断面（27.04 第四站：GICv3 真中断 + 虚拟注入全链）

宿主第一次活在"被中断"里，guest 第一次收到与宿主物理线无关的虚拟中断：

| 层 | 形态 | 关键事实（QEMU 8.2.2 源码 + 活探针双重实证） |
|---|---|---|
| 物理 GIC | dist+redist+cpuif 单版本直书（IHI0069 偏移） | SGI 帧 = GICD 家族镜像（IGROUPR0@0x80、ISENABLER0@0x100，空洞 RAZ/WI）；GICD_CTLR bit1=EN_GRP1NS 分发门（G1NS 另需 ICC_IGRPEN1） |
| PPI 接线 | 活体探针定案（不抄 DTB 槽位序） | CNTHP（宿主 tick）=INTID 26；CNTV（guest vtimer）=27；均为电平触发（不碰 ICFGR） |
| HCR_EL2 | 宿主 0x10（IMO bit4）；guest 0x80002011（RW\|VM\|IMO\|TWI） | QEMU target_el_table rw=1 列：IMO 位是 EL2 物理 IRQ 回 EL2 的路由门；参考实现 sysregs.h 同值互证；vcpu_exit 恢复宿主值且 daifclr（异常进入自带掩码） |
| EL2 IRQ 入口 | el2_trap 平衡桩（两组当前 EL 槽 + lower-EL 槽同接） | 单一入口,eret 直用 ELR/SPSR;guest 运行中的物理 tick 断入后原样回 guest |
| 虚拟注入 | vgic_enable（ICH_VMCR=VPMR 全放行+VENG1、ICH_HCR En）+ vgic_inject_vtimer（LR0 = vINTID27\|pINTID27<<32\|G1\|pending\|HW） | 宿主对物理 27 只 EOIR 降优先级不 DIR；guest 虚 EOI（ICV 视图,EOIR 一发降+撤）经 HW-LR 硬件联动撤销物理线，电平线不重挂起 |
| WFI 陷阱 | HCR.TWI → EC=0x01（ISS.TI=0）→ VCPU_WAIT | WFI 陷阱不 retire：宿主陪睡（EL2 wfi,滴答/vtimer 唤醒）后代其完成（vm_run 内 PC+4），guest 醒后自查 vtimer 标志（TWI 下 wfi() 永不"返回"） |

中断数据观：未知 intid 记账 EOI 不 panic（同 fault 隔离哲学）；spurious(1023) 不动 EOIR。

## 调度（27.04 第五站：时间片抢占轮转）

双 VM 首次真并行；shell 是最高优先级任务，命令即原子段，泵只在
等输入处跑（宿主权威）：

    sched_pump()：轮转游标遍历 vm_table，每个 RUNNING 且非 WAIT 的
    VM 各跑一个量子（SCHED_QUANTUM_TICKS=1，即 1s）→ vcpu_run →
    cur_vcpu=NULL → 按原因码迁移（PREEMPT 计数续转 / WAIT 让位 /
    YIELD/EXIT/FAULT 走 vm_finish 善后 + RX 回收）
    vmrun <id>：异步——CREATED/PAUSED→RUNNING 即回 shell

抢占链两个扳机点（参考实现 "trap 退出即查" 的最小形态）：

    ① guest 态 tick：el2_irq_guest 递减 slice_left，耗尽置
      need_resched；lower-EL IRQ 出口查标志 → preempt_save_frame
      把 excp_stack 帧逐项转写进陷阱帧（asm，偏移契约见 vcpu.h）
      → vcpu_exit 弃帧（与 HVC 退出同构）
    ② HVC 出口：getc 轮询风暴的 tick 多落宿主段，el2_irq_host 对
      cur_vcpu 代计入量子；resume_or_preempt —— 量子尽则 PREEMPT
      弃帧走 vcpu_exit（HVC 出口帧已在 el1_trap 存好，无需转写）

RX 所有权（`sched_console_owner` = vcpu 指针，NULL=宿主）：前台模式
泵只跑 owner VM（免双 guest 抢输入，后台模式全轮转）；HVC #3 三态
代理（-2=无输入权 → guest WFI 让位，TWI 陷阱化）；HVC #4 主动交还；
WAIT/退场自动归还。

WAIT/WOKE（两源，第六-八站三段演化）：WFI 陷阱 → VCPU_WAIT → 泵置
wait_state=1 +pc+=4（代完成）+ **wait_reason 判定**（切出保存的本人
vtimer ctx 武装=TIMER，否则 RX）。TIMER 到点两路：物理 27（ctx 装载
者）与**每拍到期扫描**（第七站 sched_vtimer_sweep：物理恒为"当前客"
的，别家错过由扫描兜住；27 与扫描共用同一 cval 过期归属谓词——表序
找人在双等待者下会归错，实测）；WOKE → 泵注 LR0。RX 唤醒（第八站）：
shell 入环置 RIS → RXIM 使能即泵注 LR1（vSPI 33）→ guest handler
排空 DR 入软环 → getc 续取；泵内对仍有数据的 RX 等待者再唤醒兜底。
**RX 等待持 owner**（输入即唤醒源），**TIMER 等待才归还**（宿主睡期间
要能敲命令；回收出口 = guest 的 host 命令 HVC #4）。

## 设备仿真（27.04 第六站：vuart/MMIO 陷阱）

guest 的 UART 撤出 stage-2 映射（`S2_ATTR_EMULATED`，不建表项）——
DR/FR 访问即 stage-2 translation fault（DFSC=6），全部陷阱化：

    EC=0x24 → el1_trap 存帧 → mmio_dispatch：
      ISS 解码（ISV/SAS/SRT/WnR）
      IPA 重建：HPFAR_EL2<<8 | FAR[11:0]
        （QEMU 8.2.2 实测 HPFAR = IPA>>8：guest fault 探针
         0x30000000→HPFAR=0x300000；FAR 对 S2 fault 页地址无效、
         页内偏移有效——不信的是那半边）
      EMULATED 窗口匹配（默认拒绝：窗口外=guest fault 隔离死）
      → vuart_mmio_read/write（DR/FR 子集，u32 语义）
      → gprs[SRT] 回填（SRT=31=xzr 不回填）
      → pc+=4 代 retire → resume_or_preempt（量子契约不破）

关键不变量两条：**data abort 不退休**——ELR 指向 faulting 指令
（HVC/WFI 是"已完成"语义，ELR 指下条；两副面孔），仿真完必须代
retire，否则 ERET 原地重执行成陷阱环；**eret 尾 x0 自加载**——
trap_resume 的 x0 最后回填不可用 x1 中转（三站暗雷：MMIO 回填任意
SRT 时 gprs[0] 会灌回 x1），x1 与 x0 同批最后回填。

vuart 模型（per-VM 实例内嵌 eshine_vm）：DR 读弹 RX 环（64B；读后按
"仍有数据"重评定 RIS——多发注入的续注基础）/写走 TX（128B 行缓冲：
后台行按 `[vmN] ` 前缀整行冲、前台裸透传、owner 切换与退场冲尾不吞
半行）；FR 回 RXFE=环空、TXFF 恒 0。RX 路由 = shell 恒排空真 UART
按 owner 入环（宿主即仲裁者）。第八站增中断面：IMSC（仅 RXIM）/
RIS/MIS/ICR，RX 入环置 RIS，RXIM 使能即泵注 **LR1 纯虚拟 vSPI 33**
（Group+State(pending) 两位置足，**HW 位不置**——HW LR 挂起跟随
物理线；ICH_LR 真实位号 bit60=Group/bit61=HW/bit62=State，注释与
实现自此正名），guest handler 排空 DR 入软环，getc 中断驱动（FR
轮询路径退役）。guest 访存器全走 volatile asm 访问器：形式保证
ISV=1 可综合症（GCC 会合并出 post-index 回写型，宿主拿不到值）。

vtimer ctx（第七站）：per-VM ctl/cval 内嵌帧尾（@400/@408）；
cortex-a57(v8.0) 无 CNTV 陷阱位（guest 直写物理），泵以切换对齐：
切入还本人 ctx、切出即存（全出口），物理恒为"当前客"的；CNTVOFF
共享 0（双 guest 同墙钟）。

设备表化（第九站起）：vm_device 增 dev 字段，窗口命中按
VM_DEV_VUART/VM_DEV_VIRTIO 分发。

virtio（第十站）：**virtio-mmio legacy + split virtqueue +
virtio-blk**——第九站 vblk 的"virtqueue 种子"长成行业标准设备，
自制协议退役、同一静态磁盘移交（迁移即结构连续）。寄存器最小集
（magic 0x74726976/version 2/DeviceID 2/QueueSel/QueueNumMax=8/
QueuePFN/QueueNotify 门铃/InterruptStatus-ACK/Status，**DRIVER_OK
兼任中断使能**）；split virtqueue legacy 布局公式（desc 16N/avail
紧随/used 4096 对齐，宿主与 guest 驱动两侧自洽）；门铃路径：
扫 avail → 三描述符链解析（header/data/status，静态翻译直访
guest RAM）→ 扇区 I/O → used 环 append → 完成挂起 → 泵注
**LR3 vSPI 35**（新号与 vblk 时代 34 代际区分）→ guest ISR 读
InterruptStatus/ACK 清挂起。畸形链跳过，参数越界 status=ERR 照常
完成（设备级默认拒绝）。**持久化=结构保证**：静态磁盘数组不经
vm_create 复位，销毁重建数据仍在。中断通道编号化：
LR0=vtimer(HW)/LR1=vuart/LR3=virtio（a57 四槽，LR2 随 vblk 空置）。

## 路线注记（范围外，记录归属）

- 真板阶段：EL3/ATF 启动链（QEMU virt 无 secure world，本周用 virtualization=on 直进 EL2）
- LR 满兜底（maint PPI 25+UIE+ring）、UART IRQ 优先级/多中断源精细
  调度（当前单 RXIM）、多仿真设备表（当前 vuart 单设备）、CNTVOFF
  per-VM（当前共享 0）、vCPU>1/VM、优先级/实时策略、分配器
  （vm_space/vcpu 由内嵌换动态）、VM0=RTOS 管理 VM / VM1=Linux guest
- 与参考实现的结构对齐约定见 docs/baseline-deviations.md

## 验证

真绿门禁：`python3 tools/verify/verify_el2.py`（34 步：banner + prompt +
宿主 token 双回显 + tick 严格递增（CNTHP→GICv3→EL2 真投递判据）+
空表 + vmcreate + **vmconsole 未跑拒绝（状态门）** + vmrun 0 异步 →
**后台 banner [vm0] 前缀（陷阱 TX 整行冲刷判据）** + **guest 自报
fdt ok: cpu/uart/blk 三节点（第十一站 DTB 注入，设备身份从树查出
非硬编码）** + **guest 自报 uart irq on（RXIM/vSPI 33）** +
**guest 自报 virtio blk on（probe 三验→DRIVER_OK/vSPI 35）** +
**RX 唤醒链（入环→直通 runnable→FR 命中→prompt 重打）** +
VM0 selftest（寄存器跨 100 MMIO 陷阱）+ HVC#4 交还 +
vmstat 可读 + vm1 同律（[vm1] 前缀）+ 双 RUNNING + 双 VM 交错
token 双回显（RX 路由判据）+ **dwrite/dread（三描述符链→门铃→
完成中断判据）** + spin 3G 前后 preempts 差 >=2（纯计算
负载跨量子，帧转写无损判据）+ WAIT 公平（vm0 睡时宿主/vm1 活）+
后台醒（WOKE→泵注入）+ **双睡定序（后武装者先醒，min-deadline
判据）** + **双醒后双活** + exit/VM_DEAD + **virtio 持久化（销毁→
重建→旧数据仍在）** + fault 隔离（窗口外默认拒绝）+
全 DEAD + 宿主完整性复验 + guest 退场后 tick 仍增长（HCR/DAIF
恢复判据）+ vmkill 拒绝路径，静态 marker 不算；连续 2 轮全绿才真绿）。
make run 与 make verify 同构（同一构建产物），shell help 内置
34 步手动走查清单。
**同步纪律（2026-10-24 用户令）**：每站门禁步数/判据变化时，
verify_el2.py、shell help 走查清单、本节文字必须同 commit 更新
——曾因 replace 静默失败出现"标题 31 正文 25"嵌合体，用户实测抓出。
CI 在 .github/workflows/ci.yml，同步 GitHub 后生效。
