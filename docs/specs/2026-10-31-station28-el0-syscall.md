# 站二十八设计:EL0 用户态 + syscall 骨架(微内核弧第一站)

> 承接站二十七(501134f,普查+路线 spec,80 门双绿)。参考
> 基线:参考实现 rtos-kernel 机制(站 27 spec §7);红线:机制
> 参考、零代码拷贝、参考实现权属私藏。本站=把壳与最小用户面
> 移到 EL0,立 SVC 入口+syscall 分发+两区式 stage-1 v0。
> 门禁 80→81(存量判据全部不动——壳的可见行为零变化)。

## 0. 范围与非目标

- **做**:RTOS 自开 stage-1 MMU(两区式 v0)/EL0 任务帧与
  切换/lower-EL 异常槽/SVC 入口+静态分发表/最小 syscall 面
  (write/read/getpid/sysinfo+eshine 扩展号段)/壳任务迁
  EL0(I/O 全走 syscall)/utest 门禁。
- **不做**(后续站):per-process 地址空间与 ASID(29)/
  消息 IPC(30)/服务进程与 rootfs(31)/signal+futex(32)/
  musl(33)。demo 全家(semdemo/…)暂留内核任务,标记
> 过渡态,30+ 其 syscall 面就绪后逐批迁。

## 1. 两区式 stage-1 v0(机制参考 §8 四层,裁到两层)

- **VA=IPA 恒等**(0x40000000-0x41000000),与 stage-2
  (IPA→PA 0x5xxxxxxx)零组合意外;EL2 的 vuart 陷阱
  (stage-2 EMULATED 无页)不受 stage-1 影响。
- **布局**:低半 0x40000000-0x40800000=内核区(AP=EL1
  only,设备页 XN);高半 0x40800000-0x41000000=用户区
  (8MB,AP=EL0+EL1 RW;壳栈+用户程序+用户堆)。UART
  MMIO 窗=EL1-only(**用户态永无直访设备**——参考实现纪律:
  I/O 走 syscall/server)。
- **机制**:4KB granule,39-bit VA 用 2 级表(16MB 全覆盖);
  页表自建 RTOS RAM;buddy/SLUB 不进 v0(静态分配+位图
  物理页池,29 站再评);**无 demand paging**(用户区静态
  映射,缺页=bug,PANIC 面);**uaccess=access_ok 窗口校验
  +显式拷贝**(用户区唯一窗,v0 无异常表需求,32+ 站按需)。
- **开机序**:boot 恒等区先活→建表→置 TTBR0_EL1+SCTLR_M
  →ISB 过渡→内核运行于映射态。**RTOS 自身 EL1 取指/访存
  恒等对齐,切换零扰动**(对照站 14 SP bank 教训:先归一
  再切换)。

## 2. EL0 任务机器(参照 §7 帧与入口)

- 统一任务帧(272B)增 EL 位(参考实现统一 ktask 同构:
  调度器不区分任务层级,帧自描述);`rt_frame_init` 按
  EL 填 SPSR(EL0h)+SP_EL0=任务栈;`rt_switch_to` eret
  照旧(硬件按 SPSR.EL 落 EL0)。
- **lower-EL 异常槽**(vec.S 现槽位被内核任务占满,补真
  处理):0x400 sync=SVC+stage-1 fault 分派;0x480 IRQ=
  tick/RX 进现有 el1 IRQ 路径(ISR→簿记→判切,帧存复用);
  帧存槽复用统一帧(x0-x30+FP+far/esr/spsr/elr)——
  **EL0 陷入帧与任务帧同构**=切换/抢占/续跑零特判(站
  16 elr 槽自描述投资兑现)。
- FP:CPACR FPEN=3(EL0/EL1 同开,站 15 已实证);per-task
  FP 帧照旧(EL0 任务同享)。
- SPSR 守卫(参照 §7):SVC 入口查 SPSR I/F 位被屏蔽
  →-EPERM。

## 3. syscall 面 v0(偏差 #27 落地:标准 ABI)

- **寄存器约定=Linux arm64 标准**:**号 x8、参 x0-x5
  (六参,参考实现是七参——登记偏差:超六参走结构体指针,
  与标准 ABI 对齐,站 33 musl 零 patch 直连)**;返回
  x0(负 errno)。
- **号表=asm-generic __NR_* 原样**(write=64/read=63/
  getpid=172/clock_gettime=113…)+ **eshine 扩展段
  __NR_eshine_base=512 起**(自留 sysinfo 等);静态表
  [NR_MAX] 全填 ni(-ENOSYS)后覆写——参考实现分发形态,
  **实现按子系统分文件**(不蹈 5456 行单文件,其审计
  Q4-01)。
- **v0 号集**:sys_write(fd∈{stdout}, buf, len → 控制
  台行原子)/sys_read(stdin 非阻塞窥+阻塞取——内核侧
  复用站 24 RX 唤醒不变量:ISR 排空入核环,用户 read 挂
  等待位,wake_all 同链)/sys_getpid/sys_sysinfo(版本/
  tick/运行态)/ni 面一ENOSYS。**壳的 rt_puts→sys_write;
  rt_getc_nb+rt_block_rx→sys_read**(shell I/O 全系统
  调用化,参考实现 打印调用 同位)。
- uaccess:buf 指针过 access_ok(用户窗)+拷贝;长度
  上限(单调用 ≤1KB,参照短消息界)。

## 4. 壳迁移与判据保全

- 壳任务 rt_shell_task 编为 EL0 用户程序(同一函数体,
  I/O 原语替换为 syscall 内联包装);**80 门全部判据
  不动**:rtos> 提示符/selftest OK/hb/echo 回显/各 demo
  输出逐字不变(门禁判据查行为不查层——迁移零判据损)。
- demo 任务过渡态:内核任务照旧(标记),30+ 逐批迁。
- **新门 [81]**:`utest` 命令(壳内)→EL0 用户往返:
  sys_write 直打/sys_read 回环(注入一字节)/sys_getpid
  ≠0/sys_sysinfo 版本串/ni 返回 -ENOSYS→
  **"EL0 syscall OK"** 终行;门禁 80→81(插入尾后,
  顺移纪律照站 26 先例)。

## 5. 风险与诚实边界

- **stage-1 开机窗**:建表→开 MMU 间的取指连续性
  (恒等映射下理论零跳变;首 boot 必以 -d int+逃逸取证
  确认无风暴——站 14 无声自陷教训)。
- **EL2 组合面**:vm0 泊位/唤醒(站 25 C 臂)全部 EL1
  侧行为,stage-1 不涉;若 [69]/[76] 门在任何轮红,先
  查 stage-2 组合(HPFAR 面变两段)再谈机制。
- **RX 唤醒链改形**:shell 从 EL1 任务变 EL0+syscall,
  station-24 不变量(输入⇒runnable)必须在新链上重证
  (push→ISR 排空→核环→read 挂等待位→wake_all)——
  [19]/[69]/[76] 门即见证。
- **栈防护**:EL0 任务栈在用户窗,哨兵/水位机器沿用
  (守卫读点在 EL1 tick ISR,权限无虞)。
- 参考实现参照限定:两区式是本站裁剪(per-process 在 29),
  非其原样——设计记录 #24 框架下属正常演进。

## 6. DoD

spec(本档)→实现(vec.S 槽/syscall/mm v0/壳迁移/
utest)→门禁 81 ×2 双绿→三处同步(help 81 表+
ARCHITECTURE 用户态节+verify)→devlog+内容包→
commit→镜像机→记忆。80 存量判据零改动是硬约束。

## 7. 实现期定谳(2026-10-31 冒烟战报,devlog 同日档全量)

spec 时点的设计在十一个运行时根因面前修正如下,以下为裁决性事实:

1. **帧契约=EL1h**:本 RTOS 全任务(psr=5/0x3C5 铁证)活栈驻
   SP_EL1,SPSel=1;SPSel=0 仅存在于 EL0 异常处理语境(经蹦床
   舞蹈进入)。§4 曾写的"恒驻 SPsel=0"为误记,已按证据改正。
2. **banked SP 直访禁令**:EL1 下 `msr/mrs sp_el0/sp_el1` 皆
   UNDEF(QEMU cortex-a57 三例实锤),唯一习语=spsel 换库+MOV SP。
   蹦床不变量由 eret→EL0 边界(.Lpop EL0 路/RT_EL0_RET)维护,
   guest_main/secondary_main 不再预臂。
3. **帧布局为紧打包**:elr@0 psr@8 x0@16(带 8B hole)x_i@24+8i;
   FR(i) 与蹦床回填偏移(96/104/112)均按字节偏移立契。
4. **syscall wrapper 须声明全 caller-saved clobber(x3-x18)**。
5. **两区几何**:T0SZ=25=39 位 VA=两级走(1GB 表→2MB 块);
   AP[2:1] 真值表:00=EL1-only RW,01=双级 RW,1x=只读。
6. **宿主 vtimer 陈疾挂账升级**:站 24 [41]/[69]("回复在而
   tick 冻结")被本站 EL0 壳撞上判据面,门 [24+] 间歇游走
   失败直至其专站根治;客人侧已尽落点加固(wfi 双 nop 垫)。
   DoD 之"81×2 双绿"在 vtimer 专站前不可承诺,WIP 检查点诚实
   入档(门 [1-23] 稳定绿)。
