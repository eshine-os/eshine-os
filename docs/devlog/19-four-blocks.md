# 站十九 devlog:四块合成站(A 闪断追查 / B 事件组+队列 / C per-task FP lazy / D guest SMP)

> 上半场(A/B/C 全落地 + D 核心通:vm2 第二 vCPU 真起,PSCI 走 HVC,
> ICC_SGI1R 陷阱转发,CNTVOFF 三核一钟)已在 cce125b/550c480 入库。
> 本篇收口:tick 坍缩追查至根因层、三修复落地、SMP 引导竞态残余定档。

## D 块收口:tick 坍缩的三层解剖

### 第一层:TLB 风暴(根因之一,已修)

- 现象:同一构建同一 QEMU 参数,vm2 单核靴到 "smp: Brought up" 的
  墙钟在 **0.4s 与 180s 之间双速漂移**;慢速相里每字符 ~18ms、
  控制台 3 B/s 级爬行,内核钟停在 0.2s 量级。
- 取证:perf 三 vCPU 线程全 100%,热点弥散于 `helper_ldul_mmu`
  (stage-2 软件页表漫游)、`translator_ldq`(重翻译)、TB lookup
  长尾——**TLB 全量冲刷风暴**的典型画像。
- 根因:`vcpu_run`/`vcpu_exit` 每次退出+进场无条件
  `tlbi vmalls12e1is`。真机上属浪费;QEMU TCG 把 IS 变种实现为
  三线程同步全量 flush(跨线程屏障+两级漫游全重来),睡眠型 guest
  (Linux)每个 park→wake 周期吃两发,100Hz tick 下即成风暴,
  模拟速率塌到 ~1/1000。站 18 的裸金属客不睡觉,从未触发;
  站 19 的 Linux 专泵第一次点亮这颗雷。
- 修复([vcpu.c](../hypervisor/kernel/vcpu.c)):
  per-pCPU `last_vttbr` 记账,**表值变化才 flush**;换表
  (轮转/出生/槽复用)仍熄旧表;生命周期口(vm_start/vm_finish)
  `vcpu_flush_all_tables()` 清零强制下一发,防同值重建页表
  泄给新客。实测:竞态赢面时 anchor 0.4s,靴至内核 20s 量级。
- VMPIDR per-vCPU、ICV 注入、WOKE 表扫路径不破(62 门 [1]-[30]
  实测过)。

### 第二层:专泵缺 26 兜底(已修)

- 现象:TLBI 修复后 vm2 靴最远到 20.1s 内核时冻结在
  `alternatives: applying`(第二道 stop_machine,首个跨核同步)。
- 取证:临时探针([P26H]/[P27H]/[P27G]/[PWOKE]/[PWAIT],提交前
  已拆净,strings 验证)证明:26 在三核 1Hz 稳定;27 到达时
  `cntv_ctl=5`(ISTATUS|ENABLE,过期挂线);停泊者 ctx 武装
  (`cctl=1`)+ 过期;`[PWOKE]` 置 2 后停增——**sweep 再发现
  断流**。
- 根因:专泵核(cpu1/cpu2)无 PPI26,而 `sched_vtimer_sweep()`
  唯一的周期驱动源就是 26(`el2_irq_host` 26 分支携扫)。无 26 =
  睡客的 vtimer 在途拍蒸发(27 被关线/ctx 熄火)后,唯一的再发现
  途径缺失,单次蒸发即永眠。
- 修复([smp.c](../hypervisor/kernel/smp.c) + [gicv3.c](../hypervisor/drivers/gicv3.c)):
  `gicv3_secondary_init` 改配 **26|27 双 PPI**;`secondary_main`
  补 `timer_init()`(CNTHP 本核硬件天然 per-CPU;宿主秒计数只由
  cpu0 记账防 3 拍/秒漂移)。

### 第三层:收尸窗误杀在跑客(已修,竞态未全闭)

- 现象:两修复后 vm2 靴最远仍只到 0.12–20s 内核时区间冻结;
  同批三轮试验全部冻死 ~0.2s(SMP 交接窗,27 最密集处)。
- 根因:`el2_irq_host` 27 三分支的第三支(无主陈旧挂起)就地
  `vgic_wake_waiter()` 写 `CNTV_CTL=0`——per-pCPU 的一刀,
  sweep 只认停泊者,**WFI 已入、簿记未竟的在跑客被误杀线**,
  出栈存得 ctl=0 按 RX 归类停泊,sweep 永不再匹配(判据要
  TIMER),一个 tick 即永眠。此为 [53]/[41] 在途拍族在 SMP 客
  上的放大面;单核靴实测冻死在首拍窗。
- 修复([exceptions.c](../hypervisor/kernel/exceptions.c)):
  第三支改为**只降线保挂**(`gicv3_eoi_drop`),停泊落定后由
  26 每拍扫描接管再发现,有界(≤1s)自愈;真无主由下一位进场的
  WOKE 熄火合拢。
- **残余定档**:修后三轮试验(含 vcpu0@cpu0 轮转世界与
  vcpu0@cpu1 专泵两种拓扑)全部冻死 ~0.2s 内核时(SMP 交接窗)。
  收尸软化补掉了误杀面,但 SMP 交接窗的 27/IPI 密度下仍有竞态
  未闭合:**站二十开站首项**(证据档案:/tmp/smpA/B/C/D 剖析、
  冻结 ccval/PWOKE 停增)。vm_cpu_map 维持 spec 原设计
  `{1,2}`(实验变体 `{0,2}` 无增益,已回退,理由入注)。

## 门禁与同步纪律状态(诚实)

- 62 门在 TLBI+26+收尸软化三修后实测:[1]–[30] 过,**[31] 区
  vm2-Linux 控制台判据超时**(竞态致靴爬行/冻结)——双绿未达。
- 由此,62→67 重编、verify 脚本 SMP 判据、shell help 三处同步
  **暂缓**([53]/[65] 判据需全靴到 busybox;spec §5 判据行
  "Bringing up 1 secondary CPU(s)" 实测不存在,真行是
  "smp: Brought up 1 node, 2 CPUs")。
- 观察哨:e1 样本留档,后续站若 SMP 客再冻结即重启追查
  (同站 18 [41] 纪律)。

## A/B/C 摘要(细节见 cce125b/550c480 提交与 spec §1-3)

- A:[41] 闪断追查结案——预算封顶不复现,观察哨移交;三分支
  (真唤醒关线/在途拍降线+回业主标志/无主收尸)结构定谳。
- B:事件组 32 位 AND/OR 条件注册表 + u64 环形队列(站 17
  等待队列机器复用,eventdemo/queuedemo 双 demo)。
- C:per-task FP lazy(fp_owner 单一持有 + FPEN 拨杆 + EC 0x07
  陷阱轮转,fpstat 计数判据 M<<N)。

## 追记(2026-10-28):tick 坍缩真凶定谳,五修全链闭环

> 上一收口(6e5f1ef)把 SMP 交接窗竞态立为站二十首项。本日追查
> 定谳:不是单一竞态,是**五层叠加**——每一层单独都能冻靴。

### 真凶链(按揭开的顺序)

1. **TLB 风暴**(6e5f1ef 已修):每 exit/entry 无条件
   `tlbi vmalls12e1is` 在 QEMU TCG 是三线程同步全量 flush。
2. **专泵缺 26**(6e5f1ef 已修):到期扫描唯一周期驱动源缺失。
3. **搁浅 SGI**(本日修):发送窗落在目标跑动期、目标又先停泊后
   进场,IPI 搁浅在软件挂起位无人取——泵的停泊复查只查串口/
   virtio,从不查 SGI,取走只发生在进场路径。主核 csd 自旋等
   ack + 副核 tickless 卸甲睡 = 整靴冻死。修:`vgic_sgi_pending()`
   入停泊复查判据(不限 reason;TIMER 停泊者早醒无害,ctx 照装)。
4. **sweep ctx 熄火**(本日修,撤站 18A 的 `vtimer_ctl=0`):熄火
   后进场装回 ctl=0,被唤醒的 guest 收到注入的 v27 却读到
   ISTATUS=0——轮询式 handler 判 IRQ_NONE 丢 tick,不重装下一拍,
   jiffies 冻死,两 vCPU 双双 ws=1/reason=RX/ctl=0 停在
   cpu_do_idle(直读帧现场定谳;站 14 注释早写下的死法)。
   修:撤熄火,装回武装过期值,进场线重挂,guest 认账(ISATATUS=1)
   重装即闭环;重挂窗的重复 27 由 el2_irq_guest 27 分支幂等吸收。
5. **三核并发自报互串**(本日修):console_puts 每次调用各持锁,
   分段印在三核下行内互串(`cpu`+`1` 被隔断,门禁 [3] 抓出)。
   修:自报与 psci 结果行本地拼整行一次 puts;自报后再握手
   (main 有界等覆盖整行)。

### 验收:靴通到用户态

vm2 双 vCPU 全靴:/init +2.0s 级,`cat /proc/cpuinfo` 出
**processor 0 + processor 1** 双行,`uname -a` 确认 #2 SMP PREEMPT
——站 19 D 块的原始判据([53]/[65])实体达成。62 门旧判据
(单 CPU 靴路径)全绿一次;67 门(62→67 重编 + -smp 3 +
[32]eventdemo/[33]queuedemo/[34]fpstat/[53]SMP/[65]cpuinfo 双核)
新判据逐段全过,其中 [53]/[65] 为站 19 核心目标的门禁化。

### fpdual 与 fpstat 的判据课(两段改写的雷)

fpdual 旧 11/12 异级串行(A 跑完退场清主 B 才起步),首陷阱恒见
旧主 NULL,saves 恒 0——fpstat 判据 M<<N 失去分子。第一版分段
sleep 强交织改写连爆三雷:asm `fadd %0,%0,d0` 把 GPR 传给 fadd
(编译炸,且 **rt_fp.c 编译失败后陈旧 rtos.bin 害测三轮**——
make 对 rtos 子构建的静默沿用,教训入册)、`fcvtzu "+r"` 覆写
非累加、段界 off-by-one。定稿:同优先级 11/11 + 两段精确分割
(Σ(1..700000)+Σ(700001..1000000) 恒等精确),段间一次
sleep 强制交替——**A/B 双精确 + 2 fp-saves << 26 switches**,
懒惰 FP 判据非零分子闭环。B 的跨段累加教训:v 寄存器跨 C 调用
是 caller-clobbered,活状态必须落在帧(GPR 只收口)。

### 残余(诚实,接 [41] 观察哨)

**[41] 闪断家族未死**:-smp 3 下被放大,随机打在门禁任意阶段
(两轮 67 门:一次 [63] vm2 echo 后无提示符、一次 [37] vm0 token
单见;探针流程多轮过)——站 18 e1 的同形(token 单见 1/3)。
双绿未达;67 门与三处同步(verify/shell help/ARCHITECTURE,
含 -smp 2→3 与 make run 同构)已随本日工作落盘,机制定谳
移交站二十(候选窗:LR2 单槽多 SGI 排队、fork/exec 的跨核
唤醒路径、console 锁 WFE)。

## 追记二(2026-10-28 下午):[41] 家族五案并发,全链闭环,双绿达成

> 站二十开站首项。取证面升级(vmrx 两域 dump + vmtrail 事件迹环 +
> QMP 客内存直读)后逐案定谳——五案互相独立,同形异质,一度互相
> 污染分析。最终:67 门连续双绿(含一轮 [54] 单发"spinning"超时
> 观察哨,1/8,未定谳,如实记录)。

1. **token_echo 心跳劈半(判据脆弱,[37] 主形)**:vm0 前台期心跳
   (合法并发输出)恰落型键回显段中间,"GOKA31717" 被劈两半,朴素
   count 判据把健康客判成闪断——全息画像(回显劈半/输出完整/提示符
   在,rrx/vuart/LR1 三面皆空)由 vmrx+QMP 拼出。修:剥除心跳行
   再计,双回显语义不变,真死客照样必炸。
2. **RTOS 壳判空-挂起失联(真竞态,rt_block_rx)**:UART ISR 恰落
   rt_getc_nb 判空与 BLOCK 发布之间,字节入 rrx 而 wake_all 只认
   BLOCK 态——末字节无后继中断,壳永睡。修:判空+发布+摘队同一
   DAIF 屏蔽窗,窗内迟到字节直接回顶重查;醒后 ISR 走既有回流令牌。
3. **vuart 跨核裸奔(真竞态,三核面)**:CPU0 shell(push/冲刷)与
   CPU1/CPU2 guest 陷阱(DR 读重评 ris / bg TX 追加)并发写同面——
   push 的 ris|= 与 DR 读的 ris=0 整写交错可"字节在环而挂起清零"
   (注入永不再来),bg 追加与冲刷对 tx_len 无序交错撕半行。
   修:console 锁(站18D,同核递归跨核互斥)串行化三处。
4. **vmconsole 冲刷序([62] 锚劈半)**:旧序"先冲半行再切 owner"下,
   新 owner 的在途无换行半行会被 "console -> vm" 消息从中间劈开
   (busybox "~ # " 无换行恰跨切换即碎)。修:owner 落位+消息先行,
   半行整体随冲刷面世。此修又暴露 [19] 裸 "rtos> " 锚误吞冲刷出的
   启动期半行("[vm0] rtos> "),判据链错位一格致 [21] "ps 行缺失"
   闪断——修:锚取"回显+提示符"成对(CRLF+rtos>),带 tag 的冲刷
   半行永不匹配。
5. **TX 前台旗按 vCPU 比([63] 锚失)**:双 vCPU 客的进程上下文被
   内核调度漂到任一 vCPU(vm2 的 sh 实测漂 vcpu1),fg=(v==owner)
   把业主 VM 自家另一帧的输出判后台——无换行提示符滞留 bg 行缓冲
   永不再冲刷。修:fg 改按 VM 比,与泵侧站 19D "双 vCPU 同 VM 皆
   前台"补齐一致。

取证工具沉淀为宿主常驻诊断面:`vmrx`(vuart 两域 + LR 面 + GICR
PPI 物理面 + CNTV/CNTHP 现场)、`vmtrail`(事件迹环:停泊/扫描
WOKE/泵入/注入/27 三分支/26 拍/RX 推送——本次定谳主力,QMP 客内
存直读配合)。教训:token_echo 的 buf 在异常路径上留在帧内,宿主
侧 forensics 拿的是调用前旧值,首版取证因此读错现场;"冻结"先量
墙钟再立论——[21] "10s 冻结"实为判据锚错位后的瞬时假阴性,客机
全程健康。
