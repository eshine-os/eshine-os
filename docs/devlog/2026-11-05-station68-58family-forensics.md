# 站六十八 devlog:58 挂脸族全案取证(仪器化+活体解剖)

> 承接站 67。用户令"继续下一站任务"——站 68=58 挂脸族专
> 场(boot 20-75% 挂:58 族静默楔/panic 楔/空格洪水三脸,+
> bsh REPL 1/4 活率纠缠)。**本站性质=取证专站**:判据先于结论,
> 仪器先于判据;不做未定谳的修真(切换器补丁错一寸=最大自雷)。

## 0. 三脸同族证据链(活体标本×3)

**脸谱 A——panic 楔**(census boot3):`rtos panic: unhandled sync
exception, task: shell, esr=0x2000000, elr=0x40188f50, far=0x40d1f000`。
elr 无符号——**0x40188f50=&pool[5]**(0x40188000+5×784):壳在
EL1 语境把一个任务池槽地址当代码执行;ESR=0x02000000=EC0+IL1=
PC 对齐错=垃圾指令流再跳非对齐地址终爆。

**脸谱 B——静默楔**(boot2/7,守卫标本):死于 initmark 后、gate
open 前,零输出。活体:vm0 RUNNING、vcpu0 WOKE、CPU 停在
guest_park(EL1 wfi 环)、**rt_ticks 冻在 8**(80ms)——主核
楔死→vtimer ISR 不再走→一切 sleep_ticks 永睡→无声。

**脸谱 C——洪水**(probe5 双靴):gated hello 首个 printf 死在
"pid=" 处,接 **8.4MB 纯 0x20**:精确 134B 周期("[vm0] "+128
空格)=vuart 行缓冲满冲刷再满;零换行、零其他字节、guest 活环。

## 1. 第四镜仪器与三把新钥匙

- **gdb `monitor xp` 物理读透传**:QEMU gdbstub 的 VA 读走当前
  CPU 翻译体制(EL2 语境 0x50xxxxxx 无映射=read 空),且
  **monitor 输出走 stderr**(stdout+stderr 合流才见)。物理读
  =`monitor xp /Nxw 0xADDR`,guest RAM 与 EL2 BSS 皆可读。
- **VA→PA 平移 +0x10000000**(rtos.elf 链接 0x40000000,装载
  0x50000000):pool=0x50188000、rt_ticks=0x5015e450、
  rt_kstacks=0x50173fc0、rt_el0_tramp=0x50187fc0(64B=双核 32B)。
- **HMP `info threads` 四核全景**:哪台物理核跑哪个 vcpu、EL2 泵
  还是 guest 语境,一眼定分身(踩过的坑:1237 端口 TIME_WAIT
  竞态——每靴轮换端口;探针端口占用=census 假 hang 全军覆没
  的旧雷再现)。

## 2. 守卫仪器(tick 扫描面,三谓词)

`rt_task.c` 栈域双守卫 + 队列孤儿守卫(站 68 代码面):

1. **sp 域谓词**:活任务 sp 必落自身栈域(EL0=本槽内核栈 4KB;
   内核=本槽栈行)。
2. **ksp 恰等谓词**:EL0 子 ksp 必恰等于本槽内核栈顶(创建时
   一次性写入,变值即毒)。
3. **READY 可达谓词**(`rt_sched.c rt_rdy_find`):READY 任务必可
   在其亲和层链上走达——**孤儿=队链丢失的直接证词**。

证据面=受害者五元(slot/名/态/el0/sp/ksp/期望域)+双核 cur+ticks;
纪律同 rt_panic(全掩+单次+裸 uart+park)。**开火实录**
(probe5b):`task: child slot 5 st 0x3(BLOCK) el0 1,
sp 0x40187ed0 dom 0x40178fc0-0x40179fc0 ksp 0x40179fc0(正确),
cur0=idle cur1=idle1, ticks 19`——**gated fork 子在首个 loader
fdread 阻塞时,sp 字段被写成蹦床域地址**(ksp 无恙)。

另配 **writev 巨 iov 绊线**(洪水脸仪器):musl `__stdio_write`
rem 下溢后 base 恒进、len 恒巨,每次请求 64KB+;合法写永不过
64KB,首发打证(fd/base/len/任务名/pid)后续静默。

## 3. 复合帧解剖(毒化的直接形态)

蹦床域帧 0x40187ed0(+272=0x40187fe0=core-0 蹦床槽尾=SP_EL1
不变量值):**头部=内核调度器语境**(psr=0x60000004=EL1t 开中
断,elr=rt_daif_load=rdy 锁窗出口,x4-x10=&idle_task/&rdy_head/
&rdy_lk=rt_pick_ready 现场,x18=&rt_cur_arr)+**尾部=EL0 用户
残余**(x26=0x4020c158=rt_shell_task 的 .user 窗,x27=0x40d1fec0=
loader 用户栈)+**x30 槽=&pool[5]**(任务指针!)。恢复此帧→
`ret` 跳池数据→执行→PC 对齐错→**SP0 同步槽=guest_park 静默楔**
(脸 B)/**SP_ELx 同步槽=rt_sync_fault panic**(脸 A)——两脸
一dễ,分道仅在故障取的向量槽。

帧尾三槽恰=RT_EL0_ENTER 蹦床 scratch 布局({x11,x9,x10}@蹦床
域)——**被截断的推送+scratch 覆写**的复合形态。

## 4. beater 脸正身(内核全健康的用户态楔)

probe7 活体(死亡点:beater start 后):**rt_ticks=48261→54050
仍在走**(内核 100% 健康,泵/泊位全正常,vcpu0 的 ws=1 只是
tick 间隙泊位);池转储:S00 shell BLOCK(wait4 收尸等)、
**S05 child READY+pd=137(陈旧超时臂)+src=sem——READY 孤儿**:
被醒而队链丢失,调度器永不再选,idle 空转到地老天荒。守卫
(sp/ksp)全静默=此脸无字段中毒,**第三谓词(队列可达)为它
而生**。

## 5. 家族画像与根因候选(待守卫在案发时刻的终证)

三脸+REPL 挂共用一个损伤面:**fork/exec churn 下的队列/唤醒
/帧机器损坏**——

- 队列链损坏→孤儿(child READY 不在队=永不被选=静默);
- 链写泼溅→任务指针落在帧槽/sp 字段(x30=&pool[5]、child sp=
  蹦床域)=守卫标本;
- 恢复毒帧→池数据执行→park/panic 分道;
- 用户页/回执损坏→musl 状态走飞→空格洪水(绊线候证)。

静态排雷(全数过检):syscall 分发有界(nr<ES_NR_MAX)、IRQ 分
发线性扫描非空判、sem take 单 DAIF 窗、wake_sleepers 核 0 单写
者化(站 20)、wait4 判-挂-摘同窗。**残余候选窗**:rdy 队列操
作在跨核 IPI/令牌回流(requeue/preempt_requeue 自环防线=站 19
注释自曝"双份入队→tail->next 自环")与 EL0 双栈库蹦床
(RT_EL0_ENTER/RET 的 SP_EL0/SP_EL1 换库序列)的交叉。

## 6. 定谳与修真(root-a 收口,root-b/c 武装待续)

**第五仪器(队链全审计)开火即定谳 root-a**:每靴 tick 1-2,
`rt_irq_dispatch` 出口对 rt_cur=引导期 idle 照走
`rt_preempt_requeue`→**idle(池外伪任务,不占池是设计铁律)
被塞进 prio 31 就绪队列**(2/2+冒烟 2/2 全复现,守卫四元直证)。
入队后:空队 pick 恒回 idle→调度器把 idle 当真任务切进(eret
陈旧引导帧);idle->next 字段落自家 q[] FP 保存区→队链写以
idle 结构为笔野喷——rdy_tail 野化后 `tail->next=t` 写进 BSS
任意处(rt_cur_arr=0x1、rt_ticks=代码地址、壳 sp=蹦床域帧地
址跨靴同值)——**三脸+孤儿+REPL 挂+洪水全部下游同根**。

**修真(单窒息点)**:`rdy_enqueue` 硬门 `rt_task_in_pool(t)`
——池外伪任务永不得入队;idle 上下文仍经 idle->sp 帧机器存还
(切换簿记不动此径)。**证效**:队列毒五靴全消;boot 首次完整
走通 governed 零守卫开火(全案第一靴);历史 boot 楔(脸 A/B)
与 A/B 对照中基线 2/3 早死面收敛。

**root-b(独立,活体抓两次)**:fork 子被醒为 READY 而 sp/ksp
全净、队链不可达(孤儿)——cur0=ramfs 服务器答中在场=回执路
径邻域;发生窗 tick 26-33(loader fdread 首阻)。全审计已升级
为四元分类(链野/幽灵/环/尾脱链),下次开火即分类指认写手。

**root-c(残面)**:bsh 首步与 REPL 交互残留概率挂(与站 66
记档的 RX/注入链同域);修真后一靴全通,概率面显著收窄未归零。

## 7. 本站交付

- **代码**:rdy_enqueue 池域硬门(修真)+五谓词守卫(sp 域/
  ksp 恰等/READY 可达/队链全审计四类/...)+writev 巨 iov 绊线
  ——全 tick 面常驻,健康靴零输出。
- **工具**:probe5/7/8 仪器猎场(全日志+证据抽取+活体解剖);
  第四镜钥匙三把(monitor xp 物理读/stderr 合流/端口轮换)记档。
- **判据**:58 族 root-a 定谳+修真+证效;root-b/c 仪器就位,
  案发即指认——下一站(68B)以守卫四元为判据续侦。

## 8. 教训

1. **probe 脚本的 gdb 解析曾三度全灭**(regex 行首裸 hex 无
   0x 前缀;monitor 走 stderr;TIME_WAIT 端口)——先验管道再
   信数据,与站 25 "对着坏仪器的 A/B 是空对照"同律。
2. **pkill 全局杀 QEMU 的手滑**(分面器拦下):自家探针 PID
   清单化,永不全局——本会话再犯一次,纪律执行者从人变成机器。
3. **"healthy idle" 曾误读两次**(cval≈ticks×period 既是健康
   泊位也是冻结尸面;ws=1 既是病象也是 tick 间隙常态)——判据
   必须双时点(ticks delta)+四核全景,单点读数必然误判。
4. **守卫自身要过冒烟**(第四谓词首版把设计常态当犯罪抓了
   2/2):仪器先在已知好世界验证(站 25 律),新守卫首秀=全量
   场景走查。
5. **A/B 对照是修真的牙医椅**:0/3 集中挂=回归嫌疑,1/3 随机
   挂=历史病——不对照就把残留当回归、或把回归当残留。
