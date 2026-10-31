# 站二十七 devlog:微内核机制普查+路线 spec(参考实现参考基线)

> 承接站二十六(d3037b1+915862e,80 门双绿)。用户四指令
> (10-31):参考实现=机制参考基准(非 参考RTOS)/RTOS 终态=类 QNX
> 微内核/整个 eshine-os 用 yocto 编译/终局经 docker 发布。
> **红线定格:机制参考、零代码拷贝、参考实现权属私藏。**
> 纯普查站:零代码改动,门禁 80 不动。

## 0. 方法:两份一手档案+四路普查代理

- **一手档案 #1**:参考实现 `rtos-kernel/include/uapi/syscall_no.h`
  全 305 号表逐号读毕——服务清单直接证据。
- **一手档案 #2**:参考实现 `docs/optimization/rtos-vs-qnx-linux-
  2026-08-07.md`(361 行,13 代理 410k tokens 机制级审计)
  逐行读毕——参考实现形态(E)/QNX-Linux 参照(R)/修复建议
  (Fix)三栏,比源码更干净的参照面。
- **四路普查代理**(2.9M+tokens,红线遵守:白描零转录):
  ①syscall 入口+进程模型;②地址空间机器;③IPC+APS;
  ④服务面+用户态+构建面。产出落 spec §7-10。

## 1. 关键定谳(纠我此前直觉的五处)

1. **架构真身**:微内核式 userspace-server——内核持原语,
  用户态服务进程(PM/vfs_server/vmmgr_server/ramfs manager)
  经 同步队列件 消息驱动;fd=(通道,节点,OCB)三元组=
  QNX resmgr 会合点。
2. **xspace≠每进程地址空间对象**:全局固定 VA 共享窗
  (用户区顶端,4096 xvma 表),与 shm(命名+fd+动态 VA)
  两轨并行。
3. **exec=用户态驱动**:内核不读 ELF;PM 经 VMMGR_SYSCALL
  消息重建地址空间,内核只做 EXEC_SETUP 七参帧改写
  (pc/sp/x0/spsr)——站 29 可大幅瘦身。
4. **CFS=仅背景类**(RT 位图选 idle 才咨询,永不抢 RT)——
  "CFS 排除"精确化为"背景类不移植"。
5. **VM_SET≠零拷贝**:线性窗登记+内核直接映射区单次拷贝
  (免页表遍历),配 READV/WRITEV 延迟访问。

## 2. 溯源三档(红线执行表)

- **开源RTOS 改名衍生**(底座:core/base k_* 全家/调度
  位图/blk_obj 机器;k_mm 堆):新仓可直接对照 **Apache-2.0
  公开上游**;
- **Linux 派生**(core/mm 物理层/qspinlock/arch syscall 面/
  signal 帧/futex robust/ASID/mm_struct):参照**公开 Linux
  机制文档**(读 GPL 学机制合法,Apache 仓零拷贝);
- **参考实现自有**(同步队列件 QNX 镜像/xspace/vmmgr 消息化/
  内核协调层/fdtable/CAPR/开机链/构建层):按**机制形状**白描
  参考——红线执行的主战场。

## 3. 与真 QNX 的六差距(站 30 修复清单,参考实现审计同向)

coid 实体退化/消息无优先级传播/无 channel flags/命名平表
线性扫/无事件绑定族/无 Qnet;fast 寄存器路径与 VM_SET 为
参考实现自有(QNX 无对应)——新仓取型=**机制照参考实现+审计
修复向 QNX 补齐**双层。

## 4. 产出与战况

- **spec 定稿** `docs/specs/2026-10-31-station27-microkernel-
  survey.md`:§0-10(架构定谳/26 站资产映射/站点序列/六项
  偏差决策/红线纪律/四路普查细节)。
- **偏差记录 #24-30**:基准切换/CFS 背景类/APS 暂缓/
  标准 __NR_* ABI/攻击面 ~100 原语/CAPR fail-closed/
  rootfs+musl Route B。
- **站点序列定稿**:28 EL0+syscall 骨架→29 进程+地址空间
  →30 消息 IPC→31 server+rootfs→32 signal+futex→33 musl
  /基础库→34+ yocto 弧→末 docker 弧;每站 DoD 不变,80 门
  只增不减。
- 门禁 80 不变(纯普查零代码);树 d3037b1+915862e 未动。

## 5. 历史审查补注(诚实账)

- 参考实现沿革:参考实现工作区(多板平台)→08-03"全开源"
  重构→12 fresh 仓→08-05 monorepo(剥 开源RTOS 历史)→
  **08-07 干净副本=本仓**;08-05 任务书=高可靠高安全开源
  OS(自动驾驶/机器人/医疗,守护生命)。
- 站十六"参考RTOS+不翻树"当时入册为用户指令(设计记录 #20
  "用户明示不翻树");今日基准切换同为用户令,后者为准——
  两次用户决策都是真,沿革如实记。
- 用户称参考 RTOS"借鉴 Linux 的类 QNX 微内核"比站十六
  "参考RTOS 任务内核"定性更准(audit 旧判"RTOS'Linux 味
  微内核'"佐证)。

## 残余

- 执行未定项(站 29/33 设计期终裁):运行级(参考实现 rk 有
  VHE 双路径,新仓 RTOS=EL1 guest 不涉)、xspace 窗口在
  新仓的取舍(新仓 vm0 有 stage-2 隔离,xspace 的设备窗
  面向直通场景再评)。
- 下一站:站 28(EL0+syscall 骨架)按常设授权开工。
