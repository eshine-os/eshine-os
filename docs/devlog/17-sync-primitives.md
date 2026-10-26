<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# Devlog 17 — 同步原语站(信号量 + 互斥锁)

日期:2026-10-26 · 版本:27.04-dev · 门禁:58 步真绿(17c+17d 连续双绿)

## 里程碑

eshine-rtos 补上内核人格的最后一块:等待不再只有"壳任务等串口"一条路——
任何任务都能挂在一个对象上,等另一个任务来放:

    [vm0] rtos> semdemo
    semdemo: p1..p4        (producer 灌满 4 槽,挂等)
    semdemo: c1            (consumer 消费即 give,唤醒更高者立即让位)
    semdemo: p5            (生产者续跑……乒乓至 8/8)
    [vm0] rtos> mutexdemo
    mutex: give denied (not owner)
    mutex: waiter wants lock
    mutex: contender locking / locked / (睡 300ms 持锁)
    [vm0] rtos> ps        ← 活体状态证明
      contender prio 6  SLEEP
      waiter    prio 8  BLOCK
    mutex: contender unlocking / released
    mutex: waiter locked (after block)
    mutex: recursive take 2 ok

## 形态(参考RTOS 公开设计为基准,站 16 同款红线:参考实现一页不翻)

- **每对象 FIFO 等待队列**(拒绝广播惊群):与 RX 的 wait_mask 广播域
  分立——信号量挂等者 wait_mask==0,RX 广播按位匹配永不误醒。
- **契约:唤醒=允许重试,非授予**。take 是重检环(醒后自己复核
  count/owner),give 让渡后只唤醒队头。barge(被醒者跑之前被别人
  取走)只会重挂——无丢醒、无死锁;单核+唤醒即抢占下 FIFO 公平
  事实上成立。
- **唤醒即抢占**:give/handoff 在 ready_push 后 `if (w->prio <
  rt_cur->prio) rt_sched()`——统一帧的调用点即续跑点,give 返回时
  让渡已完成。更低优先则不让位(唤醒≠让位)。
- **互斥 = owner+递归 nest,无 PI**(YAGNI,记残余);非拥有者 give
  返 -1(判据用)。
- **短 DAIF 临界段**:count/owner/wq 检查-修改与队列链接全封窗
  (rt_daif_save/load,save/restore 而 ISR 语境 restore 原样归还);
  rt_sched/WFI 永不进窗。

## 实现期纠偏一:spec 免锁论证不完整(spec §5.5 已修正)

spec 初版写"单核免锁:ISR 不触同步对象"。设计期自查推翻:tick 抢切
能把另一任务插进两条指令之间——check+dec 不封窗可被双取到负数;
且 rdy 队从站 16 起就有任务/ISR 双写者交错的**丢任务潜伏窗口**
(任务侧 rt_sched 重排/handoff 入队 vs ISR 侧醒睡客/RR 重排,尾链
交错能把任务挤出链且 tail 与真链脱钩,后续入队全部落空——永久
丢失)。实际实现 = 短临界段,rdy_enqueue/rt_pick_ready 内嵌同款
收敛。

## 实现期纠偏二:首跑崩溃的真凶是站 16 潜伏 bug

C 块判据首跑,[26] 超时,vm0 以 ESR=0x82000006(取指中止,目标 0)
VM_DEAD。尸检链:

1. `-d int` 两发 eret 紧邻:一发进 rt_trampoline(首跑),下一发
   **eret 去 EL0、PC=0**——弹出了一个全零帧(elr=0/psr=0);
2. 故障 sp=0x4000d540 → 帧基址 0xd430,nm 对表:落在 fdt 指针区与
   g_vq 之间的 bss 空洞;
3. 0xd430 = task_stacks 基址 + 9×4096 − 272 = **"槽位 8" 的帧位**——
   而 RT_POOL_MAX=8,池里根本没有槽 8;
4. objdump 全链审计(原语/调度器/帧伪造的编译产物逐条对)——
   全部清白。最后回头读 `rt_task_create` 源码:**名字拷贝循环复用
   `i`,之后的 `rt_frame_init(t, task_stacks[i]+4096,…)` 传的是
   名字长度**。
   - shell(5 字)一直住在 stack[5];heartbeat(9 字)住在越界
     stack[9]=0xe430,**压着 g_vq 顶活了两轮门禁**;gate16/fptest
     (6 字)撞同一 stack[6] 但先后顺序复用从未并发。
   - **站 16 全靠"名字长度不撞车"活着**。
   - producer/consumer 双双 8 字——第一对同长并发任务共享 0xd430
     同一帧区,双任务互踩,栈混乱后弹零帧 eret 去 EL0 PC0。

修复三行(槽位号独立保存 `slot`)。**教训:名字/拷贝循环复用下标
变量,是和"noreturn 死代码"同级的静默杀手;两轮门禁绿≠无雷——
新判据第一次把雷踩响,门禁即尸检报告。**另外 B 块后的 55 步回归
绿是"demo 未入判据"的侥幸,不是安全证据。

## 实现期纠偏三:[53] 闪断归因(4 轮 1 挂)

C 块双绿认证跑(17b)在 [53] initramfs 步超时:Linux 内核时间冻在
0.489s(Unpacking 完成、Freeing initrd 8192K 后、/init 前的安静相),
VM0 心跳照常存活。归因 = **站 16 记档的残余物理**:

- `vgic_wake_waiter` 的**无条件关线**(cntv_ctl=0)+ **HW LR 的
  pending 由物理线驱动**——宿主相 v27 若带着未被消费的 pending LR,
  关线一落该拍蒸发;解包期的 Linux 没有任何别的中断来唤醒它重编程
  定时器,一拍冻即死。
- 站 16 加的 `vtimer_inflight()` 守门只护**抢占决策点**(在途则不
  切走),不护**宿主相 27 分支**;站 16 的 gating 实验撞过同一堵墙
  (不关线 → re-assert 风暴)。
- **真正的修法**:inflight 时改走"立即回业主 VM 消费自己的拍"
  (而非关线)——需要泵/中断路径的再设计,登记为**站 18 首选候选**
  (已有两次独立症状报告:站 16 [47] 期闪断 + 本站 [53])。
- 4 轮观察:绿/红/绿/绿;17c+17d 连续双绿按门禁规则认证。闪断不
  粉饰:当前机器的真实失效率记档,不追第 5 轮刷绿。

## 门禁 55 → 58(只增不减)

新 3 判据插 vm0 独占段([26-28]),旧 [26-55] 顺移 [29-58](降序
两遍重编号防级联,自校 0 陈 58 唯一):

- [26] semdemo 有界环 8/8:per-item 计数判据(p/c 各恰好 8——结束行
  刻意避开 "semdemo: p" 前缀互撞);
- [27] ps mid-hold:持锁睡 300ms 的窗口里发 ps,断言 waiter BLOCK/
  contender SLEEP 两行在场——**阻塞不是打出来的日志,是 ps 表里的
  活状态**;
- [28] 醒序与语义:unlocking < locked-after-block(阻塞-唤醒时序),
  recursive take 2 ok,非拥有者 give denied。

## 三处同步纪律:第二起嵌合体事故,本站根治

C 块开工自查抓出:**e87a3bb(站 16)commit message 宣称"三处同步
verify/shell help/ARCHITECTURE",但 shell.c 从未落盘**——help 还是
站 15 的 48 步陈表(旧 vm0 裸机走查 + 重复 [44]),version 还是
"rtos: not-yet"。ARCHITECTURE 是 55、verify 是 55、help 是 48,
三个数字三个时代。站 17 C 块整块重写根治:help 58 步表对 verify
真实步表逐行核,version 修为实态,ARCHITECTURE 纪律注立此存照。
教训强化:宣称同步≠落盘,**每站收尾必须肉眼过 help 输出**。

## 残余与后续

- 无 PI(低优先持锁阻塞高优先)、无事件组/消息队列/挂等超时、
  无 ISR-give 面(save/restore 临界段使其事实上安全,但未验)。
- per-task FP ctx 再议结论:仍不做——同步原语零 FP,安全前提不破。
- **站 18 首选候选:vgic 在途拍守门扩展**([53] 归因,见上)。
- spawn 单缓冲名互踩照旧(demo 名走静态串不冲突)。
