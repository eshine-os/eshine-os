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

## SP bank 归一（27.04 第十四站定型：全 EL 共享寄存器归还 guest）

    _start: msr spsel,#1        宿主 C 世界 -> SP_EL2（与复位同值）
    异常入 EL2: 硬件强制 SPSel=1 -> SP_EL2（任意时刻都有真栈）
    guest@EL1h: SPSel=1 -> SP_EL1；SP_EL0 = guest 的 current
    陷阱帧: sp_el0@416（el1_trap/el2_trap 转写存，trap_resume 还，
           vm_create 清零）——全 EL 共享寄存器随帧走

早期形态宿主 C 世界跑 SP_EL0：QEMU 复位 SPSel=1 下 excp_stack 舞步
踩活栈的旧账（vcpu_init 预载撤销，桩自置栈顶）；更深的账是 Linux
实测暴露的——arm64 内核在 EL1 以 **SP_EL0 存 current**（init_cpu_task
写入），宿主借作栈则每次 ERET 把 .stack 指针灌给 guest 当 current，
首个触碰点即无声向量自陷风暴（enter_from_kernel_mode 连环读，
printk 亦需 current 故 Oops 无声）。SP_EL2 世界 + sp_el0 入帧后，
该寄存器回到架构本义：guest 的。

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
| 虚拟注入 | vgic_enable（ICH_VMCR=VPMR 全放行+VENG1、ICH_HCR En）+ vgic_inject_vtimer（LR0 = vINTID27\|pINTID27<<32\|**优先级=guest 自配 IPRIORITYR 镜像**\|G1\|pending\|HW） | **站十四两路分治**：跑动客路径禁碰 CNTV、不熄 ctx，仅 EOIR 降物理优先级——Linux timer handler 轮询 ISTATUS 才认账、自 IMASK 落线、自装填下一拍，宿主越权代关即 jiffies 冻死（实测）；睡客路径（el2_irq_host）关线+熄火不变（WAIT 中无人替它落线）。LR 硬 0x00 优先级会落 Linux pseudo-NMI 阈值，故镜像 guest 配置 |
| WFI 陷阱 | HCR.TWI → EC=0x01（ISS.TI=0）→ VCPU_WAIT | WFI 陷阱不 retire：宿主陪睡（EL2 wfi,滴答/vtimer 唤醒）后代其完成（vm_run 内 PC+4），guest 醒后自查 vtimer 标志（TWI 下 wfi() 永不"返回"） |

中断数据观：未知 intid 记账 EOI 不 panic（同 fault 隔离哲学）；spurious(1023) 不动 EOIR。

### vGIC distributor/redistributor 仿真（27.04 第十二站：中断面 OS 化）

guest 的中断从此有 distributor：vGICD@IPA 0x08000000（0x10000）+
vGICR@0x080A0000（0x20000，RD+SGI 帧），镜像 QEMU virt 真布局，树里
intc 节点据此描述（its/maintenance 未仿真不登记）。寄存器分层：探测/
使能/唤醒路径真语义（PIDR2=0x30 判据、CTLR Enable+RWP 恒 0、WAKER
存储回读、ISENABLER 写置位/ICENABLER 写清位、TYPER 家族只读），
纯状态寄存器（挂起/活跃/优先级/配置）存储回读，未知 RAZ/WI（设备级
默认拒绝）；GICD SPI 寄存器族 0x80 宽分立解码（族基 0x080/0x100/…
首版共用 (off-0x080)>>2 把 ISENABLER0 算成 n=32 越界静默 WI，guest
使能回读当场抓获）。**泵注入三通道改按 guest 使能位门控**——vtimer
PPI 27（GICR_ISENABLER0 bit27）/vuart SPI 33 与 virtio SPI 35
（GICD_ISENABLER0 bit1/bit3）：distributor 成为注入权威，guest 没
使能就不注入、不代完成 WFI；再唤醒网同律。MMIO 分发新增 SAS→字节数
换算（1<<sas，编码值不当字节数传——半字掩码吃 4 字节读的坑记档）。
guest init 长成真 OS 形态：PIDR2 探测→CTLR/WAKER→SGI 帧 PPI 27→
GICD SPI 33/35→回读验证自报。EMULATED 窗口自本站允许多页（真实设备
布局天然跨页；直映射仍单页）。

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
退场归还（vm_finish/vm_kill 清 owner）；**站十五：显式接管**
（`vmconsole <id>` 后不再按 WAIT 原因自动归还——真 Linux 壳恒
250Hz armed，timer 睡即被收台则 owner 形同虚设，实测；归还三口 =
`vmconsole host` / **逃生字符 0x1d（Ctrl-]）**/ VM 死亡；裸机客另有
host HVC#4 自报）；**站二十六：控制台快捷键**——`Ctrl-^(0x1e)`
前缀 + `0|1|2` 直切 vm / `h` 回宿主，双击字面直递；选型避雷：0x1d
逃生契约不动、0x1c 是 SIGQUIT 不可偷、0x1e 无 TTY 信号绑定且与
0x1d 字节相邻；快捷键与 `vmconsole` 命令共用同一切换函数=零行为
差异。**站二十：TX 前台判定按 VM 对齐**——vuart 陷阱
的 fg 旗（mmio_dispatch）从"v==owner"改为"VM 相等"：双 vCPU 客
的进程上下文会被内核调度漂到任一 vCPU（vm2 的 sh 实测漂到 vcpu1），
旧比法把业主 VM 自家另一帧的输出判成后台，无换行的 busybox 提示符
滞留 bg 行缓冲永不再冲刷——与泵侧站 19D 的"双 vCPU 同 VM 皆前台"
（过滤按 VM）补齐一致。诊断面：`vmrx`（RX 两域现场 + GICR/CNTV）
与 `vmtrail`（事件迹环）宿主命令。

WAIT/WOKE（两源，第六-八站三段演化）：WFI 陷阱 → VCPU_WAIT → 泵置
wait_state=1 +pc+=4（代完成）+ **wait_reason 判定**（切出保存的本人
vtimer ctx 武装=TIMER，否则 RX）。TIMER 到点两路：物理 27（ctx 装载
者）与**每拍到期扫描**（第七站 sched_vtimer_sweep：物理恒为"当前客"
的，别家错过由扫描兜住；27 与扫描共用同一 cval 过期归属谓词——表序
找人在双等待者下会归错，实测）；WOKE → 泵注 LR0。RX 唤醒（第八站）：
shell 入环置 RIS → RXIM 使能即泵注 LR1（vSPI 33）→ guest handler
排空 DR 入软环 → getc 续取；泵内对仍有数据的 RX 等待者再唤醒兜底。
**owner 是显式接管**（站十五定型： TIMER 睡不再自动归还——Linux 壳
的 tick 恒 armed，自动归还让壳会话永远进不去；宿主夺回 = 逃生 0x1d）；
RX 等待者输入入环即刻唤醒，TIMER 等待者（壳等敲键）醒于下一拍后
自查环——4ms 量级延迟无感。

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
RIS/MIS/ICR，RX 入环置 RIS，RXIM 使能即泵注 **LR1 纯虚拟 vSPI 33**。
第十三站长成 Linux 驱动可绑形态：AMBA 身份证（PERIPHID/PCELLID 只读
常量，id 0x00141011）+控制族（CR/IBRD/FBRD/LCR_H/IFLS/DMACR 存储
回读，复位值按真芯片 CR=0x300/IFLS=0x12）+**CR 真语义**（DR 写要
UARTEN|TXE，RX 中断要 UARTEN|RXE——开机前 DR 写被门丢弃，guest
自报必须挪到开机后，本站战况）；波特率无时序不仿真。guest 引导按
Linux pl011 同律：身份证自查→关机→配置→开机→自报（门禁 [12]）
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
  （vm_space/vcpu 由内嵌换动态）、**VM0=eshine-rtos 任务内核客（第十六
  站：参考RTOS 公开设计为基准的从零最小人格——32 级位图抢占+tick 100Hz+
  IRQ 驱动壳；SIMD per-VM 帧入泵；第十七站同步原语：每对象 FIFO 等待
  队列的计数信号量+owner/递归互斥，DAIF 短临界段；第十八站：互斥单级优先级
  继承+per-task FP 帧——切换簿记宏内嵌 SIMD 存取；第十九站：事件组
  AND/OR+消息队列+per-task FP lazy（FPEN 拨杆陷阱轮转持有权）；第二十
  站：SMP 双核——per-CPU 就绪队列+自旋锁+SGI 跨核重调度，PSCI(hvc)
  唤起次核、vm0.vcpu1 上 CPU3 专泵、per-CPU GICR 帧；第二十一站：
  同步超时族（参考RTOS 0=no-wait/FOREVER 语义，take 环单窗化，超时
  摘除+owner PI 重算）+ panic 观测面（未知 EC 打印 ESR/ELR 后终态，
  扫描面无寄存器行）+ 栈防护（垫+哨兵+水位，ps 水位列）；第二十二站：[54] 定谳闭案（console 原子化+fg 行缓冲+泊位冲刷）+软定时器（tick-ISR 回调，静态池）；第二十三站：irq 注册表+live-sp 水位守卫+泊位延迟冲刷；**第二十四站：心跳按需化（只在壳自主睡时说话+hb 命令，提示符交互零刷屏）+ 输入⇒runnable 唤醒不变量（推环唤醒去 reason 门+泊位落定复查，[41] 家族输入蒸发面的交互侧收口）+ vcpu 进场 flush 幸存者退役（站十九不变量补完）；**第二十五站：唤醒架构收口——[41] 死亡帧可达性翻案（TIMER+ctl=0 稳态不可达，站二十四残余登记的证据基础修正）+事件迹环 16384（取证主证据链收编：预约/字段入专用递归自旋锁，丢更新与幽灵槽双杀；证据行整行持锁防撕）+法医双位（verify 门红自动取证=全类失败脸；CARM-STALL 同 cval 连醒 2000 上界=唯一可达坏类 consume-fail 活锁的绊线）+泵扫级 vtimer 兜底（介入运行者覆写物理线致泊者 27 蒸发、唤醒迟至 1Hz 扫描=tick 迟到 ≤1s 的可达延迟类；泵扫直查 saved cval 置 WOKE，时钟读后置只对 armed TIMER 泊帧付账，迟到 ≤1 泵圈，pending⇒runnable 在全部四类可交付事件上泵扫级成立）**）** / VM1=裸机对照组（guest2.bin）/
  **VM2=Linux guest（第十四站：6.6 LTS，Image 外置构建、stage-2 非恒等
  映射、per-VM 诚实 DTB；第十五站起端点=busybox 用户空间）**
- **四 pCPU 真并发（第十八站两核→站二十 -smp 4）**：CPU0=宿主
  shell+泵{vm0.vcpu0,vm1}（1s tick 量子）；CPU1/2=vm2 双 vCPU 泵；
  CPU3=vm0.vcpu1 专泵（PSCI CPU_ON 唤起，无量子跑到出），vm0 双核
  RTOS 与 vm1/vm2 物理并行；WFE/SEV 邮箱跨泵握手、console 自旋锁
  行级原子、VM 状态锁收敛 kill/在途退场竞态、VMPIDR per-vcpu
  （0x80000000|idx，guest MPIDR 视图=帧号，DTB/PSCI 三角一致）
- 与参考实现的结构对齐约定见 docs/baseline-deviations.md

## 验证

真绿门禁：`python3 tools/verify/verify_el2.py`（111 步：banner + prompt +
宿主 token 双回显 + tick 严格递增（CNTHP→GICv3→EL2 真投递判据）+
空表 + vmcreate + **vmconsole 未跑拒绝（状态门）** + vmrun 0 异步 →
**后台 banner [vm0] eshine-rtos 前缀（陷阱 TX 整行冲刷判据）** +
**RTOS 设备自证链（fdt ok 三节点/pl011 amba 身份证/gicd v3/gic on
27 33 35/uart irq on/virtio blk on——与裸机客同一 ABI 同款 needle，
第十一/十二/十三站判据原样移植到任务内核客）** +
**rtos: up（位图调度器+tick 起立）** + **RX 唤醒链（入环→直通
runnable→壳任务 READY→rtos> prompt）** + RTOS selftest（寄存器跨
100 MMIO 陷阱）+ **ps（shell/heartbeat/idle 任务表活体）** +
**spawn gate16（动态任务生-问候-亡）** + **fp 自证（1e6 double 和
跨任务/跨客切换精确=SIMD per-VM 帧判据）** + **sleep 3 睡中心跳
夹行（任务级并发硬判据）** + **yield（HVC#2→PAUSED→vmrun 续跑，
任务语境存活）** + HVC#4 交还 + vmstat 可读 + **semdemo（有界环
8/8——信号量对阻塞乒乓，per-item 计数 8+8）+ ps mid-hold（mutexdemo
持锁睡中 ps 表里 waiter BLOCK/contender SLEEP——阻塞是活状态非日志）
+ mutexdemo（阻塞醒序 unlocking < locked-after-block+递归 take 2+
非拥有者 give 拒绝——第十七站同步原语判据）** + **cpu1 online（PSCI-SMC CPU_ON
  次核起立）+ pidemo（站十八B：PI 饥饿反转，mid 忙旋饿死 low，
  high 挂等 boost 9->6->9 全弧）+ fpdual（站十八C：双 FP 任务
  并发双精确闭式=per-task 帧）+ **eventdemo/queuedemo/fpstat（站十九
  B/C：事件组 AND 双拍齐醒+OR 单拍即醒；16 条消息环满发阻塞；
  lazy FP M 个 fp-saves << N 次切换）**+ **SMP RTOS 五判据（站二十：
  cpu1 online (rtos psci) 次核自报+GIC 帧 1 ISENABLER0 回读
  0x08000001（MMIO 窗/nframes/使能门三层链全通）；smpdemo 双核计数
  行交错；ipidemo SGI 往返 >=20+跨核唤醒 3（ICC_SGI1R 写→EL2 陷阱
  →vgic 帧挂起→poke→泵注入→次核 IAR=0 全链）；fpsmp per-CPU FP
  帧双精确；次核在线主核不回归（ps 表+fpstat 两核求和严格递增）**
  + **站二十一三判据**：[40] tmodemo 超时面全弧（no-wait ×2/满发超时/
  空收超时/在时醒/no-wait 排空 + PI 14→9→14 全弧序断——超时摘除后
  owner 重算，"零改动"断言翻案）；[41] stkdemo 栈防护（水位收缩
  deepdiver<<shell + magic ok + ps 水位列）；[74] overflow→rt_panic
  指名 overflower（tick 扫描面无寄存器行，vm0 exit 后 vmcreate/
  vmrun/vmconsole 复活链，vm2-Linux 仍在场）——未知 EC 静默 wfi
  停车退役，panic 裸打不取锁
  + **站二十二两判据**：[42] swtdemo 软定时器三面（单发到点/周期
  ×3 自停/取消后缺席——核 0 tick-ISR 回调语境，rt_timer_t 静态池，
  站二十一超时账本的自然延伸）；A 块 [54] 四站悬案定谳闭案
  （console_putc 原语持锁 + fg 行缓冲化 + 泊位冲刷（爆发感知+全
  VM 静默判）——needle 字节级撕裂 ("spinni[vm2]…") 机制面根除，
  R1b dump 取证 + probe-tear 失败测试先行 + 双雷反转验证；第二十三站:[43] irqdemo 注册表链式活证
  （四硬编码 INTID 分支退役,替换式登记+RT_RX_BIT 泛化,探针 x
  0.1s 延迟补发供计数——命令字节在换钩前已被壳消费,首版机理
  为假）、[44] lowdemo live-sp 守卫（tick 读点在 ISR 链下方,
  告警线=危险线+读点差,贴线深旋恰一次 warn 且 magic 不误伤）、
  [65] echo 风暴 10/10（泊位冲刷延迟化:武装 CNTPCT+20ms 与
  fire 判-清共 console_lock,泵入口扫描+fire 时刻复验静默判
  （陈旧武装不拦腰冲行）,时间基=CPU0 宿主壳忙自旋,chardev
  分块缝吸收;评审迁窗:初版 [51] 落点 owner=host 恒绿空转,
  终落真并行窗 vm1 前台）
  + 真并行硬证（站十八D：vm1 spin
  独占 CPU0 的窗口内 vm2 内核行持续行进——单物理 CPU 下不可能）** + **vm1 裸机对照组
（[vm1] 前缀，双 RUNNING）** + 双客交错 token 双回显 +
**dwrite/dread（重锚 vm1：三描述符链→门铃→完成中断判据）** +
**spin 3G 前后 preempts 差 >=2（重锚 vm1；RTOS 并跑下跨量子，帧
转写无损判据）** + **WAIT 公平（重锚 vm1：裸机睡时宿主/RTOS 活）** +
后台醒（WOKE→泵注入）+ **双睡定序演进（第十六站：vm1 one-shot 4s
与 vm0 RTOS 任务睡 2s 并存——后武装者先醒；RTOS 100Hz 周期重臂
若串台踩了 vm1 的单发 cval，醒序即翻案）** + 双醒后双活 +
**virtio 持久化（重锚 vm1：销毁→重建→旧数据仍在）** +
fault 隔离（窗口外默认拒绝）+ **vmls 双态（rtos RUNNING 留活供
并发判据/裸机 DEAD）** + 宿主完整性复验 + guest 退场后 tick 仍增长
（HCR/DAIF 恢复判据）+ vmkill 拒绝路径 + **Linux bring-up（第十四
站：vmcreate 2/vmrun 2 → Booting Linux @CPU0（stage-2 非恒等映射 +
DTB x0 启动协议）→ Machine model: eshine-os,vm（诚实树）→
arch_timer virt 62.50MHz + arch_sys_counter clocksource → GICv3
redistributor 探测（word1=0x104 规范译码）→ **guest SMP 上线（站
十九D：smp: Brought up 1 node, 2 CPUs 真行判据，第二 vCPU 经
PSCI(hvc) 真起）→ **双客并发时序证明
（第十六站立判、站二十修订、**站二十四A 改 hb 注入**：活 RTOS
的 rtos: alive 行必须夹在 Linux boot 行流之间——串行 log 顺序
即证；周期心跳站二十四起只在壳自主睡（RT_SLEEP）时说话，提示
符交互零刷屏（用户令；[47] 零自发心跳断言），判据窗改门禁注
`hb` 命令 ×3（0.5/2.5/4.5s，窗 5s）——tick 严格递增即 vm0 在 vm2 靴流内真实行进，且
hb 的泊位→推环→醒→回路径身兼站二十四B 输入不变量见证；前台
门控（HVC #5）保留，vmrun 后台静默免扰宿主/他客串口）** →
ttyAMA0 enabled → vda 128 blocks →
**initramfs 接管（第十五站）→ vmconsole 2 直落 ~ # 壳 → 壳内
token 双回显 → cat /proc/cpuinfo CPU part 0xd07 用户态自证 →
**cat /proc/cpuinfo processor 1（站十九D：双核用户态自证，guest 内
第二处理器行可见；**站二十四B：超时 30s→10s 收紧——大 dump 后
泊位+立即输入正是 [41] 家族输入蒸发脸的复现器，健康轮 ~1s,
10s 上界=输入⇒runnable 不变量的诚实见证**）→
**utest（站二十八：vmconsole 0 后 EL0 syscall 面一门全验——getpid>0/未注册号 -ENOSYS/sysinfo ver+tick 回填；EL0 存在性证词=SVC 通道本身，[77]）** →
**站29 进程三门（[78] forkdemo COPY_UCONTEXT 全生命周期——子进程
eager 全拷+exit42+父 wait4 收尸；[79] mmapdemo 缺页零填——VMA 命中
页池补页+rw+munmap；[80] exedemo EXEC_SETUP 帧改写——pc/sp/x0 直改
保存帧，内核零镜像字节知识；per-process stage-1+ASID 位图+切换点随
任务装 TTBR0）** →
**站30 IPC 三门（[81] ipcdemo 三段消息 Send-Recv-Reply+优先级
传播——壳在 prio-6 占先干扰下 dt=0 快返=提权活证;[82] pulsedemo
异步脉冲 5 连发/排空;[83] abortdemo 发送者超时+UNBLOCK 脉冲 QNX
镜像;内核槽中继+coid 实体+引用计数+层级命名+CHF_UNBLOCK flags）** →
**站31 fd=通道三元组门（[84] fddemo——fdtable{coid,ocb}→三段消息
→resmgr 服务器:fdwrite(1) 经 tty 服务器落串口=全链活证,/etc/motd
+/etc/version 经 ramfs 服务器(cpio newc 构建期打包+内嵌解析+逐路径
挂名)读出全文+字节数自证;fork=fdtable 组拷+DUP 双侧引用,exit=逐
fd CLOSE——POCB 引用计数镜像;服务器 v0=内核任务,用户态 server
二进制=站 33 musl 弧）** →
**站32 signal+futex 两门（[85] futexdemo——musl 锁原语底座:入口
值校验 -EAGAIN/无等者 wake=0/扑克代演同址空间第二执行流的真实
等待→唤醒对(真 threads=站 33+)/超时真阻塞 r=-110;16 桶 key={proc,
va},站 21 单窗阻塞机器第三笔复用。[86] sigdemo——跨进程 kill 打断
子 futex=-EINTR,handler 在子自身用户上下文执行(syscall 出口投递=
帧手术:272B 整存+elr=handler/x0=sig),sigreturn 恢复;**SP_EL0 驻
t->usp 不在 272B 帧内——投递对必存还 usp,否则 handler 序言压帧+
sigreturn 跳过 epilogue=用户栈错窗读局部(冒烟打出序言预存槽的别家
字面,反汇编定谳)**;号面=真 Linux 98/129/134/139,errno EINTR=4）** →
**站33 双 musl 世界两门（[77] vm2 右翼：同一 48KB 静态 ELF
字节（tools/ramfs/bin/hello=build-musl.sh 产物入仓）在 vm2
busybox Linux 上跑；[88] vm0 左翼：run /bin/hello 全链=用户态
loader（fd=通道三元组读 ramfs→hint-mmap 段+EL0 预触缺页→
栈块 auxv→exec_setup 七参帧改写，内核零镜像字节红线保全）→
musl init→printf（writev）→exit_group→wait4 收尸；同字节，
两个 OS，一台 hypervisor；musl 1.2.5 上游零 patch 直连，
真 asm-generic 号面兑现日；EL0 FP 陷阱体自 EL1 向量抽出共用=
per-task FP 机器补 EL0 面）** →
**站35 线程门（[89]——musl pthread 契约兑现：pthread_create→
__clone(220) 帧整拷+x0=0+SP=musl 压好 func/arg 的栈；4 线程争
PTHREAD_MUTEX_INITIALIZER=futex WAIT/WAKE 真线程对；join=用户态
detach_state futex+CHILD_CLEARTID 内核面（线程退出 *ctid=0+
futex wake）；TPIDR_EL0 随任务切换存还（vec.S 簿记，站32
SP_EL0 同款 272B 帧外清点纪律）；counter=10=互斥串行化自证）** →
**站36 共享段两门（[90-91]——内核登记 shm 段（页自页池+refs
计数，create 取页/destroy 还页一口）+xspace 固定窗（块 8
@0x41000000，段 i 固定映同 VA=参考实现全局唯一窗的站内形状）+
VMA 第三型 pool=2 + fork 律（SHM VMA 子表同物理直挂零拷贝=
共享与站 29 eager 隔离两立；段页永不入 fork/munmap/退场三条
页回收路）；[90] shmdemo fork 共享判别器（子写同 VA 父读回，
eager 拷会私有化子写=读不到判负）、[91] msgseg 单拷窗
（1KB 载荷走共享窗一拷，8B 脉冲只带段引用，父按引用 attach
原地零拷读=站 30 差距⑤⑥ 的 v0 兑现；VM_SET 真身=免页表
遍历的单拷+延迟访问，页池恒驻+同物理直挂天然成立）** →
**站37 posix_queue 三门（[92-94]——mqueue 命名优先级队列
（高优先出同优 FIFO，空队 no-wait -110 脸，阻塞=站 17
pend 机器第 N 笔复用）/pipe 字节流双端（骑站 31 resmgr 零新
fd 线：pipesrv=第三内核服务器，满环/空读=持消息挂对象
等待队有界窗，写端全关=READ 恒 0=EOF 脸；站31 fd 层
WRITE 漏带 ocb 的潜雷被本站定谳——tty/ramfs 不读 ocb 故
无感，按端分发的 pipe 即爆）/长消息 1024B（len>64 自动走
长槽池，归还三口=所有 used=0 位）+MsgRead 镜像（接收者
收后按偏移两段再读所持=延迟访问）** →
**站38 信号完整门（[95]——rt_sigaction 4 参全脸(SA_RESTORER:
投递改写 x30=restorer,handler ret 落其内自调 rt_sigreturn,与旧
显式契约两立)+rt_sigqueueinfo(138,pid/tid 双定向:tid 先解析=
任务级待决位图先于进程面,x1=sigval)+默认动作=终局(无 handler
信号以 128+sig 走 exit 链,POSIX 面)+sigtimedwait(137)轮询面。
附带定谳:唤醒走查 t->entry 谓词=站 29 帧改写子盲区(首个自然
睡 fork 子睡死实抓),四走查活判据改状态面(TASK_LIVE)** →
**站39 FS 深面三门（[96-98]——ramfs 写时复制（首写拷入页池
页 ≤4KB,追加随位长；读优先 cow 面——rodata 零改动红线保
全）/CREATE 建即开挂名/UNLINK 死节点标记（免索引挪移撕开
在册 ocb;页还池）/READDIR 活节点序枚举（/dev/ramfs 元名
+单答形:一次走查恰一次回执）/SEEK whence 三档（SET/CUR/
END 透传 wire rsv 槽,lseek 返新位=POSIX 面,站33 loader 判
据同步改只拒负）/blkdev=第四 resmgr 服务器（真 virtio-blk
裸盘:宿主 EMULATED 静态盘后端（站9-10 机器）,写=读改写整
扇回写,WFI 等完成由 tick 兜醒;免 QEMU -drive）/容量账:
CH/CONN/NAME 表按 boot 4 服务器+demo 瞬时放宽（4 通道满
表=sigq 后继整脸溺毙实测定谳,站31 ocb 池同课:先算账后
放宽）** →
**站40 服务收编+busybox 八门（[99-106]——第五/六/七服务器：
rngsrv=xorshift64* 混 cntvct/ticks 两脸同源（fd READ+getrandom
278 内核面）/procsrv=/proc 任务表文本行+READDIR 第二消费者
（站38 TASK_LIVE 状态谓据）/slogsrv=环 16×48B+warn 控制台
策略；时间真号 clock_gettime 113/getres 114/gettimeofday 169
（monotonic=cntvct/freq,realtime=BOOT_EPOCH 常量+mono,RTC=
drvfw 后弧）；busybox=真 musl 精选集 264KB 双世界（同一字节
vm0 我们内核/vm2 真内核,md5sum 同哈希铁证;defconfig 1.3MB
驻内核像区 0x40200000 两区式硬界下塞不下=精选集 264KB 落
码,GPL 仓外配方入仓）——qemu-user -strace 实测需求面定谳：
openat 56/close 57/getdents64 61（每调一名=POSIX 合法面,fd
私域 dircur 游标）/newfstatat 79（STAT wire:mode<<32|size,
musl aarch64 stat 144B 零填+定点写）/dup3 24（DUP 双侧引用
镜像）/fcntl 25/uid 族零值/brk 214（窗内游标堆,mallocng 探
测）/目录前缀柄（名空间归服务器:直名未挂者回退路由 ramfs 元
名,basename 枚举+首现去重+深径判 DIR）/read/write fd≥3 路
由 fd 机器（fd 0=控制台面契约不动）;loader argc/argv 泛化
（启动块变长,写入抽 noinline 辅函数——偏移寄存器勿长持跨
EL0 缺页,11 站潜伏保存洞第一受害形态,build 敏感,调用边界
=帧重载;内核疑点记档挂账）+页包含映射（RW 段 vaddr 非页
对齐,Linux 同律）+栈预触（段预触同律）;init+inittab 形态=
壳代处理器（sysinit/wait/once/respawn+wait4 WNOHANG 探询
拍;真 PID1 init=后弧）;**ASID 还位=站 29 遗产缺口定谳**
（exit 注释谎称"已尽释"实从未还,11 站无雷只因电池从未超
15 次进程创建,respawn 每命令一拍=首个高频循环者,第 16
次 proc 创建即 EAGAIN 连环）;容量账再进:POOL 16→20/CH
8→16/CONN 16→24/NAME 16→32+栈 hint 回 1MB 位（窗口账:
hint 顶游标则 musl 线程栈族挤爆窗尾=pt 第 3 线程 EAGAIN 实
测定谳,容量是账第三课）;EL0 睡拍=futex 超时（hvc yield=宿
主暂停面,inittab 等待环误用=VM 首轮即冻实测定谳）;loader
门拍制（自主连拍=竞态放大器:beat 加载窗×任意门窗,mutex/PI
遗留态共窗壳静默楔两轮 verify+孤立二分定谳;initrespawn 命令=
确定性单拍 sync 等死即拍即收,自主连拍挂账后弧;旧案 loader
单飞（respawn 子加载与 run 子 fork 重叠=ramfs 通道双发送者
并发,seg copy 中途断实测定谳;内核侧并发发送者疑点带证挂
账后弧取证）** →
**站41 CAPR 能力框架（[107-111]——参考实现五面的最小站内形：
能力位图 u64 caps 四位 CAP_VFS/SPAWN/KILL/MM（create=零能
力,boot 壳=CAP_ALL,fork 按位继承,无任何加能力路径=self-inc-out
由构造保证）+fail-closed 执法点=choke point 非全铠甲（fd 六门
open/create/unlink/dir/getdents64/fstatat 判 CAP_VFS 缺=-1;
fork/clone 判 CAP_SPAWN;kill/sigqueue 判 CAP_KILL;mmap/shm/
mq/pipe 判 CAP_MM;read/write 已开窗不设卡 v0 记档）+rlimit 自
降单向（max_fd 默认 RT_FD_MAX 满额拒 -24 EMFILE/max_child
默认 RT_PROC_MAX-1 满额拒 -EAGAIN,rlimit_self 583 只降不升）
+uuid 进程身份（create 期站 40 混出器随机 8B,fork=新 uuid 资源
承继身份不承,零值 pid 换挡）；号面 cap_drop 580/cap_get 581/
uuid_get 582/rlimit_self 583;判据 [111]=壳 CAP_ALL 全脸隐式回
归;粗粒度位图/读写不分档/MAC-RBAC-LSM 真形=后弧记档;
站内最大战果=吞行自雷三小时法医：插 CAP_SPAWN/child_count
两检查时 Edit 吞掉 rt_proc_create(NULL,&cp) 一行,cp 成未初
始化野指针,fork_as 拿它狂写→EL1 数据中止→静默驻车（原
guest_park 语义,wfi 永环）=全系统死锁零故障行；三重伪象（state
=RUN 非 BLOCK/watchpoint 零命中/断点后零停点）最终由
"调用者驻车于 rt_sched 空队列 WFI"全解释,git diff 定谳;
**-Wall -Wextra 本就在构建里且当轮必已打 cp 未初始化警告
——警告在,没人读;freestanding 无 -Werror,构建输出必须过
眼**;第二判据教训重犯：新 demo 针线不看实输出,fddemo
假 FAIL 一轮）;站 40 提交完整性洞六文件补收（busybox 构建配
方+beat/initmark 源与产物+inittab——配方自宣"配方入仓"却没
入仓,git archive 纪律站 34 立站 40 未用;busybox 二进制仓外
=GPL 隔离故意非洞）** →
**站42 syscall 面补全（[112-118]——参考实现终审计补齐弧：调度器管
理面真号组（setparam/setscheduler/getparam/getscheduler/affinity
/yield/rr_get_interval/getcpu+setpriority/getpriority nice 图
[16±n/2 线性两向一致]；**getpid 槽号≠pid 真身定谳**——rt_pid_of
返任务池槽+1,壳 28 站全绿只因槽 0 恰等于 pid 1,首个 fork 子
getpid 现形,修=proc 面 pid 优先）;信号补全（sigpending 136/
sigsuspend 133=站24 rx 正牌三件套 wait_mask+BLOCK+rdy_remove 同
DAIF 窗+**掩码还原移 sigreturn**[sig_retmask/suspflag 两域=POSIX
掩码序:handler 用挂起窗掩码,完成还挂起前];sigaltstack 132 注册
回读 v0 不切栈）;posix timer+itimer（timer_create 107/delete 111/
settime 110/gettime 108/getoverrun 109+getitimer 102/setitimer
103;SIGEV_THREAD_ID 面单形,全局池 8 槽骑站 20 软定时器,回调
语境=禁调度版投递 rt_sig_queueinfo_ctx[tick 回调带 rt_sched=站20
契约违],overrun=上发未收计数;**站 28 tp 守卫谓词扩展**=posix
timer 池成合法第二驻地[守卫原只认任务池,真野值照抓不变]）;robust
futex 链（set/get_robust_list 99/100,exit 路径遍历[musl 律节点
基=锁字 offset 0,next@+0,用户窗范围门+100 跳保险丝]自动唤醒
等者=持锁者死线程得救）;进程组/会话（setsid 157/getsid 156/
setpgid 154/getpgid 155,fork 承继组与会话）;close_on_exec
（fcntl 25 真 F_SETFD/F_GETFD 位,exec_setup 语境逐 fd 摘除,
父侧不受子扰 POSIX 面）;uname 160 六字段。**裁剪记档**:E
（ERESTARTSYS 重启语义,改站 32 契约面大）/H（calltrace+ps 观测
扩展）/I（poll）三面后弧另立——账册 docs/specs/2026-11-01-
station41-post-reference-audit.md。**战雷三枚冒烟定谳**:getpid 槽号
真身（第一受害者定律新案:14 站无雷只因无人 fork 子调 getpid）/
sigsuspend 漏 BLOCK 态=站41"调用者驻车"反面教材当日重演/
**clone 子落父 C 帧=野址读**（恢复点在 wrapper 返回帧,新栈无父
局部,[sp+1304] 读出字面串指针野值 far=ASCII 定谳;修=必入自有帧
入口函数,musl __clone 同律——fork 子因整栈拷贝无此约束,两种
child 两种纪律）**） →
**站43 观测面+poll（[119-121]——参考实现审计裁剪记账 H/I 兑现日）：
calltrace=x29 FP 链走查（16 帧保险丝+内核窗门[窗内帧才报，用户
帧止步]+panic 钩子[abort 面自动打栈，EL1 取证从"打印一行"升"打
一链"]）;ps blk 归属列=wait_src 域挂起原语归属（sem/mut/rx/futx/
slep 各打点，站30 msg 等待经 ch->msgs 信号量显示 sem=机制复用
的如实面;**futex pend_q 阻塞不经 sem=漏标显陈旧值，站43 自己的
洞自己补**）;ppoll 号面 73（musl poll 走 ppoll）+RT_FDOP_POLL
wire op——per-fd 串行往返骑站31 fd 三元组零新等待逻辑（超时=
tick 轮询环，Linux pollfd 8B {int fd;short ev;short rev} 契约，
events@+4/revents@+6）;tty/ramfs/pipe 三服务器各答 POLL 脸
（pipe 读端=count!=0，写端 POLLOUT 恒真 v0 面）。**战雷三枚**:
events@+4（首版 (u16)(ent>>16) 取 fd 高半=ev 恒 0，POLLIN 永不
成立——服务器侧取证 count=9 答 1 全程，判据面指向）/pipesrv
无 POLL case（默认 -22 落 POLLNVAL）/冒烟驱动晚挂=RX 死窗
（vmconsole 必须紧跟 vmrun，verify [19] 同款 wait=on 形状）**）** →
**站44 ERESTARTSYS（[87] 原位改针——站42 裁剪记账 E 兑现日）：信号
打断可重启——futex WAIT 的 sig_intr 改返 -512（内部分发器消化不越
ABI），dispatch 单点帧手术：elr 回拨 4 重取 svc+x0 槽不写保入参；
随后的出口投递整帧存 sig_saved（含回拨 elr+原 x0），handler 归还
即同参再入，无 handler 默认动作终局回拨无害。-EINTR 只存 POSIX
特例 sigsuspend（恒返，sigdemo2 判据不动）；sem/msg 打断被 take
环吸收重挂（站32 既有契约）非生产者。判据=sigdemo 迁移：handler
内戳字 → 重启再入入口值校验 -EAGAIN——一判决两事（elr 没回拨=
执行 svc 后指令错窗垃圾；x0 没保入参=拿 -512 当 uaddr 野访）；
重启破则旧脸 -4 落 FAIL 行门红自抓。ERESTARTNOHAND/restart_block
记档非目标（无消费者不落码）** →
**站45 APS+MPRT 分区化（[122-124]——参考实现 k_aps.c/k_apm.c 白描兑
现日，站27 差距清单分区项）：CPU 预算分区=百分比预算+tick 记账
（窗 20 tick，credit=budget×20/100，两核同面，idle 池外免记账）+
超标节流（复用站 17 睡客机器 state=RT_SLEEP+wake_tick=下窗界，
零新等待逻辑）+**偷闲**（无人跑=free CPU 回冲记账，QNX guarantee
+steal 两语义）；分区 0=system（100% 余量池，新分区从它扣，Σ≤100
纪律）。内存预算分区=**声明窗单一咽喉**记账（as_declare_pool=
mmap 与 fork 同口，一钩双面；declare-time 模型记档差异 vs 参考实现
resident 页）；munmap uncharge+退场全退；fork 承继入组（参考实现
mprt_fork 白描）=孙窗全拷入组超限 fork 被拒（内存隔离带 fork 面）。
判据=apsdemo（对齐窗界起任务,两满窗确定性 share=20% 带 10-30
宽判+偷闲窗 45 拍独跑满账）+mprtdemo（16p mmap ok/再 mmap -12/
fork 孙 -12/munmap 复 ok/退场 cur 0）+parts（两表,k_aps_show
镜像）。**站38 律第六案（复用槽新域入清单）**:mpart 残值 0=
幻影分区 0,fork 儿全被 max=0 拒,冒烟四脸连炸一根因定谳;
**fork_as identity VMA 记账雷**:旧码一股脑 as_declare_pool,
记账钩子下 .user 块 14 页被当匿名预算收,修=pool 0 走专口
rt_as_declare 零记账。零新 syscall（demo 蹦床面,k_part_create
同位）** →
**站46 崩溃取证链（[125-126]——参考实现 coredump 白描的内存形状,
站29"野访终态面留边"翻案日）：EL0 无 VMA 野访由**全系统 rt_panic
升死者本人 exit 链**（code=139=128+11 SIGSEGV 面,同信号终局机器
fd 清+线程群扫+ZOMBIE;EL1 panic 面不动=内核 bug 仍全驻车）。
**crash 环**（rt_crash.c,2 槽）：{tick,pid,名,esr/elr/far 三元,
code,x0-x5+x29 寄存器面,用户 x29 帧链前 4 帧}——ELF 核文件不可为
（ramfs=只读 rodata）,内存记录+shell 倾倒=v0 诚实形记档。记录点
两处：野访终态（硬件三元全）+信号默认动作（esr/elr/far=0）。
**用户帧链窗门**（identity∪mmap 两窗）+**映射门**（逐读前 l2 PTE
present 查——坏栈不得把取证变成 EL1 二次事故）+单调门。**-O2
omit-frame-pointer 定谳**：用户码默认无 x29 链,首版 walk 0 帧
（x29 残值在窗内但 [fp+8] 非返回址）;optimize 属性方案污染全 TU
代码生成炸 strlen 弃;修=crashdemo 法医夹具手工挂 {x29,x30} 帧,
真链用户构建（-fno-omit）同一机器即全链** →
**站47 跨 VM 服务面（[127]——参考实现 xspace 全局窗/VM_SET 白描兑现,
批令第六站）：safeshm 窗=宿主自有 2MB 物理雕刻（PA 0x52000000,
三客 RAM 之外的空档）经 stage-2 双映 vm0/vm1（IPA 0x41200000 块 9,
mem[1] 槽零新机器）;配置权唯一属宿主=安全面。**vm0 设备后端形**:
窗在 RTOS stage-1 入内核 EL1-only（**必须双侧 L1:内核 PGD+per-proc
半区——单侧加块=壳 demo op（per-proc 表+SPSel=0）写窗落 SP0 同步
槽=guest_park 静默驻车站 28 家族,-d int 定谳 ESR 0x25/FAR 0x41200010
→0x40013000**）。协议={seq,ack,text[64]}代际槽位（写方先 text 后
dmb 再 bump;消费=ack==seq,任何时刻入场皆真）;**零拷**（参考实现 VM_SET
单拷的优记档）。**前台独占泵律定谳（本站最大战果）**:站19D 的
4 行规则 `if (console_owner && owner≠vm) continue`——谁持前台谁
独享 CPU0,后台 VM 零切片;十连仪器取证（别名 gdb/双侧 stage-2
描述符/vcpu 存档 PC-x5-x6/自写回环）全部指向正确,答案在调度器的
泵规则里。demo 依律重铸:**开机常驻 xvmsrv 生产者（静默,交换才
出声=站24 安静承诺)+vm1 客 xvm 消费者+host-owner 窗舞**（xvm 消费
后 `host` 交还=owner NULL=双 VM 并跑）。WFI 首版死于泵律（vtimer
亦不注入未调度 vcpu）记档;apsdemo 偷闲判带随常驻任务放宽（40→20,
真竞争者=真前提）。零新 HVC,doorbell vIRQ 后弧** →
**站56 DFX 收口（[162-164]——迹环过滤查询+per-VM 抢占会计+同病自审定谳,批次 B 末站）**：
vmtrail 查询面（`vmtrail [N] [cpu0-3]`——CPU 过滤=vm 过滤的拓扑近似
[cpu1/2=vm2、cpu3=vm0.v1 专泵,cpu0 双客混载]）;per-VM 抢占会计
（vm_preempts,双扳机点[量子耗尽泵侧+帧转写异常侧],vmstat 双会计行
[vm-preempts+quanta]）。同病自审三件定谳（记档）:§C25 量子记账无
cycle 补偿=granularity 即量子本身(1 tick/片,无累积损失路径,亚拍量
子再议);§C23 CPU_ON 握手原子性=有界等待兜底(丢拍降级不挂死,真板
event register 语义差异真板弧重审);§Q10 宿主侧 FP eager=TCG 非性能
目标(RTOS 侧已 lazy 站19C;真板弧)。

**站55 PSCI 补面+配额会计（[158-161]——参考实现 PSCI 面+实时配额调度 白描,批次 B 第三站）**：
CPU_OFF（调用者帧泊停,核 0 恒在拒;wait 无源泊,SGI 再醒=记档）/
AFFINITY_INFO（客视角 MPIDR 帧号→0=ON/DENIED）/**SYSTEM_OFF**（干净
VM_DEAD 无 fault 行）/**SYSTEM_RESET**（复位旗+泵出口复位链:teardown
后 vm_create+vm_start 重建重启——console owner 释放,复活后重接管）。
per-VM 泵量子会计（vm_quanta,vmstat quanta 行;实时配额调度 BUDGET/USED
白描的会计面,强制执行=消费者召唤后弧）。pscidemo/pscireset/pscioff
三命令(EL1 hvc 直调,psci_rtos_cpu_on 同 conduit）。

**站57 AF_UNIX sock（[165-167]——参考实现 unix_sock 服务白描,批次 C 首站）**：
socksrv=第九内核服务器（own channel+/dev/sock 元名;boot 链尾静默起=
站 50 零 boot 期往返纪律）。**真号八门**（aarch64 asm-generic 号:socket
198/bind 200/listen 201/accept 202/connect 203/sendto 206/recvfrom 207/
shutdown 210;站 42 定号纪律——musl 直连零 patch）;sendto/recvfrom=
fd≥3 读写面路由（数据面零新 wire op,骑站 31 READ/WRITE）;控制面新
wire ops 12-17。语义:bind=对象挂路径（服务器内表）;**connect=即建对**
（accepted socket 预入听者 accept 队=backlog 语义,数据在 accept 前
即可流）;空读/空 accept/满环=-110 有界窗面（pipesrv v0 同形,POSIX
无限阻塞=后弧）;shutdown/close=断对+对端 EOF 面（READ 恒 0）;DUP=
refs 引用计数（fork 继承镜像,pipe 同款——子进程退出 CLOSE 不误收
对象）。fd 辅助三件共享化（fd_zero/fd_slot_ok/fd_used_count,rt_fsrv.c
出+rt_sock.c 吃）。消费者:tools/ramfs-sockdemo.c（单进程回环序,
connect 先于 accept+send 先于 recv=零重试环零时序,冒烟确定性优先;
printf 收口="sockdemo: echo ok <ESHINE57>"）。

**站58 virtio-net（[168-171]——OASIS virtio 1.1 spec clean-room,批次 C 第二站）**：
**virtio-net 设备模型**（第二窗口 0x0A001000,legacy,设备 id 1;q0 rx/q1 tx 双
vq;F_MAC+配置区 MAC;tx kick=回环后端:读 tx 链帧入设备暂存区,拷进 guest 预投
rx 缓冲,双 used 环同拍完成）;vSPI 36（LR 通道=VTR 探测:QEMU 实测 4 LR——LR4
写=未实现寄存器 EL2 异常 ESR 0x02000000 首版即死,VTR 探测后与 vblk 共 LR3,
同 pCPU 语义不撞记边界）+唤醒/注入双钩（vm.c 泵路径）。**消费者=vm2 Linux
virtio_net**（DTB 增第二 virtio_mmio 节点;busybox 精选集+IFCONFIG/PING+initramfs
dev/mem 节点）。**定谳矩阵（九刀二分+插桩,站 53 MDCR/TCG 同例的平台边界）**:
撤 vIRQ=活;IRQ+空 poll=活;rx 完成无帧=活;零帧=活;IP ethtype=活;**ARP
（请求/应答,自/他 IP,单播/广播）=死**（6.6 Oops 于 __netif_receive_skb_core+0x4
执行 FDT 字节,ESR 0x02000000=QEMU 未实现寄存器同号异常;host 侧 gdump 取证:
客物理文本净且崩前崩后不变=客 stage-1/我 stage-2 映射分歧,机制未穿透）——
v0 丢弃 ARP 回声（真实 NIC 不收自家出向广播,回环回声本就病态;RX 完整递送
证明走原路设计:宿主侧 QEMU -netdev 桥接弧）。**gdump 宿主取证命令**（客 RAM
十六进制倾倒;EL2_sync 加 ELR+EC 解码——LR4 定谳即其产物）。

**站59 生态件入仓（[172-174]——cJSON/zlib 构建配方+demo 消费者+3rd 台账,批次 C 第三站）**：
cJSON 1.7.18（上游 MIT,tarball 入仓 3rd/;单文件库直编免 configure）;zlib
1.3.1（上游 Zlib 许可;核心源直编 libz.a——CHOST configure 对 musl specs
语境不友好,记档）。**消费者=EL0 demo**（musl 窗口语境）:jsontest（解析
治理面真清单 /etc/services.json——cJSON 全功能走查,输出确定性;RTOS
内核自研最小 JSON 不动,站 51 决策）;ztest（**slog 压缩落盘**:
deflate /var/log/slog→/tmp/slog.z→读回 inflate 逐字节校验;缺 slog 回退
services.json）。**3rd/LEDGER.md 台账起步**（§62 SBOM 前置事实源:名/
版/许可/形态/源/用途六列;GPL 仓外隔离=busybox 先例;append-only 纪律）。

**站60 benchmark 延迟基线（[175-176]——cyclictest 形,批次 D 首站）**：
**nanosleep 真号 101**（号面纠偏:首版臆 35=x86_64 号,asm-generic/musl
aarch64 头真号 101——站 42 定号纪律重犯,头为准）;**POSIX 至少语义**=
ceil+1 拍界（拍界醒最短 (k-1)T+拍内相位,+1 拍界才保证 ≥ns;代价=最多
超睡一拍 10ms=tick 量子的诚实超睡记档）。消费者=/bin/cyclictest（200 环,
10ms=1 拍周期,超额入 1ms 桶 0-10/>10）。**基线定谳**（docs/benchmark/
wake-latency-baseline.md 落盘）:主簇 9-10ms=恰一拍超睡（相位锁定 ≈30µs,
200 环 149/50/1 分布）;">10ms 尾 25%=相位游走偶越拍界（≤20ms 双拍超睡）;
亚拍面恒零（tick 量子调度器无亚拍唤醒——hrtimer/oneshot=后弧）。趋势门
（数值非硬门）:直方图形状+max≤3 拍界+基线文档在位;后续站（真板/中断
接管/优先级面）重跑对照主簇位移。

**站61 yocto 镜像聚合（[177-178]——终装,批次 D 第二站）**：
**machine conf**（meta-eshine/conf/machine/eshine-qemu-a57-type1.conf:
QEMU virt a57 type-1 目标机;ESHINE_QEMU_* 面六变量=run.sh 启动参数的单一
事实源,非散落脚本;站 34 的 qemuarm64 位面占位退役——local.conf 硬设盖
环境变量的坑记档:改 setup-yocto.sh 生成面+存量 build 一并打补丁）。
**eshine-image.bb**（deploy-only 终装:do_deploy[depends]=eshine-rtos:do_deploy
→**读 deploy 类的 sstate 镜像面** ${DEPLOY_DIR_IMAGE}（deploy.bbclass:
inputdirs=WORKDIR/deploy-PN,outputdirs=DEPLOY_DIR_IMAGE——每配方 DEPLOYDIR
独立,共享面在 sstate 输出）→boot/ 四件+manifest.json（版本+机器+sha256
工件账）+run.sh（终装入口:机器定义生成,串口 socket 可入参））。
**判据=镜像产品真实可靴**（站 34 同型第二台 QEMU:[177] 聚合完整性
（boot/ 四件+run.sh+manifest 在位+sha256 逐件核）;[178] run.sh 启动
全链 musl hello——终装入口自靴=组装非拼盘）。

**站66 bsh 无参交互（[183 面]——行 REPL 定稿+泵 RX 注入门修真;
常驻 ash 竞态挂 58 族账）**：用户令"务必实现无参交互"。全案取证
（gdb-multiarch 源级直读=第四镜:eshine.elf 符号直读 vm/vcpu/任务池/
调度队列/客 RAM;HMP xp 读 MMIO=假象对照定谳弃用）——**bsh 无参
定格的观感 90% 是设计**（站 24A 心跳门:壳非 SLEEP 态静默;vm0 实况:
tick 100Hz 活/心跳醒睡循环活/ash BLOCK RX_BIT 睡 stdin 合法/字符进
宿主 vuart 环+vm0 内核 rrx 环）;**真断=33 注入链**（泵行 730 的
`wr==TIMER && owner==v`,站 24B 设计排除面+owner=NULL 非确定读数
[watchpoint 未抓到]+33 使能位疑清）。**泵注入门修真**:非 owner
TIMER 泊若 RX 环真有可取数据（过站 24B 同款门控）亦醒（空环不醒,
站 20 热自旋教训保全）——常驻 ash 交互链曾点亮（"sh: selftest:
not found"=ash 吃行铁证）但**非确定**（1/4 活率,与 58 挂脸族[未定谳,
boot 20-75% 挂]纠缠）。**站 66 定稿=行 REPL**:bsh 无参→壳父自读行
（主循环同款 sys_read 字符面=几十站门禁验证的稳定输入面,带回显+
退格）+每行一个 sh -c（确定性 fork+wait 面,11/11 基础上）→空行
exit 回 rtos>——**绕开常驻 ash 的非确定 stdin 中断链,活率 3/4**
（唯余 58 族挂脸靴,全局问题非 REPL 面）。**语义边界记档**:变量
不跨行（每行新 sh）;busybox applet 内联活（NOFORK:echo/ls/cat/
变量展开）,非 busybox 命令 execve=ENOSYS 127（exec=用户态 loader
红线）。场景 18/18 绿（sh -c×2+REPL 全链）;[183] 判据=sh -c+REPL。
**常驻 ash 交互可靠化=58 族专场前置**（下一站首选）。

**站65 bsh 子壳（[183]——busybox ash 并排共存面;两个 musl 契约缺口
由 busybox 上门讨）**：**musl fork 契约**（aarch64 号面无 __NR_fork
（asm-generic 64 位段）,musl 的 fork()/vfork() 都=clone(SIGCHLD,0)——
sys_clone 零线程旗转发 sys_fork（进程复制语义）;站 42 的 clone 只认
线程族（NEEDS=VM|THREAD）把 musl fork 全拒——busybox ash=仓内首个
fork 的 musl 进程（hello/beat 全不 fork）,"sh: can't fork" 定谳）。
**子继承父 tpidr**（站 35 线程面 SETTLS 的 fork 缺角:孙不 exec 直跑
busybox 代码,__pthread_self=tpidr-0xA4,far=-0xA4 野访由站 64 core
文件上门讨;修=sys_fork 拷父槽,vec.S 切换存还链既有）。**bsh 命令**
（bsh <cmd...>=busybox sh -c 一次性子壳;判据面=ash 解析器内建:
echo+变量赋值展开;printf=applet 走 execve=ENOSYS 127 记档（exec=
用户态 loader 红线,ash 不识）;$((算术)) 构建禁用记档）;**无参交互
stdin 流模式=深水挂账**（ash 无参 fork 孙后 vm0 全停:tick 断+宿主泵
活（gdb 定格:PC=pump_run_vcpu+0x2a8 mrs cntvct_el0 前拍）+宿主热键
不响应+QEMU 0% CPU=四核 wfi 中断全断——孙交互/tick 链双深水未定谳,
专站收;参考实现基准无先例面）。

**站64 ELF core 文件（[181-182]——批次 E 深水终站;站 46 "核文件
不可为"翻案）**：**rt_ramfs_core_store**（rt_fsrv.c 内核侧直写:crash
语境=死者本人 EL1,一次直写零消息往返——消息面=活任务的活路,取证面=
死者最短路径;ramfs 站 39 COW 页承载——只读 rodata 的"不可为"前提
被站 39 拆除,站 64 只是消费;CREATE 同序:字段先填 rf_nnode++ 殿后=
半节点不可见;不挂名,open 走站 40 元名回退路由）。**ELF core 序列化**
（rt_crash.c:ET_CORE+EM_AARCH64+单 Phdr PT_NOTE+双 note——NT_PRSTATUS
标准 desc=elf_prstatus 最小形(siginfo+cursig+pid 四元+time 四元 0 面+
gregset 272B=死者帧全寄存器 x0-x30+sp+pc,gdb/readelf 语义可读)+
NT_ESHINE 自定扩展（code/tick/esr/elr/far+用户帧链;type 高区记档）;
600B/核,一页 COW 上限内）。**EL0 coreparse 工具**（tools/ramfs-coreparse.c
musl:open/read 循环+Ehdr 校验+note 走查,打印 pid/cursig/pc/sp/esr/
far/frames）。**gcore 命令**（EL0 壳:core 文件 hex 行转储串口——
宿主重组判据面）。**双解析器对照**（门 [181]=EL0 coreparse 消费面;
门 [182]=宿主 python 重组+独立解析 Ehdr/PT_NOTE/NT_PRSTATUS/sig/pid
逐项对齐——binfmt_elf 形的独立验证,readelf 可替换面）。野访终态与
信号默认动作两路崩溃链同收（rt_crash_record 尾部单挂点）。

**站63 docker 弧（[180]——构建环境容器+发布镜像 tar,批次 E 首站）**：
**docker/Dockerfile.build**（工具链运输形态:基座 ubuntu:24.04=宿主同源
noble——apt 交付版本与 manifest.yaml prebuilt pin（gcc 13.3.0/binutils
2.42/qemu 8.2.2）同源即同 pin;件面=cross 工具链+qemu+dtc+python3+make）。
**scripts/docker-build.sh**（CI 容器化判据:镜像内重建全链——3rd 三件
+make demos+make -B+sbom-check 全绿;**版本对账=容器实采 vs pin 逐字相等**
（pin 是对账出来的,不是注释出来的）;--user 降权跑=产物落宿主树 owner
即宿主用户,镜像层零仓字节）。**scripts/make-release.sh**（发布镜像 tar:
站 61 部署四件+run 入口（机器定义镜像面）+manifest.json 工件账+LICENSE/
NOTICE 合规面 → release/eshine-os-\<ver\>.tar.gz;**发布=可解包即靴**——
脚本尾自靴判据:解包临时目录现场重演 musl hello 全链）。门 [180]=文件面
+语法面+指令面（docker build 实跑=站级冒烟,门禁不嵌重构建=时间预算律）。

**站62 版本 pin+SBOM 台账（[179]——外部件账本,批次 D 第三站）**：
**3rd/manifest.yaml**（机器可读事实源;八组件:musl 1.2.5/cjson 1.7.18/
zlib 1.3.1（in-repo,工件名含钉死版本）+busybox 1.36.1/linux-image 6.6
（GPL 仓外隔离）/gcc 13.3.0/binutils 2.42/qemu 8.2.2（宿主 prebuilt
台账）;站 59 LEDGER.md=人读面,manifest.yaml=机读面）。**NOTICE 聚合
对照**（八组件名+许可全列;LICENSE=Apache-2.0 本体）。**tools/sbom-check.py
完整性脚本**（门 [179] 消费者:字段齐/in-repo 工件在且名含 pin 版本/
GPL 恒仓外/NOTICE 列全/许可证本体——五面全绿才 0 退;极简 YAML 子集
解析器零依赖）。

**站54 跨域 fail-closed 门（[155-157]——safeshm 窗 ACL+运行时 stage-2 改表,批次 B 第二站）**：
xvmacl 授权表（默认 vm0/vm1 授权=站 47 几何保位,vm2 不授）+运行时
开关=条目级 stage-2 改表（`stage2_window_toggle`:l2_ram 单块条目
清/置+**运行改表三纪律**——dsb sy 前置→TLBI IPAS2E1IS（IPA[47:12]|
VMID<<48,内广播跨核）→dsb sy+isb 收口=reference-audit §C2 改表守则的
站内形,亦是 demand paging/balloon 的入场券）。revoke→客访问=翻译
fault→隔离死（默认拒绝面不动）;re-grant→DEAD/INIT 下次 vm_create
按新旗建表（授权跨槽位复用持久）;xvmacl 命令自解析 id+词
（parse_id 单字符局限记档）。

**站53 debug/PMU 隔离面（[152-154]——reference-audit 审计"两仓同病"项,批次 B 首站）**：
MDCR_EL2.TDA（bit9）guest 进场置/宿主还场清（真硬件机制：EL1 的
MDSCR_EL1 等 debug 寄存器访问陷阱化，EC 0x18 收口——MDSCR per-VM
代访（帧新增 mdscr 槽）+debug 族（crn 0-2）/PMU 族（op0=3,op1=3,
crn 9/14）RAZ/WI（Linux hw_breakpoint 探测见 0=优雅降级）。**QEMU
TCG 平台实证（站 53 插桩定谳）：不实现 MDCR_EL2 陷阱 enforcement
（handler 零触发），MDSCR=惰性存根（写弃读 0）、PMCR=ID 真值可读
计数惰性=平台级无泄漏；机制为真板弧就绪**。**参考实现头标 TPM=bit0/
TPMCR=bit1 与 HPMN[4:0] 撞位记档（实证：0x203 写入把 HPMN 改 3
=QEMU 客进程挂死毒源；TPM 位号存疑真板重核）**。dbgdemo 探针
（平台事实报告：trapped/quiescent+raz/id 双形态）+vmdbg 槽倾倒+
vm1 boot 探针（guest/main.c）。

**站52 ham 健康面 v0（[149-151]——参考实现 ham 白描的最小站内形,批次 A 末站）**：
心跳实体（名+期望拍距+最近拍）+missed 条件（2 周期无拍=>MISSED 态+
log 动作[elog 环入卷+控制台+站 50 持久化审计]）+恢复拍=>HEALTHY
（recovered 动作=记录）；动作五型取 log 型，command/signal/message/
wait 型+raise 型条件+自愈守护=后弧记档。hamsrv 内核任务 1s 扫描拍
（站 17 睡客机器复用）；与治理面（站 51）互为表里——治理面管怎么起,
健康面管活着没。ham add/hambeat/ham ls 三命令（demo 蹦床,重名=复用
重整）。

**站51 driver_manager 治理面 v0（[146-148]——参考实现白描:JSON 服务清单+
有序拉起+就绪门+waitpid 监控+重启上限;RPC 注册面 v0 不立,批次 A 第三站）**：
治理域=inittab 基流之外的服务域（inittab 判据行是门锚,不动）。
`/etc/services.json` 清单（marker/gated/beater 三服务）驱动：最小
JSON 行走器（schema 约束:平对象/无嵌套/仓内配方生成——js_key 按键
取值,首版漏跳开头 '{'=manifest parse failed 战雷）+有序拉起（boot 尾
与 svcdemo 命令同体交互复跑）+就绪门检查点（探针 open,100 拍上界;
顺序语义=检查点非同步栅,异步拉起形态=后弧记档）+respawn 监控
（waitpid 阻塞收尸+restart 上限+giving up）。EL0 壳体实现
（rt_shell.c=.user 段,壳侧助手必须单 TU）。

**站50 slog 持久化+klog 双源+procfs 扩面（[140-145]——参考实现 slog/
log_file+seq_file 机制白描的站内形，批次 A 第二站）**：slogsrv 以
resmgr **客户端**身份走消息面写 /var/log（服务器互为客户端=微内核
正道，零内部 API 泄漏）：open/create（ramfs 元名连接）+SEEK END+
WRITE 追加（44B 窗分块）+STAT 尺寸影子；**双世代交替命名轮转**
（slog/slog.1，rotate=unlink 对侧+create 对侧=零拷贝；8MB 页池下
10MB×10 不可为）3.5KB 触发。klog 双源=站 49 elog 环增量收割入卷
（核心 `eshine_log_ring_delta` 游标 API+log_ring_lock/unlock 锁对——
绝对索引模型 phys(K)=K%SIZE，head=total mod SIZE 恒等式）；用户
slog WRITE 面 'S [X]' 前缀入卷；recv 1s 超时醒=周期 flush+用户写
piggyback。**boot 期往返惰性化**：slogsrv 的 ramfs 往返与 pm stdio
绑定窗同拍打散 FIFO 序=pm 间歇 bind 失败（busybox 全链挂死），
实测定谳；惰性开卷（首个客户端写/周期收割时）后 4/4 稳。
**深径前缀路由**（`rt_msg_name_prefix_open`：最长前缀逐级截短，
"/proc/meminfo"→"/proc"；既有 ramfs 元名回退语义不动）——深径
openat 从未真正路由过（站 40 v0 全失配回退 ramfs），procfs 文件面
的第一块基石。procfs OPEN 按路径分型（任务表既有/meminfo 页池账
/cpuinfo 双核行/interrupts 逐中断计数行——rt_irq_tab 新 count 域，
dispatch 逐中断递增）。logctrl 三脸（flush/stat/echo on|off）。
存量警告清账三处（sys_pipe frame/gic.h static→inline/rt_blk
include 序——"警告在,没人读"再犯即抓）。

**站49 eshine_log 日志契约（[134-139]——参考实现 log/ 三仓契约的站内形，
批次 A 首站）**：`common/eshine_log.{h,core.c}` 双树单源（方案 A：共享
核心+各侧弱符号后端钩子；冻结 ABI=级别枚举/模块结构/钩子签名；一源
两物=契约单源与构建隔离两立——**规则必须落 all: 之后，先于 all 的
规则劫持默认 goal，elog_be.o 静默不编而 make 报"已是最新"=站 49 战雷
第一枚**）。级别模型 Zephyr 对齐（ERR..TRC 数值越大越啰嗦，默认 INF）
+ LOG_MODULE_REGISTER 入 `eshine_log_mods` linker section（GNU ld
__start_/__stop_ 聚合，双侧 linker.ld KEEP；不依赖 constructor——
bare-metal 安全）+ 调用点宏级别检查内联（低于阈值零参数求值）。
后端：宿主=cntpct 时钟+console 行级锁窗整行+ring console 锁窗；
RTOS=cntvct 时钟+rt_puts_n（DAIF 行级）+ring 自旋锁窗——**参考实现的
"单写者 ring"假设在 4 CPU 下不成立，环完整性=后端锁窗内调 raw（站 20
实态修正）**。核心自带最小 vsnprintf（%d/%u/%x/%s/%c/%p/%%，l/ll/z
收纳；两侧本无 snprintf 面——house 手搓行传统的契约需要格式化面）。
32KiB 字节环覆盖最旧+logdump 倾倒；ratelimit 窗口+burst+suppressed
计数；panic 通道（无锁后端+banner+环倾倒+halt，代码完备无判据=记档
非目标——ESHINE_PANIC 会 halt 系统，首个真实消费者接入时验）。四命令
双侧（宿主直调/RTOS 经 sys_demo 蹦床 RT_U_LOG*）：logdemo（三级现身
+dbg/trc 默认滤除=禁现判据+ratelimit 3/10 面）/loglevel（全局与 per
模块调级）/logmods（段聚合模块表）/logdump（环倾倒+boot 行环审计）。
boot 自证：双侧 banner 后 LOG_INF "log core up, mods=2"（模块计数=
段聚合自证）。
**站48 批收口（[128]——批四面合卷健康面板+批发布注记）：
health=demo op RT_U_HEALTH 一屏（uptime tick/活任务/活进程/fp
轮转计数/cpart·mpart 在役数/crash 环记录数/xvm 窗 seq·ack——
43 观测/45 分区/46 崩溃/47 跨 VM 的自证合卷）;发布注记=
docs/release/2026-11-02-batch-43-48.md（版本号不动=发布节奏归
用户管辖）** →
**站34 yocto 门（[123]——meta-eshine 层入仓：git archive 提交树
出源 tar（参考实现机制镜像）+宿主交叉工具链经 HOSTTOOLS 白名单=
零补丁（参考实现六十补丁战争不重演）+一条 bitbake eshine-rtos 到
部署四件（eshine.elf/rtos.bin/guest2.bin/ramfs.cpio）；判据=
部署产物真实可靴——第二台 QEMU 吃部署物起 vm0 跑出 musl run，
非 parse-only）** →
**RTOS 迟退（第十六站：vm0 exit 留活跨过并发判据后收场）** →
vmkill 处决 + 宿主完整**，静态 marker 不算；连续 2 轮全绿才真绿）。
make run 与 make verify 同构（同一构建产物，-smp 4），shell help 内置
183 步手动走查清单。
**同步纪律（2026-10-24 用户令）**：每站门禁步数/判据变化时，
verify_el2.py、shell help 走查清单、本节文字必须同 commit 更新
——曾因 replace 静默失败出现"标题 31 正文 25"嵌合体，用户实测抓出；
2026-10-26 第二起：站十六 e87a3bb 宣称三处同步但 shell.c 走查从未
落盘（48 步陈表+version 陈行），站十七重写根治。
CI 在 .github/workflows/ci.yml，同步 GitHub 后生效。
