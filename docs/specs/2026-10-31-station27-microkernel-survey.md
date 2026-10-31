# 站二十七:微内核机制普查 + 路线 spec(参考实现参考基线)

> 承接站二十六(d3037b1+915862e,80 门双绿)。用户令四指令(10-31):
> ①参考实现=机制参考基准(非 参考RTOS);②RTOS 终态=类 QNX 微内核;
> ③整个 eshine-os 用 yocto 编译(参考参考实现);④终局经 docker 发布。
> **红线(用户定格):机制参考、零代码拷贝、参考实现权属私藏。**
> 本站=纯普查站:不动代码,80 门不动;产出=机制档案+站点映射+
> 微内核弧各站 spec 骨架,报用户批后站 28 起施工。

## 0. 参考基线的两份一手档案(已读毕)

本站普查以两份一手材料为锚,不再散读源码:

1. **参考实现 rtos-kernel/include/uapi/syscall_no.h**(全 305 号表,
   逐号读毕)——服务清单的直接证据;
2. **参考实现 docs/optimization/rtos-vs-qnx-linux-2026-08-07.md**
   (361 行,13 代理 410k tokens 的机制级审计报告,逐行读毕)
   ——它同时给出:参考实现机制形态(E: 侧)、QNX/Linux 参照(R: 侧)、
   修复建议(Fix: 侧)三栏。**这是比源码更干净的参照面**:
   审计报告本身就是机制白描,新仓按"机制参考"红线使用。

## 1. 参考实现架构定谳(审计报告执行摘要+syscall 表实证)

- **形态:微内核式 userspace-server**。内核持原语(同步队列件
  消息传递=QNX Neutrino 同源/task/sched/signal/futex/clock/vm);
  用户态服务进程:proc_mgr(进程管理)、vfs_server、vmmgr_server、
  ramfs manager;musl 用户态经 syscall 翻译层。
- **消息面=QNX 镜像**:其消息调用{ATTACH,OPEN,SEND,RECV,REPLY,
  SENDV,RECEIVEV,FAST_*,SEND_PULSE,CHID_GET,CREATE,DESTROY,
  VM_SET,READV,WRITEV,*_TIMEOUT}——chid(通道)/coid(连接)/
  三段消息(Send-Receive-Reply)/pulse(异步脉冲)/iov 向量,
  术语与 QNX Neutrino 一一对应。
- **进程模型**:utask(线程)+ uprocess(进程);UPROCESS_
  {CREATE,PREPARE_EXEC,EXEC_SETUP,EXEC_START,TERMINATE,
  COPY_UCONTEXT}——真进程生命周期含 exec。
- **地址空间**:VMM/XSPACE_{OPEN,VALLOC,VMAP,PMAP,PUNMAP,
  MAP_DEVICE,FREE};vmmgr_server(用户态内存服务)+缺页 pulse
  (PFAULT_CONTINUE)。
- **调度**:参考RTOS 位图基座 **+ k_cfs(Linux CFS 移植,带审计
  定谳的 bug:无 tick 抢占/单全局队列)**;策略 FIFO/RR/CFS;
  APS 自适应分区(k_aps)+MPRT 内存分区(审计:均无鉴权)。
- **安全**:CAPR 能力模型(审计:fail-open/硬编码 UUID 旁路/
  无 uid/gid 凭证)+rlimit+uuid。
- **rootfs 真相**:无块设备文件系统(仅 ramfs/procfs/sysfs
  ——审计 F3-06),rootfs=cpio initramfs 加载 ramfs+proc 管理器
  进程;init=用户进程管理服务 硬编码(审计 F3-18)。**用户
  "为何没有 rootfs"之问的参考答案:参考实现的 rootfs 也不是块
  设备 FS,是 initramfs+ramfs+用户态 server 形态。**
- **syscall ABI**:自有编号 0..304(230 注册)+musl
  patch 596 行翻译表(审计 A2-03 建议转标准 __NR_*)。
- **攻击面**:~230 内核 syscall(审计 A2-11 建议收缩到 ~100
  原语,fdtable/posix_ipc/security 迁用户态 server)。

## 2. 新仓现状资产(26 站,映射表"我方"侧)

| 参考实现机制 | 新仓对应 | 状态 |
|---|---|---|
| core/base 任务内核(参考RTOS 位图) | 站16 位图调度+统一任务帧 | ✅ |
| 同步原语(sem/mutex+queue/event) | 站17/19(参考RTOS 语义+超时族) | ✅ |
| 互斥 PI(rtmutex 链) | 站18B 单级 PI | ✅(单级) |
| 软定时器 | 站22B(绝对截止账本) | ✅ |
| IRQ 注册表 | 站23A(替换式注册表) | ✅ |
| SMP(per-CPU 队/自旋锁/SGI) | 站20 | ✅ |
| 栈防护 | 站21C(垫+哨兵+水位+live-sp 守卫) | ✅ |
| panic 观测 | 站21B | ✅ |
| hrtimer | — | ✗(100Hz tick 即当前时基) |
| syscall 层 | — | ✗(站 28 起) |
| EL0/地址空间(xspace/vmmgr) | — | ✗(站 28-29) |
| uprocess/exec | — | ✗(站 29) |
| 同步队列件 消息传递 | — | ✗(站 30) |
| 用户态 server(proc/vfs/io) | — | ✗(站 31) |
| signal/futex | — | ✗(站 32) |
| musl 用户态 | — | ✗(站 33) |
| CAPR 安全 | — | ✗(评估瘦身) |
| CFS/APS/MPRT | — | **偏差决策(§4)** |
| virtio-blk | vm1 侧 dwrite/dread(设备层已通) | ✅(RTOS 侧待接) |

## 3. 微内核弧站点序列(普查回填已齐,定稿;各站参照节标注)

| 站 | 块 | 内容 | 参照 |
|---|---|---|---|
| 27 | 普查+spec | 本站 | — |
| 28 | EL0+syscall 骨架 | SVC #0 入口/全量陷入帧(含 FP)/静态分发表(子系统分文件,不蹈 5456 行单文件)/**寄存器 ABI=号 x8+参 x0-x6+返 x0**/**uaccess 纪律=access_ok+异常表**(参考实现审计 C1-06/07 定谳的 memcpy 退化不蹈)/SPSR 守卫;壳+demo 迁 EL0 | §7 |
| 29 | 进程+地址空间 | 统一 task 结构+task_group 进程对象/每进程 stage-1 页表+ASID 位图+切换点/PER-UPROCESS 生命周期(CREATE 挂起/COPY_UCONTEXT fork/EXEC_SETUP **7 参帧改写**=内核不读 ELF,loader 在用户态)/PGD 高低半区布局 | §7/§8 |
| 30 | 消息传递 IPC | 三段消息(Send-Recv-Reply)+rcvid 复合+短消息栈缓冲/长消息映射/abort+UNBLOCK 脉冲/pulse+lpulse/iov;**六差距修复**:coid 实体+引用计数/消息优先级传播/channel flags/命名层级化/事件绑定族/**VM_SET 单拷窗照抄** | §9 |
| 31 | server+rootfs | **开机链**:内核从 cpio 固定 spawn PM→PM 拉服务序列(ramfs 挂 "/"→procfs→driver manager(tty)→busybox init→rcS 挂 /proc)/fd=通道三元组(fdtable+PNODE/POCB resmgr 面);RTOS 侧接 virtio-blk;块 FS 观察哨 | §10 |
| 32 | signal+futex | 投递点=返回用户态(SWITCH_OUT)在被投递任务自身栈造 ucontext/ERESTART 重启/futex 16 桶+robust list+PI 变体 | §10 |
| 33 | musl 用户态(基础库) | **双 musl 世界**(宿主=改道分发;客 Linux=标准);新仓从零即 **Route B 形态(显式 -l基础库,不源级并 libc.a)**;io 三层 resmgr 风 | §10 |
| 34+ | yocto 弧 | meta 层自建(参照 eshine-rtos.bb 形态:单仓 archive+源内构建;**新仓免 60 枚工具链切换补丁**——从零统一工具链);**边界扩展:hypervisor/RTOS 内核也收进 Yocto** | §10+设计记录 |
| 末 | docker 弧 | PCT 思想容器化:构建+QEMU 全系统演示镜像 | 07-07 传承 |

顺序原则不变:EL0 先于进程→消息先于 server→server 先于
musl→yocto 收全量→docker 收出货;signal/futex(32)可在 30/31
后按风险排。**每站 DoD 不变**(spec→实现→门禁×2→文档→
内容包→commit→镜像机→记忆),80 门只增不减。

## 4. 偏差决策点(登记 baseline-deviations 续册,报用户批)

1. **CFS 背景类**:参考实现的 k_cfs 仅在 RT 位图队列选到 idle 时
   才被咨询(永不抢占 RT 任务,nice 区间限定)——"CFS 排除"
   精确化为**背景类不移植**:新仓 RT 位图为主(与参考实现主导
   路径同构),nice/背景带列观察哨(管理 VM 需要低优先级
   后台清扫时再评);参考实现 CFS 自审两 bug(无 tick 抢占/单
   全局 runqueue)佐证不移植。登记有意偏差。
2. **APS/MPRT 分区**:参考实现有(QNX APS 同源);新仓**暂缓**,
   观察哨级登记——待管理 VM(VM0 治理面)真正需要资源分区时
   再立站。
3. **syscall ABI**:参考实现=自有编号+musl patch 翻译层
   (其审计自认是债务 A2-03);新仓候选=**直接标准 __NR_*
   (asm-generic) 编号**,musl 零 patch 直连——机制参考其服务
   面、ABI 面更干净;登记偏差待用户批。
4. **内核攻击面**:采纳参考实现审计自己的建议(A2-11):新仓从
   第一天就 ~100 原语级内核面+服务走用户态 server——参考参考实现
   形态、跳过它自审的倒置期。
5. **CAPR 安全**:参考实现的能力模型带 fail-open/硬编码旁路
   (其审计定谳);新仓 v1 采 fail-closed 铁律+最小凭证面
   (ruid/euid),CAPR 完整模型评估瘦身。
6. **rootfs 形态**:对齐参考实现真相=initramfs+ramfs+用户态
   server(非块设备 FS);块 FS(squashfs/fat32)为其审计建议的
   补全面,新仓列为 rootfs 站后的观察哨。

## 5. 红线执行纪律(站 28 起每站复查)

- 机制参考=读结构/行为/审计白描;零代码拷贝=新仓实现全部
  从零写;开源RTOS 原语层可对照 **Apache-2.0 公开上游**实现
  (比参考实现更干净的合法参照面);参考实现自有服务层只按"机制形状"
  白描参考;第三方样本 3,142 文件永不碰。
- 每站 spec 报备与参考实现的机制差异入 baseline-deviations.md 续册
  (条目 #24 起)。

## 7. syscall 入口+进程模型细节(普查回填)

**入口形态**:SVC #0 唯一陷入指令(无 sysenter/vsyscall 固定
页,vDSO fallback 亦内联 svc);**SVC 内按 x8 复用分发**:
x8==SMC_CALL(207)→el0_smc_handler(代理 smc #0 进固件),
x8==HYPERVISOR_CALL(148)→el0_hvc_handler(代理 hvc,含
XEN 式 magic imm)——直通面。**寄存器 ABI:号在 x8、参
x0-x6(七参,比 Linux 多一)、返回 x0(负 errno)**;大量
调用走"单指针指 uapi 事参结构+copy_from_user"而非多寄存器
(uapi/syscall_arg.h=共享结构定义,非寄存器 ABI)。入口
守卫:SPSR I/F 位被屏蔽直接 -EPERM。

**向量表**:current-EL SP0 槽全路由 panic(**SP_EL0 复用为
current TCB 指针,Linux 手法**,不能当栈);0x400/0x480
lower-EL sync/IRQ 跳 vector_sync/vector_irq;AArch32=死循环
(无 32 位用户态)。陷入帧=全量 x0-x30+FPU/SIMD+far/esr/
spsr/sp_el0/elr。

**分发**:Linux 式静态表 sys_call_table[305] 全填 ni_syscall
后由 unistd.h 的 __SYSCALL 列表覆写;路径=svc→帧存→
el0_svc_common(ori_x00 备 restart+trace 钩子)→表[x8](regs)
→SWITCH_OUT 按帧内 SPSR 判用户/内核帧,用户帧先递送
信号再 eret。实现集中单文件 reference_ksyscall.c(5496 行,
~300 stub,其审计 Q4-01 自认债务——新仓按子系统分文件)。

**任务/进程**:统一 ktask_t(~190 行大结构;is_proc/tgid/
task_group/mm/pid(idr 分配 0x40-0x7FFF)/信号面/亲和/策略/
security_mgt);**进程=task_group_t**(pid/pgid/sid+资源三
链+fdt_gp fd 表+组锁+状态机);capability 结构含
**incapr(PM 管保护权)映射进用户地址空间供用户态 PM
仲裁**——能力模型跨内核/用户两面的形态。

**生命周期链**:UTASK_CREATE(进程内建线程,继承
mm/group)/UPROCESS_CREATE(leader+全新 mm(PGD+ASID)+
task_group+security,挂起待 autorun)/COPY_UCONTEXT(fork:
整帧拷+子 x0 魔数辨 child+fdtable 组拷)/PREPARE_EXEC/
EXEC_SETUP(**7 参直改保存帧:pc/sp/x0/spsr**)/EXEC_START
(ptrace 事件后放行)。**镜像装载=用户态 loader/PM 驱动
(内核内 elf_load 无人调用),内核只给 ipc-mmap+上下文
改写**——与 §8 PM 段互证;initrd 由 DTB 解析、内容用户态
消费后 INITRD_MEM_FREE 归还 buddy。

**调度器修正(改写 §4.1 措辞)**:RT 位图(K_CONFIG_PRIO_MAX
=141 级链+64 位字位图+最高位缓存;SMP=全局 rt_rq+per-cpu
local_rq;RR 到期转尾)=**主导路径**;k_cfs 仅在 RT 队列
选到 idle 时咨询(背景类,永不抢占任何 RT 任务,nice 区间
限定)——**"CFS 排除"精确化为:CFS 背景类不移植**,
新仓 nice/背景带后置观察哨。tick 睡眠=tick_list/tick_match。

**溯源分层**:参考RTOS=改名衍生(参考RTOS_→K_ 前缀统一重命名+
版权头改写+SMP 双队列重构,非逐字上游——比"原样搬"轻,
但结构同构);arch/arm64 syscall/uaccess/锁/ASID/mm_struct=
Linux 同构层;参考实现=自有编号集/内核协调层/同步队列件/
fdtable/vmmgr/security(CAPR)/signal+futex+shm+posix_queue/
vdso/aps+HVC/SMC 代理。

## 8. 地址空间机器细节(普查回填)

**四层架构**(引用面:参考实现 core/{mm,vmmgr,内核协调层}+arch 页表引擎):

1. **arch/comm/mm/mmu.c=页表引擎**(__create_pgd_mapping/
   mmap_user/munmap_user/mmap_kernel/fixmap;arm64 3/4 级、
   4KB granule、PUD/PMD block+contiguous PTE;血统判定
   Linux 改造型,中高置信);
2. **core/mm=物理面**(buddy 页分配/SLUB/memblock 早期分类
   登记(dma/cma/kdshmem/devmem…)/k_mm 堆(开源RTOS k_mm 同构)
   /mmu_gather 批量 TLB 失效(按 ASID 发 TLBI+延迟页表页回收)
   ——Linux 衍生层);
3. **core/vmmgr=VMA+消息服务**(vmm_struct 每进程:VMA
   rbtree/brk/mmap_base/rlimit/shm_fds/xspace 面;**vmmgr_server
   双消息分发**:VMM 自用调用(170) 自用段 BRK/MMAP/
   MAPDEV/SHM…+VMMGR 调用(171) PM 段 SETUP*/CLONE/
   DESTROY——**exec/fork 的地址空间生命周期由用户态 PM
   进程经消息驱动,内核不读 ELF**);
4. **core/内核协调层=进程协调**(其用户进程创建调用(PGD 清零+
   asid_alloc)/exec_setup/copy_ucontext(fork:x0 魔数辨 child
   +TLS+优先级+fdtable 组+信号面)/pfault 协调)。

**xspace 定谳(纠我 spec 草案直觉)**:非每进程对象——
**全局唯一共享 VA 窗口**(用户区顶端固定段,单例 vmm+4096
xvma 表,bitmap+hint 分配 id,每 xvma 一锁+引用计数):
VALLOC 占位→PMAP 物理映射(首进程 phys_amap 分配可回传
pphys,后进进程同虚址 xshmap)→VMAP 后进者挂既有 xvma→
MAP_DEVICE 多进程同址设备窗;与 shm(用户区动态 VA+缺页按
页补)互补两轨。

**ASID/切换**:16bit 位图;context_switch 中 next->mm≠prev
才 switch_mm(TTBR0=PGD 物理|ASID<<48);另按 priv_enable
差异切 SCTLR_EL1.UMA(PAN 类);VMID 属 stage-2(参考实现域,
rk 不用)。

**demand paging=消息化缺页协议**(新仓 IPC 弧关键参照):
EL0 sync 异常→fault.c 按 FSC 分表→pfault_proc→按 VMA 型:
文件=阻塞+组 file_fault_msg **发 PULSE_CODE_PFAULT 给该 VMA
登记的文件服务器通道**→用户态 FS 装页后 PFAULT_CONTINUE(160)
唤醒复核(写保护路径改写保护);lazy stack/shmem/anon=内核
直补。跨进程拷贝引擎=ipv 族(逐页查源表物理再写目标表,
允许源空洞先触发源缺页)。

**Type-2 参考集成**:其 Type-2 映射调用 fd={dev_type,vmid,subdev};
vcpu fd 映射 run 态页(KVM_RUN 同构)+MMIO ring 页入用户区
——Type-2 直通窗。

**溯源标记(红线执行表)**:
- Linux 衍生(GPL 面):core/mm 物理层+qspinlock+arm64 asm
  头+arch mmu(疑)——新仓参照物=公开 Linux 文档/源码机制
  (读 GPL 码学机制合法;Apache-2.0 新仓零拷贝);
- 开源RTOS(Apache-2.0):调度基座/k_* 文件/k_mm 堆——
  新仓可直接对照公开上游实现;
- 参考实现自有(MIT 标):vmmgr 消息服务/xspace/内核协调层 协调/
  pfault 协议/ipv 族/CAPR 钩子——按"机制形状"白描参考。

## 9. IPC 消息机器细节(普查回填)

**通道模型**:全局 4096 项位图通道表;通道对象含接收等待队列
(信号量式资源计数)+发送优先级链+已收未答链+cookie 表
(默认 128)+状态机(OPEN/CLOSING/CLOSED)+属主 pid/task_group;
**连接概念退化**(attach 直返 chid,无 coid/每连接对象/断连
脉冲——参考实现审计 A2-01 自认);rcvid=32 位复合(cookie_id<<16
|chid);cookie=id/recv_id/reply_ref/snode 四元组 slab 分配。

**三段消息语义**:SEND(≤1024B 短消息拷内核栈缓冲/>1024B 长
消息映射预转换;按优先级插链+挂起自身+唤醒一个接收者,可被
信号打断)→RECV(无消息则 pend 支持超时变体;摘首+短拷/长
按映射拷入接收者 iov;返回 rcvid+消息元信息(长度/发送者
优先级/tid/tgid/sid/pgid/128B 扩展))→REPLY(按 rcvid 定位;
≤1024B 写发送者预留 reply_buf,长回复直写 iov;status 即发送
者返回值)——**QNX 镜像三段语义**。abort:未收走=直接唤醒
返回 ERESTARTNOINTR;已收走=发 PULSE_CODE_UNBLOCK 通知
服务器客户端已死(QNX unblock 脉冲镜像)。pulse=8B 固定
(code+value)/lpulse≤80B,异步不阻塞,rcvid=0 镜像;系统
码为负(UNBLOCK/PFAULT/MEMNOTIFY),用户码≥0;iov≤32 段/
总量 256MB。

**优先级传播=无**(全路径无接收者提权;与参考实现审计 C1-09
互证)——新仓站 30 必修项。

**fast 路径**:32B 消息经用户异常帧 x9-x12 四寄存器直传
(内容不进缓冲;发送侧不读 smsg 只校验非空,用户库装寄存器
约定不在本仓库)——参考实现自有微优化,QNX 无对应;普通 recv
遇 FAST 返回 ESRCH 互斥。

**VM_SET 真身**(纠"零拷贝"直觉):登记进程线性窗
[start,end)+offset,落在窗内的发送者 iov 地址内核按
addr+offset 直算物理→内核直接映射区**单次拷贝**到对侧
(非把发送者 buffer 映进接收者空间);窗外走逐页 walk 源页表
+缺页 pager;配 READV/WRITEV(接收者事后按偏移读写发送者
缓冲,MsgRead/MsgWrite 镜像)——形态=免页表遍历的单拷+
延迟访问,非真零拷贝。

**两层结构**:参考RTOS 原语层(queue/buf_queue 经 k_obj 通用
句柄表,传 void* 槽,无跨地址空间能力;posix mqueue 架其上)
vs 同步队列件 独立新层(专用对象类型+全局通道表,**底层
阻塞/唤醒复用 参考RTOS blk_obj_t/pend 机器**);每入口挂
CAPR 校验。

**APS 自适应分区**:100ms 滑动窗+1-tick 桶环形记账(per-cpu
计费起点);根分区"System"100% 起步,建新分区从中扣,根
保留≥10%/至多 90% 可分配/上限 32 分区;PART_TASK_JOIN 双
字段迁移在切换点生效;**溢出=软借用**(选任务跳过超支
分区,RT 候选全超支则回退最高优先级兜底),无 QNX critical
time/notify。**无鉴权**(审计 A2-05 互证)——新仓若立此站
需带 capability 门。

**用户态中断线程模型**:INTERRUPT_INIT 建事件表(内核
信号量槽)+硬中断换 IRQ_WAKE_THREAD 桩;WAIT 对槽信号量
可中断带超时 take;电平 IRQ 睡前 mask/唤醒按引用计数重放
unmask;用户"ISR 线程"醒后用户态处理——QNX
InterruptAttach/Wait 镜像(信号量载体);MSI=ITS/LPI 复用
入口(分配 INTID/注册 device_id/映射 device+event→INTID/
取 MSI 地址)供直通驱动。传用户函数指针版本=TODO 存根
(审计 A2-18 互证)。

**与真 QNX 的六处差距**(新仓站 30 修复清单,参考实现审计
Fix 列同向):①coid/Connect 实体+引用计数+断连脉冲;
②消息优先级传播(发送者阻塞→提权服务器线程,QNX
adjust_priority 镜像);③ChannelCreate flags 体系
(UNBLOCK/THREAD_DEATH/DISCONNECT/COID_DISCONNECT…);
④命名服务层级化(旧=全局平表≤256B 路径线性 strcmp);
⑤事件绑定族(ionotify/MsgDeliverEvent/SIGEV_PULSE);
⑥fast/VM_SET 为自创机制(QNX 无)——**新仓按"机制
参考+审计修复"双层取型:三段语义/pulse/abort/事件表
照参考实现,coid/优先级传播/flags 按审计修复向 QNX 补齐**。

## 10. 服务面+用户态+构建面细节(普查回填,第四路)

**futex**:全局 16 桶哈希(每桶 klist+自旋锁);key={tgid,
addr,is_shared}(shared 先转物理地址);等待队列内嵌 参考RTOS
blk_obj_t(复用 pend/post 机器);命令集=Linux 全集(WAIT/
WAKE/REQUEUE/CMP_REQUEUE/WAKE_OP/LOCK_PI/…/WAIT_BITSET/
PRIVATE_FLAG);robust list Linux 同形(上限 2048,OWNER_DIED
处理)。

**shm**:命名对象全 POSIX 面(shm_open/close/unlink/rename/
truncate/fcntl…);每进程 shm_fds 挂 vmm_struct,fork 时
clone;页后备(struct page 数组/页缓存两模式);**与 xspace
两轨并行**(shm=命名+fd+动态 VA;xspace=固定窗同址)。

**signal**:数据结构 Linux 同形(每任务 pending+sigqueue
链(静态池+堆回退+排队上限)/进程 shared_pending/blocked);
**投递点=SWITCH_OUT/do_ret_to_user**(内核返回用户态时点,
在被投递任务自己的用户栈造 ucontext/siginfo 帧,PC=handler、
LR=restorer、_sigreturn 返回)——handler 在目标用户上下文
执行,非他线程代跑;syscall 重启(ERESTARTSYS 族)同点处理;
致命信号→coredump。

**posix_queue**(PNODE/POCB):pqueue_t 环形缓冲(msg/stream
双 ops/定长变长/优先级)+pnode_t 服务端持久节点(读写阻塞
链/notify)+pocb_t 客户端 open 控制块(引用计数);poll/
notify(AND/OR/XOR 语义);**fd=通道三元组 {channel,p_chid,
node_type,node_priv,ocb_id}——fdtable 与 同步队列件 IPC 的
会合点**(QNX resmgr 模型:fd→服务通道→server);与 buf_queue
相互独立(参考RTOS 内核队列),跨进程主通道=同步队列件。

**CAPR**:每任务 security_t{process_capr,uuid[36],rlimit[16
(Linux 编号)]};内核句柄式 cap 表(256 条目块链+回收栈);
类型分内核侧(process/memory/ipc/irq/timer/event/shm)与
用户侧(iopath/file/dir/device/libso/socket/port/rlimit);
**selfcapr(用户可见 4K 页)+incapr(PM 管保护)+outcapr
(FS 导出)三面**;VFS_CHECK 由用户态 VFS server 的 open
路径调内核校验;各管理器 UUID 固定常量(审计:硬编码旁路
面)。**notifier**=Linux 式通知链(atomic/blocking/raw)+
业务通知(内存水位阈值→经注册进程 chid 投递)。

**基础库 组成**:applications(~60 测试工具:rtos_ps/top/
free、capability_set、ctrace/mtrace、xspace_test、sq_*/rt_*/
cfs/aps 测试)+base(**io 三层 QNX-resmgr 风:core/handler/
libc{iofunc,notify,ops}**+libc_syscall 分域包装)+system
(**PM**/ipc(mqueue/pipe/sysvipc)/network(unix_sock)/tty/
slog/time_service/drvfw/ham/coredump)+lib(comm/rtos/
security+libso.patch)+syscall 包装;**shell/init=busybox**
(3rd/busybox 当 /bin/sh 与 init,inittab 构建生成)。

**双 musl 世界**:宿主侧 eshine-musl-host=忠实复刻 开源RTOS
移植(__syscallN 改道 基础库 分发→自有编号;67 文件补丁+
**ar 手工把 基础库 目标文件并入 libc.a**;后期 Route B 改显式
-l基础库 不再源级并库);客 Linux 侧=标准 Yocto musl+良性头
补丁。宿主链接形态:-fno-pie/-no-pie、-Ttext-segment=
0x400000、静态;rootfs=扁平 staging→其打包脚本→init.cpio。

**yocto 构建面**:eshine-rtos.bb do_compile=git archive 单仓
子目录进 WORKDIR/rtos(内核/虚拟化/板级/基础库/文件系统/
安全件/自测件)+**~60 枚工具链切换补丁**+
其 QEMU 构建脚本 按序;deploy=init.cpio+kernel.dtb+
rtos-uImage;"rootfs-rtos-host tier"实体=type2机器族
(rtos host+guest Linux);eshine-boot.bb 按平台/型别组装
(型A=RTOS 宿主 EL2 起 guest;型B=DOM0 Linux+DOM1 RTOS)。

**开机链**(站 31 的直接参照):内核 main→cpio_load_pm→
**cpio 内找 用户进程管理服务 的 ELF→内核 elf_load 建映射
→其用户进程创建调用 固定拉起首用户进程**(带 cpio 地址/大小
sys_cfg)→PM 起 pathname 服务→loader_task 按固定序列拉服务
(ramfs manager 用传参 cpio 解包挂 "/"→cap→coredump→sysfs→
procfs→random→sock→time→driver manager(含 tty)→slogd→
**busybox init**)→busybox 读 inittab:rcS 挂 /proc /sys→
respawn/askfirst /bin/sh。**init 形态=内核固定 spawn PM
(非 init= 协商),服务序列硬编码于 PM,壳由 busybox 收尾**
——参考实现审计 F3-18 记其僵化,新仓可保固定序列起步+留协商位。

**溯源(第四路)**:filesystem(vfs/proc/sysfs/ramfs)=
Eshine 自研;基础库=开源RTOS 上游底座(io 层 QNX-resmgr 风)+
参考实现系统件;security=开源RTOS 密码栈+AOSP trusty 移植+自有
KMS/HSM;core/base k_*=开源RTOS 改牌;mm/qspinlock/
arch signal 帧/futex robust=Linux 派生(GPL,vmmgr/xspace/
fdtable/posix_queue/CAPR/内核协调层=自研面;yocto/meta-eshine=
参考实现构建层。
