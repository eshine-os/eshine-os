# 站四十六:崩溃取证链(crash 环+寄存器面+用户帧链)

**日期**:2026-11-02
**状态**:实现完成,冒烟 12/12;门禁 131(新 [125-126])按批令写而不跑
**spec**:docs/specs/2026-11-02-station46-crash-forensics.md

## 契约翻案与机制

- **站 29 留边翻案**:EL0 无 VMA 野访从"全系统 rt_panic"升
  "死者本人 exit 链"(code=139=128+11,与信号终局同一台机器:
  fd 清+线程群扫+ZOMBIE+收尸)。崩溃不再连坐全 VM;
  **EL1 panic 面不动**(内核 bug 仍全驻车=正确的保守)。
- **crash 环**(rt_crash.c,2 槽):{tick,pid,任务名,esr/elr/far,
  code,x0-x5+x29,用户帧链前 4 帧}。参考实现 coredump 白描的站内
  形状——ELF 核文件不可为(ramfs 只读),内存记录+shell 倾倒
  =v0 诚实形,spec 记档差异。
- 记录点两处:野访终态(硬件三元全)+信号默认动作(三元=0 面)。
- **用户帧链**:死者帧 x29(frame[3+29],FR 布局 x_i=frame[3+i])
  起走;窗门(identity 0x402-0x404M ∪ mmap 0x40C-0x40E M)+
  **映射门**(逐读前 rt_as_user_mapped 查 l2 PTE present——坏栈
  不得把取证变成 EL1 二次事故)+单调门;4 帧保险丝。

## 战雷两枚(判据先于结论)

1. **-O2 omit-frame-pointer 定谳**:首版冒烟记录工作但 **0 帧**
   ——x29 残值恰在窗内(0x4020A1E0)而 [fp+8] 非返回址。先加
   x29 入寄存器面再诊断(仪器先于判据),十六进制心算曾误导
   一轮(0x4020A1E0 误读作窗外,重算即翻)。
2. **optimize 属性污染 TU**:函数级 no-omit-frame-pointer 方案
   炸链接(全 TU 代码生成变样,凭空 strlen)——属性是雷区弃。
   修=crashdemo **法医夹具**手工挂 {x29,x30} 帧:walk 由野读点
   证活(#0 pc=0x402034dc);真链用户构建(-fno-omit)同一机器
   即全链。

## 判据

crashdemo:子野读 0x4F000000(两窗皆无 VMA)→ 子死 code=139、
**父照常收尸打印**(连坐翻案的活证)→ crashinfo:far=0x4f000000
+pc=0x(帧链)。门 [125]/[126]。

## 纪律执行记录

- 探针脚本自身 % 格式化 bug 一枚(QEMU 秒死)——修后取证,
  自雷不讳言。
- sed 针线编辑自雷(.n 字面)——两脸假阴,改整行 python 替换。

## 非目标(记档)

ELF core 文件/落盘(ramfs 只读,后弧);crash 后重启链;多任务
并发崩溃合并取证。
