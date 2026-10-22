# YouTube / B 站分镜脚本 — 第三站:vCPU 完整上下文切换

## 标题(YouTube 英文)
A bad guest can't kill my hypervisor anymore (and the stack leak that took 79 traps to explode)
## 标题(B 站中文)
坏 guest 杀不死 hypervisor 了 + 79 发陷阱才爆的栈泄漏(vCPU 上下文切换)

## 描述
Stop 3 of the from-scratch ARM64 Type-1 hypervisor: a full trap frame
inside the vcpu (asm stub saves x0-x30 before any C runs), HVC yield
with breakpoint resume through the PAUSED state, and guest fault
isolation — the guest dies clean, the host keeps typing. Three debug
war stories: a noreturn that lied, a 110-byte-per-trap SP_EL2 leak
that walked through the stack bottom at trap #79, and GCC folding
the register-integrity test into a constant. Open source:
github.com/eshine-os/eshine-os · Apache-2.0 · 1-2h/day, human + AI.

## 分镜脚本

| # | 时长 | 画面 | 口播要点 |
|---|---|---|---|
| 1 | 0:00-0:30 | 黑屏淡入 + selftest 输出滚屏 | 开场:上一集我欠了笔债——guest 寄存器存活靠编译器巧合。这集还债,还赚了三个功能 |
| 2 | 0:30-1:30 | asm 桩讲解(代码特写) | 陷阱帧:x0-x30+sp+pc+pstate 进 vcpu;保存动作自己都需要引导——x18-x28 不能碰,先停 x0 |
| 3 | 1:30-2:20 | yield→宿主→resumed 复活录屏 | 断点续跑:guest 让出、宿主干活、guest 原地继续。PAUSED 状态等了一周终于上岗 |
| 4 | 2:20-3:00 | fault 诊断录屏(x0=野指针特写) | fault 隔离:VM 干净死,诊断读帧——能看到野指针原值。宿主毫发无伤:guest 崩溃是数据,宿主崩溃才是事故 |
| 5 | 3:00-3:40 | 坑一:挂死现场 + 反汇编 | noreturn 谎言:进入口会返回,不能标 noreturn;GCC 信任你的属性 |
| 6 | 3:40-5:10 | 坑二(主菜):泄漏计时图 + gdb 现场 | SP_EL2 每陷阱净泄 110B:trap_resume eret 不还栈;79 发碾穿 4KB 栈底踩 cur_vcpu;上一站没炸=余量未用完;修法=入口归一化 |
| 7 | 5:10-5:50 | 坑三:反汇编对比(折叠前后) | GCC 常量折叠杀判据:循环体零累加;空 asm "+r" 一行救回;x19/x30 跨 100 陷阱实证 |
| 8 | 5:50-6:20 | make verify 17 步 + 仓库页 | 门禁 ×2;开源地址;下集:调度或 GICv3 中断注入 |

## 录屏清单
1. 全流程:selftest → yield → vmls → resumed(主视觉,一镜到底)
2. fault 诊断 + 宿主 echo 继续活
3. gdb 复现踩 cur_vcpu(进阶素材,泄漏侦探记)
4. 反汇编两帧对比:常量折叠前 vs 空 asm 后
5. make verify 连续两次 17/17
6. GitHub 仓库 + commit 历史(AI 署名特写)

## 标签
YouTube: hypervisor, ARM64, QEMU, virtualization, trap frame, context switch, debugging, operating systems, from scratch
B 站: 虚拟化 hypervisor 底层开发 操作系统 ARM gdb 调试 从零开始 开源
