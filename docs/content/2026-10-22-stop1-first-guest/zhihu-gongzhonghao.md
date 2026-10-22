# 一个未初始化的 SP_EL0,伪装成三起 bug

> eshine-os 27.04 连载第 1 篇(技术篇)。第 0 篇见 26.10 EL2 骨架发布。
> 代码 github.com/eshine-os/eshine-os,Apache-2.0,AI 结对(Claude/GLM-5.3)
> 署名在 git log 里。

上一站(26.10)的终点是:EL2 直启骨架、VM 状态机、真绿门禁。这一站的
目标一句话:**给 hypervisor 装上第二级地址翻译,让第一个 guest 在自己
的地址空间里活起来**。交付是干净的,过程里却埋着一桩"三起表面无关的
bug、一个共同根因"的连环案 —— 本文把它完整摊开。

## 交付了什么

**① stage-2 页表模块**。VTCR 定好粒度/层级/物理地址宽(4KB/T0SZ=32/
SL0=1/PS=40bit),描述符三件套:表描述符、2MB block、4KB page。映射
刻意做成**非恒等**:guest 以为自己在物理 0x40000000,真身被挪到
0x50000000 —— 翻译在工作,才证明 hypervisor 在工作。

**② vCPU 进出口**。vcpu_run 武装 ELR_EL2/SPSR_EL2/VTTBR/HCR,ERET 进
EL1;guest HVC 退回 EL2,清 VM 状态后恢复宿主帧。宿主现场(sp/lr,
后来扩展到整块 callee-saved)存进 vcpu 结构。

**③ 首个裸机 guest**。独立链接的 ELF(不是 Linux,不是 RTOS,就是它自己),
同一套代码两条 IO 路径:banner 走 HVC 超调用(EL2 代打),交互 shell
直访 UART —— 同一物理设备,一条路经超调用、一条路经 stage-2 设备页
(nGnRE+XN),两条都绿才认账。8 步门禁连过 8 次。

三起 bug 按出场顺序讲。前两起是热身,第三起是主菜。

## 热身一:AF bit,教科书一页纸,现场两小时

stage-2 描述符的 AF(Access Flag,bit10)**必须置位**。我按对 stage-1 的
记忆拼描述符,漏了它 —— guest 第一条取指就 Instruction Abort,
ESR=0x8200000A(IFSC 0x0A = Access Flag fault,L2)。

有意思的是排查路径:错误码本身就说出了答案,但"IFSC 0x0A"这种字段要
翻 ARM ARM 到 DDI 0487 的 fault status 表才对得上号。**手册里有答案的
bug 都是好 bug**;此后 AF 变成代码注释里的"铁律",写 stage2.c 的人
(包括未来的我)不会再踩。

## 热身二:HVC 的寄存器契约,编译器不替你读手册

guest banner 循环里,字符串指针被 EL2 处理程序改了值 —— guest 开始
无限打印 '\n'。反汇编一看,指针活在 x1:超调用 ABI 说 x1-x17 易失,
guest 的内联 asm 没声明 clobber,编译器理直气壮把活跃值放在会被
EL2 碾过的寄存器里。

修复:guest 侧 hvc.h 显式声明 x1-x17 clobber。教训:**ABI 是双方契约,
编译器只读你声明的,不读你心里的**。

## 主菜:三起"灵异",一个 0

然后是真正的对手。现象像灵异事件:

- guest banner **能完整打印**(41 次 HVC,逐字都对);
- guest 的退出 HVC **能到**(ESR=0x5a000001 清清楚楚);
- 退出路径四步调试打印**全过**;
- 然后宿主 ret 到垃圾地址,`ESR_EL2 = 0x02000000`,WFI 收场。

每条静态链路都检查无误。这种时候不再推理,上 gdb(gdb-multiarch +
QEMU -s -S,断点钉在 vcpu_run,一路单步):

```
A vcpu_run 入口:      sp=0x4020ce60      ← 宿主栈正常
B msr spsel,#0 之后:  sp=0               ← SP_EL0 从未初始化!!
C eret_enter:         x16=0x20 x17=0     ← 宿主帧读的是 flash 零页
H ret x17 之后:       pc=0               ← 跳去执行 QEMU bootloader
```

根因一层层揭开:

1. **QEMU 直启 EL2 时复位 SPSel=1** —— 我的整个宿主 C 世界,从第一行
   起就跑在 SP_EL2 上,而 SP_EL0 从未初始化,值是 0;
2. 我在 vcpu_run 里给异常路径准备的"excp_stack 舞步"(`msr spsel,#1;
   mov sp,excp_stack; msr spsel,#0`)于是变成灾难:它把**活着的宿主栈
   (SP_EL2)覆盖**成异常栈顶,再切到**死 bank**(SP_EL0=0);
3. 尾部的栈恢复序列从 PA 0 附近读 QEMU bootloader 零页 → "宿主帧"
   = {sp:0x20, lr:0} → ret 到 0 → ESR 0x02000000(在 PA 0 执行
   bootloader 指令,被仿真器当成不支持指令拦下)。

**为什么 guest 的 banner 反而正常?**因为 ELR/SPSR 是在舞步**之前**
武装的;ERET 本身不依赖我踩烂的栈。**为什么早先的 HVC 都正常?**因为
异常硬件强制 SPSel=1 落在 SP_EL2 —— 而舞步恰好把 SP_EL2 设成了
excp_stack:那个"修复"碰巧把 HVC 路径救活了,同时把退出路径钉死了。
一个错误,救活一半系统,杀死另一半 —— 这就是它藏住自己的方式。

修复其实只有三行:boot 归一 bank(`_start` 置 SPSel=0,宿主世界住
SP_EL0;SP_EL2 专职异常栈,boot 一次装好),vcpu_run 删掉舞步。
`msr spsel, #0` 从"修复"变成"归一"。一句话教训:

**寄存器 bank 的默认值,在固件和仿真器里没有标准答案 —— boot 代码必须
显式归一,而不是假设"应该是 0"。**

## 番外:永不等待的轮询,是竞态制造机

门禁随后出现 ~25% 的间歇失败:token 字节被随机重复
(`GOK1882187` 回显成 `GOOK18822187`)。真相:guest 的 UART 驱动把
FR 寄存器偏移写成了 0x14 —— PL011 的 FR 在 **0x18**,0x14 未实现、
QEMU 读它恒 0 → RXFE 永不置位 → 读 DR 变成百万次每秒的热自旋,和
QEMU 主线程往 FIFO 推字节的动作在读写指针上赛跑,偶发同一字节弹两次。

修一行偏移,8 连过。教训两条:**寄存器偏移抄手册,不抄记忆;永不等待
的轮询不是慢,是竞态生成器。**(顺带一提:trace 一开这个 bug 就躲,
最后是失败现场的 dump 破的案 —— Heisenbug 就得用死人验尸。)

## 方法论小结

这一站的 debug 笔记浓缩成三条,后面两站反复用到:

1. 静态链路全对、运行时矛盾 → **停止推理,换运行时铁证**(gdb-multiarch
   + QEMU gdbstub,断点/单步/watchpoint);
2. 错误码字段(ESR/IFSC/DFSC)逐位解码,答案常在 ARM ARM 的表里;
3. 修复要区分"归一"和"补偿":让系统回到一个明确声明的不变式,
   而不是在错误状态上贴膏药。

## 数字

9 个 commit(stage-2/vCPU/guest/双 IO/门禁/文档);8 步门禁连过 8 次;
零警告。下一步预告:VM 抽象 —— 把"一个硬编码 guest"升级成"被管理的
VM 对象",坑照旧管够(见连载第 2 篇)。

---

### 平台微调说明

- **知乎**:主菜一节建议配 gdb 四连截图;标题可用"一个未初始化的
  寄存器,伪装成三起 bug"。可挂"嵌入式/虚拟化/ARM"话题。
- **公众号**:主菜改成信息图叙事(三个现象 → 一个根因的三箭头归并);
  文末放连载目录(26.10 发布 → 本篇 → 第 2 篇)与仓库链接。
- 共用事实口径以 docs/content/2026-10-22-stop1-first-guest/README.md 为准。
