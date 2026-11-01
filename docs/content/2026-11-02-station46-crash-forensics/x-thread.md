# X 8 推(站 46 崩溃取证链)

1/8
eshine-os 站 46:一个用户进程野访问,凭什么全 VM 陪葬?站 29 留下的"野访=全系统 panic"今天翻案——死者走它自己的 exit 链,死前留全遗照。 #kernel #faults

2/8
翻案的关键:野访者现在走 sys_exit 同链(fd 清+线程群扫+ZOMBIE+父收尸),code=139(SIGSEGV 面)——与信号终局同一台机器,零新死亡流程。EL1 panic 面不动:内核 bug 仍全驻车,保守面留对了地方。 #design

3/8
遗照=crash 环(2 槽):tick/pid/任务名/esr-elr-far 硬件三元/退出码/x0-x5+x29 寄存器面/用户 x29 帧链前 4 帧。ELF 核文件在我们架构不可为(ramfs 只读)——内存记录+shell 倾倒,诚实形不冒充。 #coredump

4/8
走死者的栈,三个门:窗门(帧链不许出用户窗)、映射门(逐读前查 PTE present——坏栈不许把取证变成 EL1 二次事故)、单调门(栈向高长,环即断)。取证工具的第一美德是不制造新事故。 #debugging

5/8
首版 0 帧。诊断:把 x29 加进寄存器面转储——值在窗内,但 -O2 用户码默认不链帧指针,x29 是残值,[fp+8] 不是返回址。仪器先于判据;顺带自曝一次十六进制心算错误(0x4020A1E0 误读作窗外,重算即翻)。 #forensics

6/8
想走捷径:函数级 __attribute__((optimize)) 局部开链——污染整个编译单元,凭空炸出 undefined strlen。属性是雷区,弃。 #gcc

7/8
终修:crashdemo 手工挂一帧 {x29,x30} 当法医夹具——内联 asm 两行,walk 由野读点证活(#0 pc=0x402034dc)。真链用户构建(-fno-omit-frame-pointer)接上同一机器就是全链。捷径的成本永远比正路高。 #lesson

8/8
判据:子野读 0x4F000000→死 code=139,父照常收尸打印(连坐翻案的活证);crashinfo:far 面+帧链面。门禁 131,冒烟 12/12。批令推进,第 46 站。 #eshineos #buildinpublic
