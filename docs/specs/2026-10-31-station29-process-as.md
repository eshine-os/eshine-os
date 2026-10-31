# 站二十九 spec:进程+地址空间(per-process stage-1/ASID/缺页)

日期:2026-10-31 · 作者:Julian Chu + Claude Code
基线:参考实现普查 spec(站 27 §7/§8)机制白描;红线:机制参考、
零代码拷贝、参考实现权属私藏。
前置:站 28 收口 c93b5ad(81 门双绿,EL0 壳+syscall ABI 在役)。

## 0. 目标

EL0 从"单一用户段全体恒等"升格为**每进程地址空间**:每进程
自持 stage-1 用户半区(独立 L0/L1/L2),ASID 位图分配,调度切换点
按任务装 TTBR0;EL0 缺页从"诚实终态面"退役为**按 VMA 申报补页**
(demand paging),申报外野访仍走终态面(v0 边界诚实保留)。
进程生命周期三态:CREATE(挂起)/COPY_UCONTEXT(fork)/EXEC_SETUP
(**帧改写式 exec——内核不读任何镜像字节**,参考实现 loader 在用户态
的机制形状;v0 由同段二相函数替代用户态 loader)。

## 1. 机制映射(参考实现 → 新仓,站内尺度收编)

| 参考实现机制(§7/§8) | 站 29 新仓形态 |
|---|---|
| 统一 ktask_t 大结构+task_group_t 进程对象 | rt_task_t 尾扩 ttbr/proc 两槽(@696/@704;asm 契约位 @672/680/688 不动)+ rt_proc_t 轻进程对象(pid/asid/pgd/VMA/parent/exit_code/state) |
| 每进程 PGD+ASID 位图+切换点 | rt_proc_t 自持 L0/L1(静态池)+按需 L2;ASID 16 位位图;TTBR0=asid<<48\|L0 在 RT_SWITCH_BOOKKEEP 里随任务装(内核任务=内核 PGD,asid 0) |
| PGD 高低半区布局 | 同一 L1 内半区:块 0=内核 EL1-only(恒驻每进程)+块 7=DTB EL1-only(内核读 fdt)+块 1..6=用户按需 |
| EXEC_SETUP 7 参帧改写(内核不读 ELF) | sys_exec_setup(entry,sp,a0..a3):直改 SVC 帧 pc/sp/x0..x3/psr——内核零镜像字节知识,by construction 不读 ELF |
| COPY_UCONTEXT fork(x0 魔数辨 child) | sys_fork():父帧整拷,x0=RT_FORK_MAGIC;子进程 AS=**eager 全拷**(vm0 无 COW——表+页直拷进页池,规格从简,站 32+ 再议 COW) |
| demand paging(文件=消息协议/lazy=直补) | v0 只做 **lazy anon**:EL0 翻译错→rt_pfault 查本进程 VMA→identity 段(镜像/栈)补 VA→PA 恒等页;池段(mmap)补页池零页;VMA 外=-1 落终态面 |
| vmmgr BRK/MMAP/MAPDEV 消息面 | 站 31 server 化;本站内 sys_mmap/sys_munmap(ANON 零填)直入表 |
| TLB:按 ASID 发 TLBI+mmu_gather | v0:进程销毁 tlbi vmalle1(简单正确);切换靠 ASID 标签免冲刷 |

## 2. 地址空间几何(站 28 两区式的自然延伸)

- VA=IPA 恒等不变;L0[0]→l1_dev(设备窗,恒驻每进程,EL1-only+XN+PXN)
  ;L0[1]→本进程 L1。
- 每进程 L1(512×2MB 槽):[0]=内核块 EL1-only(常量共享);
  [7]=DTB 块(0x40E00000-0x41000000)EL1-only(内核 fdt 读);
  [1..6]=**表描述符**→本进程按需 L2(512×4KB/表)——
  L1 块粒度→L2 页粒度的换装=本站核心变更,站 28 的恒等块退役。
- 用户窗布局:
  - 块 1(0x40200000-0x40400000):壳 .user 段(镜像+栈)——进程 1
    identity 申报;
  - 块 2..5(0x40400000-0x40C00000,8MB):**页池**(2048×4KB 静态位图
    分配器)——fork 拷贝页与 mmap ANON 的后备;
  - 块 6(0x40C00000-0x40E00000):保留(v0 不映射);
  - 块 7:DTB(见上)。
- 内核 PGD(boot mm0):块 0+块 7+设备窗,用户块 1..6 置 0——
  EL0 永不在内核 PGD 上执行;内核任务/引导跑内核 PGD。
- 表池(RT_PROC_MAX=4):每进程 L0/L1 各 4KB+L2 池 16×4KB——
  全部 .bss 静态(内核 2MB 块余量 ~1.8MB,充裕)。

## 3. 切换点与故障面

- **切换**:RT_SWITCH_BOOKKEEP 增一段——新任务 ttbr!=0 则
  `msr ttbr0_el1`(asid<<48|L0)+isb;ttbr==0=内核 PGD(装 mm0_l0)。
  每切换必装(微代价换正确,ASID 标签保 TLB 存活)。
- **缺页**:guest_el0_sync 非 SVC 分支改调 rt_pfault(frame):
  - FSC=翻译错(0x04..0x07):查本进程 VMA→命中:
    - identity 型(镜像/栈):l2[页]=VA 恒等描述符(EL0+EL1 RW);
    - 池型(mmap):页池取页+清零+l2 挂页池 PA;
    - l2 缺则先从 L2 池取表+l1 挂表描述符;
    - 返回 0→SVC 帧原样 eret(ELR 未动,重执行即中)。
  - VMA 未命中:原样落 rt_el0_fault 诚实终态面(v0 边界不变)。
- **生命周期**:
  - rt_proc_create(parent):建进程(CREATE 挂起态)——pid/
    ASID/L0/L1 池分配,VMA 空表;boot 为壳建进程 1+申报
    .user 镜像/栈两个 identity VMA+eager 补页(首 SVC 前无缺页
    悬念);任务 ttbr=(asid<<48)|L0。
  - sys_fork():COPY_UCONTEXT——子进程+子任务(EL0 帧整拷,
    x0=RT_FORK_MAGIC);父已映射页逐页拷入页池+子 l2 挂池页;
    子 VMA 表整拷。
  - sys_exec_setup(entry,sp,a0,a1,a2,a3):帧改写(pc/sp/x0..x3/
    psr=EL0t)——不碰任何镜像字节。
  - sys_exit(code):子任务 DEAD+页池回收+l2 清+tlbi vmalle1
    +ASID 释放+唤醒父(挂 ZOMBIE 带 exit_code)。
  - sys_wait4(pid,&status):父挂起(同步等待机器,站 17)至
    子 ZOMBIE→写回 status+回收进程槽,返子 pid。

## 4. syscall 号面(站 27 偏差 #24 的续行:标准号优先)

| 面 | 号 | 语义 |
|---|---|---|
| exit | **93**(标准 __NR_exit) | 进程退场(触发回收链) |
| mmap | **222**(标准 __NR_mmap) | ANON 零填段:页池 VA 段分配+VMA 登记,lazy 补页 |
| munmap | **215**(标准 __NR_munmap) | VMA 摘除+池页回收+l2 清+tlbi |
| wait4 | **260**(标准 __NR_wait4) | 指定子进程收尸(子集语义:无 options 位) |
| fork | **520**(扩展) | COPY_UCONTEXT(eager 全拷;非 Linux clone 旗语,诚实独立号) |
| exec_setup | **521**(扩展) | 7-参帧改写式 exec(参考实现 EXEC_SETUP 形状;非 execve) |

## 5. 判据面(门禁只增不减:81→84)

- [78] forkdemo:fork→子打印+exit(42)→父 wait4 收
  "reaped pid code=42" 全生命周期一线。
- [79] mmapdemo:mmap 一页→写魔数→读回=魔数(lazy 补页+零填
  自证)→munmap。
- [80] exedemo:exec_setup 二相入口+新 sp→"phase2 entered"
  (帧改写自证:pc/sp/x0 三改全中)。
- 旧 [78-81] 顺移 [81-84];宿主壳走查表 81→84 步;ARCHITECTURE
  同步;guest help 补三命令。双绿=84×2 连续。

## 6. 边界与不做(v0 诚实)

- 无 COW/无 write-protect 缺页/无文件缺页(消息协议=站 30/31
  接管);无 brk(壳无动态链接);无 ASID TLBI 精细化(销毁全冲)。
- 内核 PGD 用户块清零后,**用户缓冲 uaccess 只在进程 PGD 语境
  合法**(SVC 帧内恒真);站 28 的 access_ok 纪律不变。
- 帧契约不变:.Lpop/.Lel1/蹦床零改动,仅 BOOKKEEP 增 TTBR0 段。

## 7. DoD

spec→实现→84 门×2 双绿→devlog→内容包 5→commit→镜像机→记忆。
