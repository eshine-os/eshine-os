<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
<!-- 知乎/公众号深度长文(共用底稿,发布时按平台微调,见文末发布说明) -->

# 从零写一个开源 hypervisor：第一晚，真绿

> eshine-os 26.10 发布记 —— 一个人、每天 1-2 小时、与 AI 结对，从第一行代码到第一个正式版。

## 先看结果

今晚（准确说是一个晚上，约四五个小时的高强度人机结对），eshine-os 的第一个正式版 **26.10** 已经推上 GitHub：

- 仓库：https://github.com/eshine-os/eshine-os（tag `v26.10`）
- 官网：https://www.eshine-os.org（kernel.org 风格）
- 528 行 C/汇编、18 个源文件，Apache-2.0，每个文件带 SPDX

```
eshine-os 26.10 (EL2) @ QEMU virt
eshine> state
VM state machine (lifecycle first):
  VM_INIT -> VM_CREATED -> VM_RUNNING <-> VM_PAUSED
  VM_RUNNING/VM_PAUSED -> VM_TEARDOWN -> VM_DEAD
  any other transition: panic (default-deny)
demo walk: VM_CREATED VM_RUNNING VM_PAUSED VM_RUNNING VM_TEARDOWN VM_DEAD
```

这不是 demo 视频里那种"看起来能跑"——它有一个**真绿门禁**：每次提交，脚本会在 QEMU 里把系统真正启动，等 banner、等 shell 提示符，然后发一个带进程号的特征串 `echo ESHINEOK12345`，**必须看到它被回显两次**（终端回显一次 + 命令执行输出一次）才算 PASS。静态字符串不算数——因为静态字符串会骗人。

## eshine-os 是什么

一句话：**面向高可靠、高安全场景的开源基础操作系统**（车载自动驾驶、机器人、医疗），建立在 ARM64 虚拟化之上。口号是"底层之光，守护生命"。

目标架构（现在还只走到第一步）：

```
+--------------------------------------+
|  ARM64 硬件                          |
+--------------------------------------+
|  Type-1 Hypervisor (EL2)             |   ← 26.10 在这里(骨架)
+----------------+---------------------+
|  VM0: RTOS     |  VM1: Linux guest   |   ← 27.04 起逐步
|  (管理域)      |  (后续 VM2/VM3)     |
+----------------+---------------------+
```

为什么从零写？两个原因。一是**版权干净**：全部代码从公开规范直接撰写（ARM 手册、QEMU 文档），单一 Apache-2.0，没有一个"来历不明"的字节。二是**设计哲学换血**：很多 hypervisor 的通病是 VM 生命周期靠后补——没有状态机、销毁路径缺失。eshine-os 反过来，**生命周期先于一切功能**：第一版的功能几乎为零，但 VM 状态机的类型和转换表已经是地基，而且遵循一个原则：**一切表外状态转换，直接 panic**。默认拒绝。这和它异常向量表 16 槽里 12 槽是"默认拒绝占位"是同一个哲学。

## 第一晚的技术内容

### 1. 直进 EL2，不要 ATF

QEMU 的 `-M virt,virtualization=on` 允许固件之外最高特权级是 EL2，于是：

```
qemu-system-aarch64 -M virt,virtualization=on,gic-version=3 \
  -cpu cortex-a57 -nographic -kernel eshine.elf
```

`eshine.elf` 的入口直接落在 EL2（0x40200000），省掉整个 EL3/ATF 启动链。**这不是偷懒，是范围管理**：仿真阶段不需要 secure world，真板阶段再补 EL3 链路——设计文档里明确写了这条路线注记。

入口代码做四件事：确认自己真在 EL2（不是就 wfi 停车，拒绝带病运行）→ 关中断 → 清 BSS → 装 `VBAR_EL2` 向量表、设栈，然后跳 C。

### 2. 16 槽向量表：12 个槽位是刻意的"默认拒绝"

ARM64 异常向量表有 16 个槽。26.10 只实装了 current-EL 的 sync（打印 ESR_EL2/FAR_EL2 后停车）和 irq（wfi），**剩下 12 个槽全部落进同一个默认拒绝处理器**——任何不请自来的异常，报 ESR、死给你看。宁可 crash 得明明白白，不默默吞掉。

### 3. VM 状态机（本文最重要的 100 行）

```c
/* allowed[current][next] — 唯一合法转换集 */
static const bool allowed[VM_STATE_MAX][VM_STATE_MAX] = {
    [VM_INIT]     = { [VM_CREATED]  = true, },
    [VM_CREATED]  = { [VM_RUNNING]  = true, },
    [VM_RUNNING]  = { [VM_PAUSED]   = true, [VM_TEARDOWN] = true, },
    [VM_PAUSED]   = { [VM_RUNNING]  = true, [VM_TEARDOWN] = true, },
    [VM_TEARDOWN] = { [VM_DEAD]     = true, },
};

bool vm_transition(struct eshine_vm *vm, enum vm_state next)
{
    if (next >= VM_STATE_MAX || !allowed[vm->state][next])
        panic("vm_transition: illegal state transition");
    vm->state = next;
    return true;
}
```

teardown 铁律也写进了结构体：每个 VM 挂 `vcpus[] / mem[] / devices[]` 三个属主清晰的数组，销毁按**设备 → 内存 → vCPU** 逆序。这些现在还是类型和约定，但它们决定了接下来 stage2、GIC 虚拟化、vCPU 每一步怎么长。

### 4. 验证基础设施先于功能

这也是第一晚就有 `verify_el2.py` 的原因。今晚它抓到了**三个真 bug**（全是我和 AI 的产出）：

1. 验证脚本自己第一次跑就红了——它在旧缓冲里匹配到了第一次的提示符，根本没等命令输出。修法：needle 只扫上次提示符之后的字节。
2. 头文件改了没触发重编，`main.o` 里烤着旧 banner 字符串——修法：kbuild 同款 `-MMD -MP` 自动依赖。
3. 版本号文件改了没触发重编——修法：`$(OBJS): VERSION`。

**AI 写的代码，AI 时代的门禁来抓**。这就是为什么"真绿"必须是交互回显而不是字符串匹配。

## 工程惯例（开源项目的"教养"）

- **kernel 风格 commit**：`subsystem: 摘要` + 正文 + `Signed-off-by:`（DCO）+ `Co-authored-by:` 把 AI 结对**透明署名**（Claude / GLM-5.3 就在 git log 里，不藏着）
- **Ubuntu 式版本管理**：YY.MM 时间驱动（每年 4/10 月），仓库根一个 `VERSION` 文件做单一事实源——Makefile 注入编译宏、验证脚本动态读它，版本、banner、发布物永远不会漂移
- **Apache-2.0 + 全文件 SPDX + NOTICE**
- 官网用 kernel.org 的视觉风格（kernel.org 开源的 "Fifty shades of Tux" 主题，换个配色），发布表 mainline/stable/longterm——这次 stable 一栏终于不是 Coming soon 了

## 下一步

**27.04（明年 4 月）**：stage-2 页表 + 第一个 guest 地址空间。之后 GICv3 虚拟化、vCPU 调度，直到 VM0（RTOS 管理域）+ VM1（Linux）双 VM 起动。

我会把整个过程做成双语 devlog。如果你觉得一个人 + AI 能从零长出一个 OS 底座这件事有意思，watch 这个仓库：

**https://github.com/eshine-os/eshine-os**

---

### 发布说明（不随文发布）

- **知乎**：直接用，标题可换《一个人用 AI 结对编程，从零写 hypervisor 的第一晚》
- **公众号**：同稿，开头加一句引导语"这个系列会持续更新"，文末加公众号系列目录
- 事实口径：约 4-5 小时一晚、528 行、AI 结对（Claude + GLM-5.3）署名在 git log 可查
- 链接三件套：GitHub repo / www.eshine-os.org / tag v26.10
