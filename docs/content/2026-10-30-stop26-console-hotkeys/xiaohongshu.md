<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# Ctrl-^ 诞生记:找一个没有主人的键 ⌨️

从零写 hypervisor 第 26 站,需求看着很简单:"快捷键切换三个
VM 的 shell"。

认真选键才发现——串口上每个控制键都已经有主了:

🚫 Ctrl-] (0x1d):已经是逃生键,三站门禁锚定,不能动
🚫 Ctrl-C (0x03):SIGINT,Linux tty 的
🚫 Ctrl-D (0x04):EOF
🚫 Ctrl-\ (0x1c):SIGQUIT,busybox 会话里是真信号
🚫 Ctrl-Z (0x1a):SIGTSTP
🚫 F1-F2/方向键:多字节转义序列,vi/bash 的 ESC 分不清
🚫 Alt+数字:vi 用户先按 ESC 再按 3 → 误切

数到最后只有真·无主的:0x1e (Ctrl-^),记录分隔符,TTY
不绑信号,readline/vi/内核全不用,RTOS 壳输入过滤器零成本。

而且它和 0x1d 字节相邻——"] 夺回宿主,^ 跨 VM 直切",
键盘邻居+字节邻居,一对心智模型 ✅

语义抄 QEMU Ctrl-A 的作业:
▪ Ctrl-^ 0|1|2 → 切 vm
▪ Ctrl-^ h → 回宿主
▪ 双击 → 字面直递
▪ 按错键 → 前缀取消,本键照常(一个字节都不吞)

实现上把 vmconsole 命令体抽成公共函数——快捷键和手打命令
零行为差异;单字节状态机+fall-through,连双击字面都是
fall-through 白嫖出来的,零 goto 零复制。

门禁 79→80 双绿收站,还顺手抓到一折:新门改了世界状态,
下游旧门的逃生步骤扑空了——插门不只是改编号 😅

#从零打造操作系统 #虚拟化 #hypervisor #嵌入式 #eshineOS
