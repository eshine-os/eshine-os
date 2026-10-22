<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 27.04 第三站设计:完整 vCPU 上下文切换(寄存器完整性 + yield 断点续跑 + guest fault 隔离)

- **日期**: 2026-10-22
- **状态**: 已评审通过(user 2026-10-22 批准六节设计)
- **前置**: 第二站(794bbdd:双 VM + 14 步门禁全绿)
- **还债**: devlog 02 记账 —— guest x18-x28 跨 HVC 存活靠"EL2 handler 编译后
  恰好不碰 callee-saved"的巧合;非 HVC guest 异常 panic 整个宿主

## 0. clean-room 与参考源

同前两站纪律:ARM ARM(DDI 0487)与 QEMU 文档为唯一参考源,不读任何参考实现代码。
trap-and-emulate 帧结构是 Xen/KVM 公开形态的常识级结构,细节以 ARM 手册为准。

## 1. 目标与非目标

**目标**: ① guest GPR x0-x30+sp+pc+pstate 全量进 vcpu 陷阱帧,HVC 往返寄存器
完整(EL2 handler 行为与 guest 无关);② HVC #2 yield:guest 主动让出,VM 转
PAUSED,vmrun 断点续跑;③ 非 HVC guest 异常:保存现场+诊断输出+VM 干净死亡,
宿主无损("坏 guest 杀不死 hypervisor")。

**非目标(本站不做)**: 多 vCPU 并发与调度、GICv3 中断注入、时钟/定时器、
guest 异常递送(guest 侧向量表)、EL1 stage-1。

## 2. vcpu 结构 = 陷阱帧 + 宿主现场(asm 硬依赖偏移)

```c
/* kernel/include/vcpu.h — 偏移是 asm 桩契约 */
struct vcpu {
    u64 gprs[31];        /* x0-x30     @0    (guest 全量 GPR) */
    u64 sp;              /* SP_EL1     @248 */
    u64 pc;              /* ELR_EL2    @256 */
    u64 pstate;          /* SPSR_EL2   @264 */
    struct {             /* host_ctx   @272  (第二站已建,不动) */
        u64 x19, x20, x21, x22, x23, x24, x25, x26, x27, x28;  /* @272-352 */
        u64 sp;          /* @352 */
        u64 lr;          /* @360 */
    } host_ctx;
    u64 exit_reason;     /* @368 退回宿主时的原因码 */
};
```

- `entry_pc/entry_sp/spsr` 字段删除,**帧即真相**:vm_create 初始化帧
  (GPR 清零,pc/sp/pstate=入口);yield 后帧即断点;新跑/续跑同一路径
- 原因码(`kernel/include/vcpu.h` 枚举):`VCPU_EXIT=0`(HVC #1)、
  `VCPU_YIELD=1`(HVC #2)、`VCPU_FAULT=2`(非 HVC 异常)

## 3. asm 桩(vectors.S + vcpu_entry.S 重构)

```
el1_trap(向量槽 lower-EL AArch64 sync 入口):
    str x0, [sp, #-16]!        /* 停 guest x0;SPSel=1 → sp=excp_stack,合法 */
    ldr x0, =cur_vcpu → ldr x0,[x0]
    stp x1,x2..x29,x30 入帧    /* x1-x30 先存,x0 之后回填 */
    ldr x1,[sp],#16; str x1,[x0]          /* guest x0 回帧 */
    mrs sp_el1/elr_el2/spsr_el2 → 入帧
    bl el1_sync                /* x0 = vcpu */
trap_resume(x0=vcpu, noreturn):           /* el1_sync 续跑路径调用 */
    msr sp_el1/elr/spsr ← 帧(x0 作基址,x1 作值暂存)
    ldp x1..x30 ← 帧 → ldr x1,[x0]; mov x0,x1   /* x0 最后回填 */
    eret
vcpu_enter_guest(x0=vcpu, 不返回):        /* 取代 vcpu_eret_enter */
    存宿主现场进 host_ctx(现第二站逻辑) → 加载帧(同 trap_resume 尾部) → eret
```

要点:x18-x28 属 guest,桩内禁用作 scratch;cur_vcpu 由 static 改全局可见
(vcpu_entry.S 引用)。**从此 EL2 handler 用什么寄存器与 guest 无关。**

## 4. el1_sync 新语义与 vcpu_retire_host

```c
void el1_sync(struct vcpu *v);   /* x0=vcpu,由桩调入 */
```

| ESR | 动作 |
|---|---|
| HVC #0 | console_putc(v->gprs[0]); v->gprs[0]=0; trap_resume(v) |
| HVC #1 | v->exit_reason=VCPU_EXIT; vcpu_retire_host(v) |
| HVC #2 | v->exit_reason=VCPU_YIELD; vcpu_retire_host(v) |
| 非 HVC | v->exit_reason=VCPU_FAULT; fault_dump(v); vcpu_retire_host(v) |

- `vcpu_retire_host(v)`:清 HCR/VTTBR + tlbi vmalls12e1is + 还宿主
  (现 vcpu_exit 逻辑三种原因共用;续跑时 vcpu_run 全量重武装)
- `vcpu_run(v, vttbr)` 返回 exit_reason(u64)
- `fault_dump(v)` 诊断输出(一次性,VM 濒死):VM id、ESR_EL2、FAR_EL2、
  帧内 pc/sp、帧内 x0-x3。EC 粗解码注释级(sync/data-abort/instr-abort/HVC)
- 宿主自身异常路径(el2_sync/el2_unexpected/el2_irq)不变:仍打印+WFI
  —— 默认拒绝边界不动

## 5. VM 层:状态机零新转换,PAUSED 首次启用

- vm_run 分派 exit_reason:YIELD → RUNNING→PAUSED,打印
  `vm0: yielded -> PAUSED (vmrun to resume)`;EXIT/FAULT →
  TEARDOWN→DEAD,FAULT 打印 `vm0: faulted -> VM_DEAD`
- vm_run 入口:CREATED(新跑)或 PAUSED(续跑)皆可;续跑打印 `(resumed)`
- vmkill:CREATED 或 PAUSED 皆可杀(PAUSED→TEARDOWN 表内已有)
- vm_create:改初始化帧(v->pc/sp/pstate;GPR 清零)
- 状态表 allowed[][] 完全不动 —— Week-1 生命周期先行红利的兑现时刻

## 6. guest 侧(同源双镜像共用,双身份不变)

`guest/hvc.h` 新增 `hvc_yield()`(HVC #2;同 x1-x17 易失 clobber 语义)。
`guest/main.c` 新命令:

- `selftest`:0-99 求和,计数器放跨 hvc_putc 存活的 callee-saved
  (putc 的 clobber 列表迫使活跃值落 x19-x28),逐位 putc,校验 4950,
  打印 `selftest OK`(失败打 FAIL)—— 寄存器完整性的直接判据
- `yield`:打印 `yielding...` → hvc_yield();续跑后打印 `resumed` 回提示符
- `fault`:`*(volatile u64 *)0x30000000` 读 → 必然 stage-2 L2 翻译错误
  (pgd[0] 段内 l2_dev[0x180] 为 0)→ VM 干净死,永不返回

## 7. 门禁 17 步(tools/verify/verify_el2.py 重排)

| # | 判据 |
|---|---|
| 1-3 | banner + prompt + 宿主 token 双回显(不变) |
| 4-5 | vmls(INIT,INIT) + vmcreate 0 → CREATED |
| 6 | vmrun 0 → VM0 banner → `selftest OK`(寄存器完整性) |
| 7 | VM0 token 双回显 |
| 8 | `yield` → `vm0: yielded -> PAUSED` + 宿主 prompt |
| 9 | vmls → `[0] VM_PAUSED` |
| 10 | vmrun 0 → `resumed` + `vm0>`(断点续跑) |
| 11 | exit → `vm0: VM_DEAD` |
| 12 | vmcreate 1 + vmrun 1 → VM1 banner |
| 13 | `fault` → 固定诊断行 `vm1: fault: ESR=0x.. FAR=0x.. pc=0x.. sp=0x..` → `vm1: faulted -> VM_DEAD` + 宿主存活(gate 判据:`fault: ESR`) |
| 14 | vmls → DEAD,DEAD |
| 15 | 宿主完整性 token 双回显 |
| 16 | vmkill 拒绝路径(DEAD 槽 → kill denied) |
| 17 | vmcreate 0 + vmkill 0(PAUSED 前的 CREATED 杀)→ killed before run + vmls 复核 |

连续 ×2 全过 + 手工全流程;构建零警告。沿用门禁工程纪律:read_until 一律以
发送时刻 buf 长度为下界,命令尾随 prompt 必须吃掉后再发下一条。

## 8. 风险与预判

- **帧偏移契约**:vcpu.h 偏移即 asm 契约,实现时以 objdump 校验
  (cur_vcpu 存帧指令序列的 #imm 偏移 vs 头文件)
- **SP_EL1 双写**:enter_guest 与 trap_resume 都 msr sp_el1 —— 幂等,无冲突
- **yield 后 stage-2 状态**:retire_host 已清 HCR/VTTBR;续跑 vcpu_run 重装
  (VTCR/VTTBR/HCR/SP/SPSR 每次全量写)—— 语义等价"冷恢复",无隐式状态
- **selftest 计数器落点**:GCC 把跨 asm-clobber 调用的活跃值放 x19-x28
  (x18 默认不分配);若实测编译器把计数器 spill 到栈,selftest 仍测不出帧
  完整性 —— 计划里加反汇编确认计数器寄存器落点(非栈)
- **两 guest 共享 UART**:fault 让 VM1 死于读、非写,不污染 UART 状态
