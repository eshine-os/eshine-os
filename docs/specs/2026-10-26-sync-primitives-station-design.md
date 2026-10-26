<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 第十七站设计:同步原语(eshine-rtos 长出信号量+互斥锁)

日期:2026-10-26 · 版本:27.04-dev · 前置:第十六站 70ede2a(门禁 55 步双绿)

状态:设计完成,待实施。

## 0. 决策记录(2026-10-26,用户令"继续下一站任务"= 按推荐序,设计按站规自决)

| # | 决策 | 选定 |
|---|---|---|
| 1 | 站点选型 | **同步原语站**(devlog 16 首选候选):信号量+互斥锁,RTOS 人格补最后一块——站 16 预埋位兑现(rtos.h `RT_RX_BIT` 注释"通用原语留十七站"、spec §6"十七站引入通用原语时再议") |
| 2 | 等待模型 | **每对象 FIFO 等待队列**(方案 A):唤醒队头,拒绝广播惊群;与 RX 的 wait_mask 广播域**分立**(互不干扰) |
| 3 | 唤醒语义 | **重检环 + 唤醒即抢占**:take 是重检环,give 唤醒队头且若更高优先立即 rt_sched(参考RTOS 形态);"唤醒=允许重试,非授予" |
| 4 | 互斥范围 | owner + 递归 nest;**无优先级继承**(PI 单独立站,记残余) |
| 5 | 门禁终点 | 55 存量全保,新增 3 判据 → **58 步**,连续 2 轮全绿 |

**方案对比**(设计按站规自决,记录备查):A=每对象 FIFO 队列(选定);
B=复用 wait_mask 广播(每对象一位;零新队列代码但惊群、32 对象上限、
互斥让渡语义被广播搅浑——人格倒退,弃);C=参考RTOS 全家桶含 PI/事件组/
队列/超时(范围爆炸,PI 涉 owner 优先级提升与嵌套还原,弃)。

**参考源纪律**(站 16 同款红线):参考RTOS **公开设计**为基准(信号量/
互斥的公开语义与形态:计数信号量、owner 互斥、FIFO 等待);参考实现
`rtos-kernel/` 一页不翻。clean-room 出身不破。

## 1. 范围与拓扑

**纯客侧站**:hypervisor 零改动(五面帧/泵/门禁存量机器不动),
全部新代码落在 `rtos/`:

```
rtos/rt_sync.c   新:信号量+互斥+等待队列原语
rtos/rt_demo.c   新:semdemo/mutexdemo 任务体(静态对象,演示即判据)
rtos/rtos.h      改:rt_task 增 wq_next;rt_sem_t/rt_mutex_t + API
rtos/rt_task.c   改:create 复位 wq_next(槽位复用卫生)
rtos/rt_shell.c  改:+semdemo/mutexdemo 命令 + usage 行
Makefile          改:RT_OBJS 增 rt_sync.o rt_demo.o
```

宿主侧改动仅**三处同步件**:verify_el2.py(55→58)、宿主壳 help 走查
(58 步)、ARCHITECTURE 验证节(58)——同 commit(纪律)。

## 2. 原语设计

### 2.1 每对象 FIFO 等待队列

```c
/* 挂等:state=BLOCK 入对象队列,rt_sched 让位;醒后续跑于 rt_pend 调用点 */
static void rt_pend(rt_task_t **head, rt_task_t **tail);
/* 摘头:出队清链;调用方 rt_ready_push + 抢占检查 */
static rt_task_t *rt_wake_head(rt_task_t **head, rt_task_t **tail);
```

队列形态镜像 rt_sched 的就绪尾链(单向、非环、摘除简单);
`rt_task` 增 `wq_next` 挂链字段(sp@0 asm 契约不动,帧尾扩先例)。

**与 RX 广播分立**:信号量/互斥挂等的任务 `wait_mask==0`,RX 广播
(`rt_task_wake_all`)按位匹配不会误醒;两域互不感知,壳的 RX 阻塞
路径零改动。

### 2.2 信号量(计数,参考RTOS 公开语义)

```c
typedef struct { long count; rt_task_t *wq_head, *wq_tail; } rt_sem_t;
void rt_sem_init(rt_sem_t *s, long init);
long rt_sem_take(rt_sem_t *s);   /* while(count<=0) rt_pend;  count-- */
long rt_sem_give(rt_sem_t *s);   /* count++; 唤醒队头;更高即抢占 */
```

**契约:唤醒=允许重试,非授予**。give 无条件 `count++` 再唤醒队头;
被醒者在重检环里自己取。barge(重检前被别人取走)只会重挂——无
丢醒、无死锁,FIFO 公平性在单核+唤醒即抢占下事实上成立。

### 2.3 互斥锁(owner + 递归,无 PI)

```c
typedef struct { rt_task_t *owner; u32 nest;
                 rt_task_t *wq_head, *wq_tail; } rt_mutex_t;
void rt_mutex_init(rt_mutex_t *m);
long rt_mutex_take(rt_mutex_t *m);  /* 空=占;自己是 owner=nest++;否则挂等 */
long rt_mutex_give(rt_mutex_t *m);   /* 非 owner -1;nest-- 归零=让渡+唤醒队头 */
```

与信号量同一队列与同一唤醒契约(owner 清空再唤醒,被醒者重检环里
重新占据)。递归按 owner 计 nest。**非拥有者 give 返回 -1**(判据用)。

### 2.4 唤醒即抢占

give/take 让渡路径在 `rt_ready_push(被醒者)` 之后:

```c
if (w->prio < rt_cur->prio)
    rt_sched();          /* 唤醒更高优先者:立即让位(参考RTOS 味) */
```

统一帧的 rt_sched 调用点即续跑点(站 16 机制),give 返回时让渡已
完成。被醒者优先级更低则不切(唤醒不让位,语义与"抢占只在切点"
同族)。

### 2.5 单核免锁论证

take/give 的检查-修改序列不可被 ISR 撕裂的前提:**ISR 不触同步对象**
——本站 give/take 全部任务上下文(tick ISR 只碰 rdy 队与 SLEEP 表,
UART ISR 只碰 RTX 环与 wait_mask 广播)。挂等中途 tick 抢切(出口
按 `state != RT_RUN` 摘人,已入 wq 故一致)与站 16 `rt_block_rx`
同款窗口,机器现成。

## 3. 演示即判据(命令面 +2,门禁 +3)

静态演示对象在 `rt_demo.c`(不建运行时分配器,池 8 够:常驻 2 +
演示峰值 2)。

### 3.1 `semdemo` —— 有界环生产者-消费者(信号量对)

```
empty(4) / full(0);producer prio 7,consumer prio 9(数字大=优先低)
producer: 8× { take(empty); 打 "semdemo: p<i>"; give(full) } -> "ring filled (8)"
consumer: 8× { take(full);  打 "semdemo: c<i>"; give(empty) } -> "ring done (8/8)"

(结束行避开计数针前缀——`semdemo: p`/`semdemo: c` 各恰好命中 8 条
per-item 行,不多不少。)
```

**判据 [26/58]**:read_until `semdemo: ring done (8/8)`;断言
`semdemo: p` 与 `semdemo: c` 各恰好 8 次(计数判据,容忍心跳夹行)。

### 3.2 `mutexdemo` —— 阻塞醒序 + 递归 + 非拥有者拒绝

```
contender prio 6(高): 打 "contender locking" -> take -> "contender locked"
  -> sleep 30 tick(300ms 持锁) -> "contender unlocking" -> give
  -> "contender released" -> exit
waiter prio 8(低): give(非 owner) -> "give denied (not owner)"
  -> "waiter wants lock" -> take(阻塞) -> "waiter locked (after block)"
  -> take -> "recursive take 2 ok" -> give,give -> "waiter done" -> exit
```

**判据 [27/58] ps mid-hold(活体状态证明)**:`contender locked` 后发
`ps`,断言表内 `waiter prio 8  BLOCK` 与 `contender prio 6  SLEEP`
两行在场——阻塞不是打出来的日志,是 ps 表里的活状态。

**判据 [28/58] 醒序与语义**:read_until `mutex: waiter done`;断言
顺序 `contender unlocking` < `waiter locked (after block)`(阻塞-唤醒
时序),且 `recursive take 2 ok` 与 `give denied (not owner)` 在场。

### 3.3 门禁 55 → 58(只增不减)

新 3 判据插在 [25](vmstat,vm0 独占段)之后、旧 [26](vmrun 1)之前,
成为新 [26][27][28];旧 [26..55] 顺移 [29..58]。存量 55 步一寸不动
(RTOS 独占段跑 demo:无 Linux 噪声,判据窗口最干净)。三处同步件
同 commit(verify 自校 1..58 降序重编号/help 58 步走查含新命令/
ARCHITECTURE 验证节 58)。

## 4. 工程与 DoD

- 构建:`RT_CFLAGS` 沿用(-Iguest,-mgeneral-regs-only 不破,demo 零 FP)。
- 站 DoD:spec → 实现(A 原语/B demo+壳/C 门禁+三处同步)→ 门禁 ×2
  双绿 → devlog 17 + 偏差 #21 + 内容包 5 文件 → commit → 镜像机(不碰
  GitHub)→ 记忆。

## 5. 风险与残余

| 风险/残余 | 处置 |
|---|---|
| 无优先级继承(低优先持锁者阻塞高优先) | 记残余,PI 单独立站(owner 优先级提升+嵌套还原) |
| 事件组/消息队列/挂等超时 | 不做(YAGNI);需要时按同队列形态扩 |
| give/take 无 ISR 上下文版 | 本站零 ISR give;将来 ISR 给量需短 DAIF 临界段(记残余) |
| per-task FP ctx | 再议结论:仍不做——同步原语零 FP,demo 任务 FP-free,安全前提不破 |
| spawn 单缓冲名互踩 | 站 16 残余照旧(demo 名走 rt_demo 静态串,不与 spawn 冲突) |
| 演示死锁即 vm0 永久挂(demo give 计数错) | 演示计数静态可验;若挂,门禁超时红,按尸体诊断 |
| 心跳行夹进判据窗口 | 判据按"行内包含/计数"锚,不要求相邻(站 16 惯例) |

## 5.5 实现期纠偏(2026-10-26,门禁 4 轮实证)

- **§2.5 "单核免锁"论证不完整,已修**:tick 抢切能把另一任务插在
  两条指令之间——check+dec 不封窗可被双取到负数;且 rdy 队自站 16
  起就有任务/ISR 双写者交错的丢任务潜伏窗口(rt_sched 重排、对象
  handoff 均任务侧写者)。实际实现为**短 DAIF 临界段**(save/restore
  而非 set/clr,ISR 语境不泄掩码):take 快路/give 记数/挂等
  state+入队全封窗,rdy_enqueue/rt_pick_ready 内嵌同款;
  rt_sched/WFI 永不进窗。
- **首跑崩溃真凶 = 站 16 潜伏 bug,非本站原语**:`rt_task_create`
  名字拷贝循环复用 `i`,`rt_frame_init` 拿到**名字长度当栈下标**——
  shell(5)住 stack[5]、heartbeat(9)住越界 stack[9](压着 g_vq 顶)、
  gate16/fptest(6)撞同一 stack[6] 但顺序复用,全靠名字长度不撞
  车活过两轮门禁;producer/consumer 双双 8 字,第一对同长并发任务
  共享 0xd430 帧区当场对撞(-d int 两发 eret 紧邻 + 帧基址与名字
  长度推算分毫不差定谳;objdump 全链审计原语/调度器编译产物皆清白)。
  修复=槽位号独立保存。**教训:名字拷贝循环复用下标变量与 asm 契约
  同级的静默杀手;两轮门禁绿≠无雷,新判据即尸检报告。**
- **[53] 闪断(4 轮 1 挂)归因**:Linux 内核时间冻在 0.489s(解包
  完、/init 前的安静相),VM0 心跳存活——站 16 记档的
  `vgic_wake_waiter` 无条件关线 × 在途 pending LR 活不过线落物理:
  宿主相 v27 若带未消费 pending LR,关线一落该拍蒸发,解包期 Linux
  无别源中断唤醒重编程,一拍冻即死;`vtimer_inflight` 守门只护
  抢占决策点不护宿主相 27 分支。站 16 的 gating 实验撞过此墙
  (re-assert 风暴),真正的修法是 inflight 时改走"立即回业主 VM
  消费自己的拍"——登记为站 18 首选候选(现有两次独立症状报告:
  站 16 [47] 期闪断 + 本站 [53])。

## 6. 偏离登记(预登记 #21,随实现 commit 落 baseline-deviations.md)

> #21 同步原语形态:参考实现 rtos-kernel(参考RTOS/Linux 派生,"连行为参考
> 也不进")-> 新仓第十七站 参考RTOS 公开设计为基准从零最小同步原语
> (计数信号量 + owner/递归互斥,每对象 FIFO 等待队列,唤醒即抢占;
> 无 PI/事件组/队列/超时,ISR-give 不在本站面)。纯客侧站,hypervisor
> 零改动;门禁 55->58(3 新判据:有界环/ps 活体状态/醒序+递归+拒绝)。
> 参考源:参考RTOS 公开设计(Apache-2.0 上游)+ 站 16 调度机器(自有)。
> 用户 2026-10-26 令"继续下一站任务",按推荐序、设计按站规自决。
