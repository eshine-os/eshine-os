# 站 41:能力框架 🛡️(还有一行被吞掉的调用)

进程从"全权裸奔"到"能力上岗"——序列终站收整 ✅

✅ 四位能力位图:VFS/SPAWN/KILL/MM
✅ create=零能力,壳=CAP_ALL,fork 按位继承
✅ 无任何加能力路径:self-inc-out 由构造保证
✅ fail-closed:fd 六门+fork/kill/mmap 卡点,缺=拒
✅ rlimit 自降单向:第 4 个 fd 就 -EMFILE
✅ uuid:fork 得新身份,资源可承身份不承

## 本站最大战果 🕵️

插两道检查,一次编辑把旁边那行
rt_proc_create(NULL,&cp) 整行吞了——

cp 成野指针,fork 往野地址狂写,
EL1 数据中止 → 静默驻车(wfi 永环)。
全系统死锁,零故障行,查了三个小时。

三重伪象:陈栈误读成活链、
双 watchpoint 零命中、断点后零停点。
最后 git diff 一照面定谳。

最讽刺的:-Wall -Wextra 本来就在构建里,
那天 GCC 必然打过"cp 未初始化"警告——
警告在,没人读。

## 序列齐收 📯

28 EL0 壳 → 41 能力框架,
十四站,spec 先行、两连绿、三处同步,
从零打造第一季收图。
