<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 小红书:UART IRQ 站——回读正确,就是不生效

## 正文(≤800 字)

第八站:把串口轮询换成中断驱动 🚀
本站最大战果,是我踩过的最阴的坑:

**GIC 列表寄存器(LR)的位号,旧代码里全是错的名字** 😱
真实布局:bit60=Group,bit61=HW,bit62=State(挂起)。
第四站写的常量:PENDING=1<<61(实际是 HW 位),HW=1<<62(实际是
挂起位)——**vtimer 三位全置,恰好凑成正确形态,名字全错,三站能跑**。

我照抄错名注入 UART:置了"Group+PENDING"=Group+HW 位,
**挂起状态位是空的=LR 无效**。症状之阴:
✅ 写进去,读回来原样(探针回读 LR/VMCR/HCR 全对)
❌ vIRQ 线就是不抬(guest 走到 wfi,没有任何向量进入)
❌ QEMU 的 WFI 还把无效 LR 算作"有挂起"→完成不陷阱→guest 空转
   烧量子,泵每轮注入一次成风暴,顺手把 vtimer 的 27 也覆盖了

**回读正确 ≠ 生效。旧代码能跑,可能只是碰巧三位全置。**
位号以架构手册为准,不以"能跑的旧代码"为准 📖

副线战果:
①GCC 把 guest 的寄存器写合并成 post-index 回写型
  (str w,[x],#-32),综合症 ISV=0,宿主拿不到值只能拒——
  guest 访存器全改 volatile asm 访问器,形式保证可仿真
②多字符爆发停在半程:一发中断只弹一个字——ISR 改排空式
  (一次清空整批,真 UART 驱动同律)+ 泵内再唤醒兜底

门禁 28 步双绿 ✅ 每晚 1-2h 与 AI 从零造 Type-1,devlog 08 全记录。

## 配图清单

1. 封面:回读探针行(INJ33 lr=... rb=... 全对却不投递)
2. ARM ARM 位号对照表手写图(名字 vs 真实)
3. post-index 反汇编截图(str w1,[x18],#-32)
4. [vm0] uart irq on 判据特写
5. make verify 28 步 PASS ×2

## 标签

#虚拟化 #hypervisor #ARM64 #操作系统 #内核开发 #嵌入式 #QEMU
#AI编程 #从零开始 #eshine-os #调试故事
