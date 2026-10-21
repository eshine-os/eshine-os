# Devlog 02 — VM 抽象落实(双 VM + 生命周期管理)

日期:2026-10-22 · 版本:27.04-dev · 门禁:14 步真绿 4 连过

## 里程碑

第一站的"单 guest 往返"升级为真正的 VM 抽象:shell 变小型 VM 管理器,
两个同源镜像各自在自己的地址空间里跑,证明同 IPA 不同 PA 的隔离:

    eshine> vmls
    VM table:
      [0] VM_INIT
      [1] VM_INIT
    eshine> vmcreate 0
    vm0: CREATED (ram 0000000050000000, entry 0000000040000000)
    eshine> vmrun 0
    vm0: entering EL1 via stage-2 (non-identity map)
    eshine-guest VM0 @EL1 via stage-2 (HVC console)
    eshine-guest VM0 UART @IPA 0x09000000 (stage-2 device page)
    vm0> echo GOK<pid>
    GOK<pid>
    vm0> exit
    vm0: VM_DEAD
    eshine> vmcreate 1
    ... 同 IPA 0x40000000,PA 窗口 0x51000000,VM1 自己的身份 ...
    vm1: VM_DEAD
    eshine> vmls
    VM table:
      [0] VM_DEAD  ...
      [1] VM_DEAD  ...

交付面:eshine_vm 实体内嵌(vm_table[2],零堆)、per-VM stage2 构建器
(配置校验、VMID 8-bit 进 VTTBR)、状态表 +2 条(CREATED→TEARDOWN、
DEAD→INIT)、vmls/vmcreate/vmrun/vmkill、同源双镜像(-DGUEST_ID)、
14 步门禁(含 vmkill 拒绝路径负向判据)。VM0 的 RAM 窗口
(0x50000000-0x50FFFFFF)与 VM1(0x51000000-0x51FFFFFF)不重叠——
VM0 无法经自身映射摸到 VM1 的物理页,隔离由映射层物理保证。

## 执行中发现并修掉的三个真问题

1. **栈分配 eshine_vm 即栈溢出**:实体内嵌 vm_space 后 eshine_vm ~20KB,
   宿主栈只有 16KB。run_guest 的栈局部 eshine_vm 一开帧就冲破栈底,
   把 boot 期静态 stage2 表清零(guest 首取指即 L1 翻译错误)。
   过渡期改静态,Task 4 整体删除。教训:**含大数组的结构体进栈前先看栈**。

2. **vm_space 表必须各自独立 4KB 对齐页**(gdb 实证):DSC_TABLE 掩码
   砍地址低 12 位,未对齐的 l2_dev(&vm_space 内紧贴 pgd[4] 之后)被折回
   pgd 页首——pgd[0] 指向 pgd 自己。首个 per-VM fetch 挂在 L2 翻译
   错误。第一站的静态表每张独立 aligned(4096) 才是它工作的原因。
   修复:vm_space 每成员独立 aligned(4096)。教训:**指针掩码类描述符
   的表,对齐是正确性的一部分,不是优化**。

3. **host_ctx 必须是完整宿主现场**(x19-x28 + sp + lr):第一站只存
   sp/lr。vcpu_run 的 epilogue 在 ERET 前恢复 callee-saved,但 ERET 后
   guest 覆盖一切;ret 回宿主时寄存器里是 EL2 异常链的残留——
   vmrun 打印 id 时输出了 HVC exit 的 ESR(0x5A000001)。
   第一站没炸只是调用方恰好只依赖 adrp 重载的字符串地址。
   修复:vcpu_eret_enter/return_host 整块保存/恢复 x19-x28。

## spec 两处工程修正(执行时定案)

- mem_region/vm_device/vm_space 类型放 mm.h(spec §2 画在 vm.h):
  避免 vm.h↔mm.h 循环包含,保持 vm.h→mm.h 单向
- 状态表除 CREATED→TEARDOWN 外补 DEAD→INIT(spec §4 漏了槽位复用)

## 门禁工程

14 步门禁本身修了两处:每个 read_until 以发送时刻的 buf 长度为下界
(拒绝陈旧匹配);每条命令的尾随 prompt 在下一条发送前必须吃掉
(VM_DEAD 后的 prompt 与 vmls 检查竞争,曾致 [13] 假失败)。修复后
4 连过。

## 架构债(记录,下一站偿)

- **guest 寄存器态**:x18-x28 跨 HVC 存活目前靠"EL2 handler 编译后
  恰好不触碰 callee-saved"。完整的 vCPU 上下文切换(guest 侧 x0-x30
  入 vcpu 结构)是下一站必做——GICv3/调度都压在这之上。
- 分配器:vm_space/vcpu 由内嵌换动态时,指针数组 ABI 不变,只换 backing。

## 下一步候选

完整 vCPU 上下文切换 → GICv3 虚拟化(ICH_*)→ 调度(轮转 VM0/VM1)
→ 分配器。路线详见 docs/specs/2026-10-22-vm-abstraction-design.md 与
ARCHITECTURE.md 路线注记。
