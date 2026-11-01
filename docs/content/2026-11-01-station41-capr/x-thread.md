# X 8 推(站 41 CAPR 能力框架)

1/8
eshine-os 站 41(序列终站):每个进程不再全权裸奔。四位能力位图(VFS/SPAWN/KILL/MM),create=零能力,壳=CAP_ALL,fork 按位继承——系统里不存在任何"加能力"路径。self-inc-out 由构造保证,不靠执法。 #kernel #capability

2/8
fail-closed 执法点=choke point 非全系统调用铠甲:fd 六门(open/create/unlink/dir/getdents64/fstatat)判 VFS;fork/clone 判 SPAWN;kill 判 KILL;mmap/shm/mq/pipe 判 MM。缺能力即拒,默认拒绝。v0 边界诚实记档:已开 fd=已授权窗。 #security

3/8
rlimit 自降单向:max_fd 满→第 4 open -EMFILE;max_child 满→fork -EAGAIN;rlimit_self 只降不升。uuid=create 期随机 8B,fork=新 uuid——资源可以继承,身份不能。参考实现五面的最小诚实站内形。 #systems

4/8
本站最大战果不是 CAPR,是一颗雷:往 sys_fork 插两道检查,一次编辑把紧邻的 rt_proc_create(NULL,&cp) 整行吞掉。cp 成未初始化野指针,fork_as 拿它狂写 → EL1 数据中止 → 静默驻车(wfi 永环)。全系统死锁,零故障行。 #debugging

5/8
三小时法医三重伪象:①尸检栈读出 sem_take/sys_write 以为是活链——其实是早前打印留下的陈栈(272B 帧是寄存器保存区不是 C 帧链);②双硬件 watchpoint(状态+当前任务)零命中=从未阻塞从未切走;③全量断点集 190s 零停点=-O2 内联让"没停点"看着像"没执行"。 #forensics

6/8
终局一击:git diff 照面,-rt_proc_create(NULL,&cp) 一行在减号侧赫然。判据先于结论——diff 应该最先打开,我放在了最后。改动面 7 文件 +297 行,一屏可尽。 #lesson

7/8
机器背书的讽刺:-Wall -Wextra 本就在构建里,吞行那次构建 GCC 必然打了"cp 未初始化"警告。警告在,没人读。freestanding 无 -Werror,构建输出必须过眼。顺手清了笔陈账:help 走查头部步数停在 93,六站没人对账,一并修到 116。 #build

8/8
41 站齐收:28 EL0 壳→29 进程→30 消息→31 server→32 信号→33 musl→34 yocto→…→40 busybox 双世界→41 能力框架。每站 spec 先行、门禁两连绿、三处同步、内容包同批。从零打造第一季,收图。 #eshineos #hypervisor
