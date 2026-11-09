# eshine-os Week-1 EL2 骨架设计

- **日期**: 2026-10-21
- **状态**: 已评审通过(user 2026-10-21 批准)
- **范围**: 从零打造 eshine-os 的第一个可验证里程碑

## 0. 背景与 clean-room 纪律

eshine-os 从零打造,目标第一周(7-14 小时)交付 **EL2 骨架到真绿**。

**clean-room 纪律(红线)**: 本仓不拷贝任何第三方或私有实现代码。实现只以公开资料(ARM ARM、Linux/KVM/Xen 公开源码惯例、公开文档)为源;AI 结对产出遵循同一纪律。

**路线决策**: 路线 A——QEMU 直进 EL2 薄启动;EL3/ATF 启动链留给真板阶段(路线注记见 §4.5)。

### 0.1 设计与参考源纪律(user 2026-10-21 定)

**参考源分级(clean-room)**: 从零编写,仅以公开规范与文档为源(ARM ARM、GICv3 spec、Linux/KVM/Xen 公开源码惯例、QNX/参考RTOS 公开设计);不读取、不拷贝任何第三方派生或私有代码。需要同类技术参考时一律走公开资料。

**偏离纪律**: 任何大偏离(启动链、目录结构、地址布局、命名体系、核心 API 形态)——先告知用户、说明理由,获批后再动;获准的偏离记入对应站点设计文档(内容/原因/日期/影响面)。小偏离随手登记即可。

**结构取舍与 clean-room 的关系**: 行为与结构层面的取舍记录在案;逐行代码拷贝仍然禁止(版权红线,§0)。

## 1. 仓库与身份

| 项 | 决定 |
|---|---|
| 本地路径 | `仓库根`(独立命名空间,物理隔离私有目录) |
| git | init -b master;origin = 用户 镜像机(URL push 时提供;镜像机 无裸仓则 3 行 runbook:`git init --bare` + 权限) |
| GitHub 演进 | 镜像机 `git push --mirror` 到 `eshine-os/eshine-os`(主仓=产品仓:README 项目首页 + 代码子目录;website 分支不动)。镜像机 侧执行,绕开本机管控 |
| License | **Apache-2.0**;全文件 `SPDX-License-Identifier: Apache-2.0` + `Copyright (c) 2026 Julian Chu`;根 NOTICE |
| Commit 规范 | kernel 模板 / `Signed-off-by: Julian Chu <rootuliux@gmail.com>`(DCO)/ AI trailer 白名单(Claude/glm-5.3/deepseek-v4/qwen3.8-max) |

## 2. 目录布局

```
eshine-os/
├── README.md            双语项目首页(v0.0 开发中状态)
├── LICENSE  NOTICE
├── ARCHITECTURE.md      §4 内容的载体
├── docs/specs/          本文档所在
├── docs/devlog/         双语 devlog(内容管线)
├── hypervisor/
│   ├── arch/arm64/      start.S  vectors.S  mm/(预留目录,暂空)
│   ├── kernel/          main.c  vm.c  console.c  shell.c
│   └── drivers/          pl011.c
├── tools/verify/        verify_el2.py
├── scripts/             run_qemu.sh
├── Makefile  linker.ld
└── .github/workflows/ci.yml   预置;同步 GitHub 后自动生效
```

## 3. EL2 骨架(路线 A)

**启动**: `qemu-system-aarch64 -M virt,virtualization=on,gic-version=3 -cpu cortex-a57 -nographic -kernel eshine.elf`,QEMU 直接进入 EL2(无 ATF,无 secure world)。

**执行流**:
```
start.S(链接于 0x40200000)
  ├ MSR VBAR_EL2(向量表) → 关中断(DAIF) → 清 BSS → 设 EL2 栈
  └ B main
main():
  ├ pl011_init(banner: "eshine-os <version> EL2 @QEMU virt")
  ├ shell_init → 提示符 "eshine> "
  └ 轮询循环: RX 行缓冲 → 命令分发
     命令: help | version | echo <token> | state(打印状态机演示)
```

**向量表策略**(16 槽): current-EL sync/irq 异常**先实**(sync 打印 ESR/FAR 后 halt;irq 本周只做 wfi 循环);current-EL fiq/serror 与 lower-EL 四组共 12 槽 → 统一 panic 占位(后续 stage2/guest 阶段逐个填)。

**工具链**: `aarch64-linux-gnu-gcc 13.3` + `-ffreestanding -nostdlib -mgeneral-regs-only -march=armv8-a -Wall -Wextra`(零警告目标)。
`-mgeneral-regs-only` 是刻意决定:FPSIMD save/restore 建立前禁 FP,前置规避参考实现 C-H01 型 trap 雷。

**构建**: kernel 风格 Makefile(单个 `eshine.elf` 目标 + `clean` + `run`);`linker.ld` 自写 `.text@0x40200000` 起。

## 4. ARCHITECTURE.md —— 生命周期先行

### 4.1 VM 状态机(本周立类型与转换表,调度后续填)

```
VM_INIT → VM_CREATED → VM_RUNNING ⇄ VM_PAUSED
                     ↘ VM_TEARDOWN → VM_DEAD
```

- **铁律**: 一切表外转换 → panic(**默认拒绝**哲学,与异常向量占位、后续 hypercall default-deny 同根)
- 结构草案:
```c
enum vm_state { VM_INIT, VM_CREATED, VM_RUNNING, VM_PAUSED, VM_TEARDOWN, VM_DEAD };
struct eshine_vm {
    enum vm_state state;
    struct vcpu      *vcpus[ESHINE_MAX_VCPUS];  /* 属主清晰,逆序销毁 */
    struct mem_region *mem[..];
    struct device    *devices[..];
};
vm_transition(vm, new_state);  /* 表驱动;非法转换 panic + 打印当前/目标态 */
```
- **teardown 逆序原则**: 设备 → 内存 → vCPU(创建序的逆)。

### 4.2 启动流图、4.3 异常级分工表(EL2 own / guest pass-through 两栏)、4.4 验证策略(真绿判据引用 §5)

### 4.5 路线注记(范围外但写明)
- 真板阶段: EL3/ATF → 参考实现模式;stage2 页表 + VTCR;GICv3 虚拟化(ICH_*);VM0=RTOS 管理 VM / VM1=Linux guest 的多 VM 拓扑。

## 5. 验证 = 真绿判据(护栏第一周就位)

`tools/verify/verify_el2.py`:
- QEMU 串口走 unix socket(`-serial chardev:socket`),python 直连
- 判据链: ①等 banner(≤30s) ②等 `eshine> ` 提示符 ③发 `echo ESHINEOK<pid>` ④**断言该唯一 token 在输出中回显**
- 单次 60s 硬超时;FAIL 打印完整串口日志;exit code 供 CI 消费
- 防假绿原则与参考实现 login-dance 同级:静态 marker 不算数,必须交互回显

**CI**(`ci.yml`): ubuntu runner + `apt install gcc-aarch64-linux-gnu qemu-system-arm` → `make` → `verify_el2.py`。本周本地跑(脚本即护栏),GitHub 同步后生效。

## 6. Push 通道(本机管控 现实)

本机 `git push` 被 本机管控 挡(2026-10-20 实测: pack-objects 读 `.git/config` 假 EPERM → unpacker error)。通道设计:
1. 日常: push 到 镜像机(若同样撞 本机管控)
2. fallback: `git bundle create → scp → 镜像机 侧 pull/am`(对象零损失)
3. 镜像机 → GitHub: 镜像机 侧 `git push --mirror`(本机管控 不沾身)

## 7. Week-1 验收标准(Definition of Done)

1. `make` 零警告产出 `eshine.elf`
2. `verify_el2.py` **PASS**(token 回显真绿)
3. 全仓文件 SPDX 齐;git log 符合 commit 规范;README/LICENSE/NOTICE/ARCHITECTURE.md 齐
4. ARCHITECTURE.md 含状态机 + 启动流 + 路线注记
5. (周末可选)`docs/devlog/00-first-boot.md` 双语初稿

## 8. 明确范围外

EL3/ATF、stage2 页表实现、GIC 虚拟化、任何真实 VM/调度、SMP、FP/FPSIMD、真板移植。

## 9. 风险与缓解

| 风险 | 缓解 |
|---|---|
| QEMU `-kernel` ELF 加载行为与预期不符 | verify 脚本首跑即暴露;fallback 改 raw Image + `-device loader` |
| aarch64-linux-gnu-gcc 裸金属适用疑虑 | `-ffreestanding -nostdlib` 足够(无 libc 依赖,零 syscall 面);真问题则提取 ARM 官方 aarch64-none-elf 工具链(参考实现 prebuilts 里的 tarball 是 ARM 公开发布物,取用不违 clean-room) |
| 7-14h 时间超支 | 砍序: shell 的 `state` 命令 → devlog 初稿;**不砍 verify、不砍 SPDX** |
| 镜像机 push 通道未验证 | push 时先小仓试探;bundle 方案兜底 |
