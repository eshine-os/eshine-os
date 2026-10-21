<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 27.04 第二站设计:VM 抽象落实(双 VM + 生命周期管理)

- **日期**: 2026-10-22
- **状态**: 已评审通过(user 2026-10-22 批准五节设计)
- **前置**: 第一站(648f040:stage2 + 首个 guest 往返 + 8 步门禁)

## 0. clean-room 与参考源

与第一站同纪律:实现以 ARM ARM(DDI 0487)/QEMU virt 公开文档为源,不读参考实现
任何代码。VM 抽象的形态是全新设计(参考实现的 vm 框架不参考);与参考实现的
结构对齐点仅一条且继续成立:**生命周期先行,默认拒绝**。

## 1. 目标与非目标

**目标**: struct eshine_vm 的 vcpus/mem/devices 从空壳变实体;stage2 从模块
级单套静态表变 per-VM 生成(VMID 隔离);shell 变小型 VM 管理器
(vmls/vmcreate/vmrun/vmkill);两个 guest 镜像经各自 stage-2 映射运行,
证明同 IPA 不同 PA 的地址空间隔离。

**非目标(本站不做)**: 分配器/堆、多 vCPU 与调度、异步 kill、GICv3、
EL1 stage-1 双阶段翻译、运行时改映射。

## 2. 数据模型(vm.h / vm.c)

```c
struct mem_region { u64 ipa, pa, size, attr; };  /* attr = S2_ATTR_* */
struct vm_device  { u64 ipa, pa, size, attr; };  /* 语义上与 RAM 分离:将来挂 IRQ/XN 策略 */
struct eshine_vm {
    enum vm_state state;
    u64 vmid;                                    /* VTTBR.VMID,8-bit */
    u32 n_mem, n_dev;
    struct vcpu       vcpus[ESHINE_MAX_VCPUS];   /* 本站每 VM 1 个 */
    struct mem_region mem[ESHINE_MAX_MEM];
    struct vm_device  devices[ESHINE_MAX_DEV];
    struct vm_space  space;                      /* arch 侧 per-VM stage2 全套表 */
};
static struct eshine_vm vm_table[ESHINE_MAX_VMS]; /* = 2,静态全量内嵌 */
```

- 槽位生命周期即 VM 生命周期:vmcreate 拉起 INIT→CREATED 并填配置;DEAD
  槽位可再次 vmcreate(复用)
- **演化路径**:引入分配器的后续站,只需把内嵌数组换成指针,builder 与命令层
  不动

### 预定义配置 vm_configs[2]

| VM | RAM(IPA→PA,16MB,attr NORMAL) | 设备(IPA→PA,4KB,nGnRE+XN) | entry |
|---|---|---|---|
| 0 | 0x40000000 → 0x50000000 | 0x09000000 → 0x09000000 (UART) | pc=0x40000000, sp=0x40200000, spsr=0x3C5 |
| 1 | 0x40000000 → 0x51000000 | 同上 | 同上 |

**PA 窗口不重叠**(VM0:0x50000000-0x50FFFFFF,VM1:0x51000000-0x51FFFFFF):
VM0 无法经自身映射访问 VM1 镜像物理页——隔离是映射层的物理事实,非约定。

## 3. per-VM stage-2(arch/arm64/include/mm.h + mm/stage2.c 重构)

```c
struct vm_space {
    u64 pgd[4]   __attribute__((aligned(4096)));  /* T0SZ=32 → 4 个 1GB 项 */
    u64 l2_dev[512];        /* IPA 0-1GB 段的 L2 */
    u64 l2_ram[512];        /* IPA 1-2GB 段的 L2 */
    u64 l3_dev[512];        /* 设备页 L3 */
};
int stage2_build(struct vm_space *sp, u64 vmid,
                 const struct mem_region *mem, u32 n_mem,
                 const struct vm_device *dev, u32 n_dev);
```

- 拓扑与第一站一致:pgd[0]→l2_dev、pgd[1]→l2_ram;RAM 走 2MB block(16MB=8 项),
  设备页 l2_dev[0x48]→l3_dev[0]
- 描述符规则沿用第一站(AF bit10 铁律、RAM MemAttr=0xF、设备=0x1+XN)
- **stage2_init/s2dump/stage2_walk/boot 期静态表移除**——stage2.c 变纯构建器
  + 配置校验(region 越界/未 2MB 对齐/表满 → 返回错误码,调用方礼貌拒绝)
- `vcpu_run(struct vcpu *v, u64 vttbr)` 加 vttbr 参数(= pgd 物理 | vmid<<48);
  tlbi vmalls12e1is 在切换/进入时冲全 VMID,切 VM 安全

## 4. 生命周期与命令(shell.c + vm.c)

```
vmls            列表: [0] VM_DEAD  [1] VM_INIT(状态名 + entry + PA 基址)
vmcreate <id>   INIT→CREATED:填配置、stage2_build
vmrun <id>      CREATED→RUNNING→(guest HVC exit)→TEARDOWN→DEAD
vmkill <id>     CREATED→TEARDOWN→DEAD(未运行先销毁)
```

- 状态表扩展一条:**CREATED→TEARDOWN**(vmkill 通道)
- **run_guest 命令移除**(被 vmcreate+vmrun 取代,门禁同步)
- 错误语义分层:命令层用户错误礼貌拒绝(活槽 vmcreate / 非 CREATED vmrun /
  RUNNING vmkill"无异步杀" / 坏 id),**vm_transition 保持表外即 panic**
  (内部不变量护栏,默认拒绝哲学不变)
- state 演示命令保留

## 5. guest 同源双镜像(guest/ + Makefile)

- 同一套 start.S/main.c/miniuart.h,`-DGUEST_ID=0/1` 产出 guest1.bin/guest2.bin
- 身份差异仅两处:banner("eshine-guest VM0 @EL1..." / "VM1")与提示符
  ("vm0> " / "vm1> ",门禁判据天然可分)
- QEMU loader 双装载 0x50000000/0x51000000;两个镜像各自"独占" IPA 0x40000000

## 6. 门禁 14 步(tools/verify/verify_el2.py 扩展)

| # | 判据 |
|---|---|
| 1-3 | banner + prompt + 宿主 token 双回显(不变) |
| 4 | vmls → [0] INIT [1] INIT |
| 5 | vmcreate 0 + vmls → [0] CREATED |
| 6 | vmrun 0 → guest banner 带 "VM0" 身份 |
| 7 | VM0 内 GOK token 双回显 |
| 8 | exit → "vm0: VM_DEAD" |
| 9 | vmcreate 1 + vmrun 1 → guest banner 带 "VM1" 身份(隔离实证) |
| 10 | VM1 内 GOK token 双回显 |
| 11 | exit → vm1: VM_DEAD |
| 12 | vmls → [0] DEAD [1] DEAD |
| 13 | 宿主完整性 token 双回显 |
| 14 | vmkill 拒绝路径:vmkill 已 DEAD 槽 → 拒绝消息(负向判据) |

连续 ×2 全过 + 手工全流程目检;构建零警告(含两 guest 镜像)。

## 7. 风险与预判

- **vm_space 内嵌于 vm_table 的 4KB 对齐**:GCC 成员 aligned 属性在静态
  存储被遵守(第一站单表同机制),构建后 objdump 验证一次
- **VTTBR.VMID 8-bit**:vmid∈{0,1},无溢出面
- **两 VM 共享 UART**:仅顺序运行,无争用;异步并发留待调度站
- **镜像驻留**:QEMU boot 时已装载,vmcreate 不搬运(未来分配器站再议
  image load)
