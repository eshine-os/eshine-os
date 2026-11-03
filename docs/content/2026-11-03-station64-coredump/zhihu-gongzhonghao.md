# 崩溃取证落盘:ELF core 文件的翻案

站 64,批次 E 深水终站:ELF core 文件。

## 一个"不可为"的翻案

十八站之前(站 46),崩溃取证链写下了这样的注记:"ELF 核文件不可为:
ramfs=只读 rodata 服务器"——于是 v0 诚实地退化为内存环+shell 倾倒。

站 39 给 ramfs 写时复制(COW)页的时候,这个"不可为"的前提已经被
拆掉了——但没人回头。今天回头:crash 语境直接把死者语境序列化成
aarch64 ELF64 core 文件落进 /var/core/core.<pid>。

## 死者最短路径

落盘的写者选择有点反直觉:不走文件服务器的三段消息往返,而是内核
侧直写(新开的 rt_ramfs_core_store)。理由写在注释里:消息面是活任务
的活路——发送者要在往返中阻塞等待,而 crash 语境是死者本人,它后面
紧跟着 exit,应该走最短路径。取证是系统行为,不是服务交互。

## 600 字节的标准形

core 文件是按 binfmt_elf 认真做的:ET_CORE 类型、EM_AARCH64 机器、
单 PT_NOTE 段、两枚 note——NT_PRSTATUS 是标准 elf_prstatus 形
(gdb/readelf 语义可读),gregset 存满 272 字节全寄存器(x0-x30+sp+pc);
另一枚 NT_ESHINE 是扩展面(esr/elr/far 硬件三元+用户帧链)。

判据也是双面的:EL0 侧 coreparse 工具解析打印(pid/cursig/pc/esr/far/
frames);宿主侧门禁用 gcore 命令把文件 hex 转储到串口,python 重组后
独立解析,逐项断言 Ehdr/Phdr/NT_PRSTATUS/sig/pid。两套互不相识的
解析器从同一文件读出同一语义——格式才算真的标准。

野读 0x4F000000,core 里 far=0x4f000000、esr=0x92000006、pc 恰是野读
指令——取证文件比控制台倾倒诚实得多。

门禁 180→182。批次 E 收官,五十九到六十四,六站连收。
