# 站六十四 devlog:ELF core 文件(批次 E 深水终站)

> 承接站 63(docker 弧提交)。门禁 **180→182**。镜像机 未同步(用户令)。
> 站 46 "ELF 核文件不可为"翻案日。

## 0. 机制

- rt_ramfs_core_store(rt_fsrv.c):内核侧直写——crash 语境(死者本人
  EL1,紧跟 sys_exit,页表在役)一次直写零消息往返;ramfs 站 39 COW
  页承载(只读 rodata 的"不可为"前提被站 39 拆除,本站只是消费);
  CREATE 同序(字段先填+rf_nnode++ 殿后=半节点不可见);不挂名
  (open 走站 40 元名回退路由,rf_find 按表查即中)
- ELF core 序列化(rt_crash.c):Ehdr(ET_CORE/EM_AARCH64)+单 Phdr
  (PT_NOTE)+双 note——NT_PRSTATUS(标准 desc=elf_prstatus 最小形:
  siginfo+cursig+pid 四元+time 四元 0 面+gregset 272B=死者帧全寄存器
  x0-x30+sp+pc;gdb/readelf 语义可读)+NT_ESHINE 自定扩展(code/tick/
  esr/elr/far+用户帧链);600B/核(一页 COW 上限内)
- 记录点=rt_crash_record 尾部单挂点(野访终态+信号默认动作两路崩溃链
  同收);文件名 /var/core/core.<pid>(参考实现白描形)
- EL0 coreparse 工具(tools/ramfs-coreparse.c,musl):open/read 循环+
  Ehdr 校验+note 走查,打印 pid/cursig/pc/sp/esr/far/frames
- gcore 命令(EL0 壳):core 文件 hex 行转储串口("gcore: <off>: hh…")
  ——宿主重组判据面;复用站 40 put_hex2(hexdig 上移文件头)
- 门 [181]=EL0 coreparse 消费面;门 [182]=宿主 python 重组(gcore hex
  行,段序对齐防错位)+独立解析 Ehdr/Phdr/NT_PRSTATUS 头/sig/pid
  逐项断言——双解析器对照=binfmt_elf 形的独立验证

## 1. 战雷

- **El0 编辑损坏雷(本站最大战果)**:rt_crash.c 首版 Edit 产出整段
  损坏代码(缺 struct 关键字/断句残文/幽灵标识符混入)——"整块重写
  +肉眼过"纪律违背后再次显形;修=Write 整文件重写。同日三连:Edit
  裁尾句事故×2(fd_dump 签名/cmd_fddemo 签名各被裁一次)——**Edit 的
  old_string 尾行与 new_string 尾行必须逐句比对,裁句必现编译红,但
  本站三次全是语义静默型(编译绿/IDE 报)——Edit 后紧跟 Read 过目。**
- put_hex2/hexdig 上移(hex 转储面在 160 区,rngdemo 的原定义在 2100
  区=前向引用):小助手随首个消费者上移,双消费者(rngx/gcore)共享。
- crashdemo 子 pid 确定性:boot 序固定+spawn 点固定=pid 恒 2——场景
  JSON 静态化靠确定性(门禁 python 仍动态抓 pid 行,不依赖此性质)。
- 非确定 boot 挂脸(58 引入,已知未定谳)今日波动峰:4/4 挂(hello
  死 printf 中途+0x20 洪水 10.7MB,症状同 58)→静置后 3/3 绿+15/15
  绿——**挂率随宿主负载波动,验证失败先查宿主再谈机制**(站 25 纪律
  重申);64 站后专场取证记档不变。

## 2. 判据与验收

冒烟场景 station64-coredump.json 15/15 PASS:野访 0x4F000000→
"crash: core dumped /var/core/core.2"(600B)→gcore done 600 bytes→
run /bin/coreparse→pid=2 cursig=11 pc=0x40207a24 sp=0x4020c000
esr=0x92000006 far=0x4f000000 frames=1 0x40204940(全语义对齐:野址/
pc/sp/帧链)。门禁 [181]/[182] append-only 写而不跑(批验收纪律;
批末=64 站收官后统一 verify)。help 182 步;ARCHITECTURE 同步。
