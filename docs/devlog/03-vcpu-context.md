# Devlog 03 — vCPU 完整上下文切换(陷阱帧 + yield 断点续跑 + fault 隔离)

日期:2026-10-22 · 版本:27.04-dev · 门禁:17 步真绿 ×2 连过

## 里程碑

第二站记账的三笔债一次清偿,guest 现场成为一等对象:

    vm0> selftest              → selftest OK(x19/x30 跨 100 次 HVC 求和 4950)
    vm0> yield                 → vm0: yielded -> PAUSED
    eshine> vmls               → [0] VM_PAUSED
    eshine> vmrun 0            → vm0: resumed (breakpoint) → resumed → vm0>
    vm1> fault                 → fault: ESR=0x93c08006 FAR=0x30000000
                                 pc=.. x0=0x30000000(帧内野指针原值!)
                                 → vm1: faulted -> VM_DEAD → 宿主 echo 照常活

三相判据全部由 17 步门禁固化:selftest(寄存器完整性)、yield→PAUSED→
resumed(断点续跑)、fault(隔离:诊断+VM 死+宿主无损)。

## 实现形态(spec 293fefe → 计划 484c4f4 → 5 任务)

- **陷阱帧**:`struct vcpu = gprs[31]+sp+pc+pstate + host_ctx + exit_reason`,
  偏移即 asm 契约;`entry_*` 字段删除,帧即真相(新跑=vm_create 初始化帧,
  yield 后帧=断点,一条进入路径)
- **el1_trap 桩**:停 x0 于 excp_stack 引导(x18-x28 属 guest 禁作 scratch)
  → 全 GPR 入帧 → SP_EL2 归位栈顶 → el1_sync(struct vcpu*)
- **三原因码**:VCPU_EXIT/YIELD/FAULT;vcpu_run 返回码由 vm_run 分派,
  状态机零新转换(PAUSED 首次启用,Week-1 生命周期先行的红利兑现)
- **EL2 handler 从此自由用任何寄存器** — 第二站"编译器巧合"债务清零

## 途中抓到的三个真雷(全部实证)

1. **noreturn 声明反杀**:`vcpu_enter_guest` 标了 noreturn,但 retire 路径
   真的会返回 —— GCC 在调用点后不生成任何代码,首次 exit 落进死区挂死。
   语义边界:noreturn 属于 eret 出去的 trap_resume,不属于会还栈返回的
   进入口。
2. **SP_EL2 每陷阱净泄漏(本轮最大发现)**:trap_resume 直接 eret、
   handler 链不退栈 —— SP_EL2 无人归还。每次 HVC 泄 ~110B,第 79 发
   陷阱(VM0 全往返 + VM1 banner 第 37 字符)确定性碾穿 4KB 异常栈底,
   踩 cur_vcpu(栈底下方 16B)→ 下一发读出全零帧 → guest 落 PC=0 →
   宿主 ret 也进垃圾。第二站没炸纯因门禁陷阱总数(~84×32B)未越 4KB
   余量。修复:el1_trap 每次入口把 SP_EL2 归位栈顶(异常入口归一化
   自己的栈,handler 允许弃帧)。**教训:任何"不退栈"的 eret 路径,
   栈归位必须发生在入口,不能指望出口。**
3. **selftest 常量折叠**:0-99 求和被 GCC -O2 折成常量 4950,判据失效
   (循环体零累加指令)。空 asm `"+r"(sum)"` 屏障挡折叠后,x19/x30 落点
   实证(反汇编 `add x30,x30,x19` 跨 hvc)—— 寄存器完整性判据才真活。

## 参考实现对齐(结构参考,clean-room 纪律不变)

探索代理扫了参考实现(arch/arm64)的 trap 上下文形态:

**结构一致仅命名偏离**:参考实现 `cpu_regs.regs[30]+lr+sp+elr+spsr` ≈ 新仓
`gprs[31]+sp+pc+pstate`(信息等价,x30 命名 lr 而已);asm 桩+C handler
两级形态(`handle_vmexit_early/late→bl handler→__vmreturn`)与新仓
`el1_trap→el1_sync→trap_resume` 完全同构;`callee_saved_regs` ≈ host_ctx;
WFI-trap-yield ≈ HVC#2-yield(语义同,机制不同)。

**真结构差异(站点设计已登记)**:
1. **帧归属层级**:参考实现帧=宿主线程栈顶钉住(`cpu_regs_ctx`,经 osx_tcb
   间接引用),GPR 与 sysreg(spe_ctx/vgic/vtimer)分置;新仓帧内嵌 vcpu
   结构,尚无线程/调度间接层 —— 第三站形态是"无调度器的帧",参考实现是
   "RTOS 线程化的帧",后续调度站自然引入该层时再对齐。
2. **原因码收敛**:参考实现无统一 exit_reason(ESR_EC 分发+trap_return 三值+
   guest_err_type×action 策略矩阵+EXP_FLAG 统计四层拼合);新仓收敛为
   EXIT/YIELD/FAULT 三码显式通道。
3. **fault 策略粒度**:参考实现按域 manifest 三态(IGNORE/INJECTBACK/SHUTDOWN,
   含 VBAR=0 强制关停防注入死循环 + 安全栈错误处理);新仓单一 FAULT→
   隔离死,策略矩阵是后续演进位。

## 下一步

调度站(多 vCPU/抢占,yield 升级为调度点)或 GICv3 虚拟化(ICH_*,
中断注入让 WFI 的 guest 真正醒来)。参考实现的 WFI-trap 调度形态
(vcpu_wfi_emulate)已备案为结构参考。
