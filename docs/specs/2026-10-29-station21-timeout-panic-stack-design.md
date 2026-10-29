# 站二十一设计:三块合成站(A 同步超时面 / B panic 观测面 / C 栈防护)【v2+3 评审修订】

> 承接:站二十(a8fd03b,72 门双绿)。主体取自 9-agent 参考实现对标调研
> 三个最高性价比缺口(用户令"继续推进下一站全部任务",三块全做)。
> **v2:对抗评审工作流(4 镜头 20 agents)16 条 CONFIRMED(2 blocker
> +14 major)全数折入**:PI 回收、字段生命周期、两窗原子性、参考RTOS
> 语义对齐、magic 首拍假 panic、垫区穿透、[74] 位次矛盾、判据可判性
> 逐条修订——逐项见各节"评审核"注。

## 0. 现状与缺口(调研口径,核到代码)

- `rt_pend` 无超时参数;`wake_tick` 字段只服务 `rt_sleep_ticks`。
- `rt_sync_fault` 未知 EC 静默 `for(;;) wfi` 停车,无 ESR/ELR/任务名。
- 4KB 静态栈无护栏,溢出永不现形。

## 1. A 块:同步超时面

### A1 语义:参考RTOS 公开设计对齐(评审核:0=forever 反转修正)

```c
#define RT_WAIT_FOREVER (~0UL)     /* 参考RTOS_WAIT_FOREVER 同义 */
#define RT_TIMEOUT      (-2L)
/* ticks==0        :no-wait——单次复核,不满足即返 RT_TIMEOUT(不挂) */
/* RT_WAIT_FOREVER :永不超时(旧名薄壳恒传此值,现语义原样) */
/* 其他            :绝对截止一次算死,重挂恒用"首绝对 deadline-rt_ticks" */
long rt_sem_take_to(rt_sem_t *s, u64 ticks);
long rt_mutex_take_to(rt_mutex_t *m, u64 ticks);
int  rt_queue_send_to(rt_queue_t *q, u64 msg, u64 ticks);
int  rt_queue_recv_to(rt_queue_t *q, u64 *msg, u64 ticks);
/* 旧名保留=RT_WAIT_FOREVER 薄壳,零签名破坏 */
```

- **评审核(deadline 锚点漂移)**:deadline 由 API 入口一次算死
  (`rt_ticks + ticks`),条件复核失败被唤醒后重挂不再重算——超时
  从首次挂起计,锚点即调用点,抢占不漂移。

### A2 机制:单窗原子挂等(评审核:两窗 gap=丢醒窗)

- **复核失败→登记→挂链→摘 rdy 同一 DAIF 窗完成**(评审核 CONFIRMED:
  现形 rt_sem_take 的检查窗与 rt_pend 窗之间 gap 可被 tick 切走——
  窗内 give=丢醒→错报 RT_TIMEOUT。新形态:`rt_daif_save` 内一气完成
  条件判、BLOCK 发布、`pend_*` 登记、wq 挂链、rdy 摘除;`rt_sched`
  保持窗外,与现 rt_pend 同约束)。
- 挂等登记五字段(任务尾追加,帧契约 q@96/fpcr@608/fpsr@616 断言不动):
  `pend_deadline`(0=未武装哨兵,扫描门)、`pend_qh/pend_qt`(超时
  自摘回指针)、`pend_owner`(互斥挂等时=m->owner,sem/queue=NULL)、
  `timed_out`。
- **字段生命周期钉死(评审核 blocker)**:①扫描门
  `state==RT_BLOCK && pend_deadline!=0`——wait_mask 型(壳 RX)与
  事件注册表型(rt_ipc.c 不挂 wq)一律免疫,零假醒;②超时返回处清
  deadline;③`rt_task_create` 复用槽必清清单加全部五字段(现只清
  wait_mask/wq_next/wake_tick——残留 stale deadline=复用槽首个挂等
  假超时);④到期判沿用有符号惯用法
  `(long long)(rt_ticks - pend_deadline) >= 0`(u64 直减下溢=永不
  超时,评审核 minor)。
- **超时摘除与 PI 回收(评审核 CONFIRMED,本 spec 最大翻案)**:
  v1"零改动"断言不成立——boost 只写 owner->prio,base_prio 全树
  唯一回收点是 give;owner 永不放锁(超时面要防的病)即 boost 永久
  滞留,prio 10..19 确定性饿死。修订:扫描摘除后**重算 owner**:
  np=owner->base_prio,扫剩余 wq 取最强,np 变化且 owner READY 则
  rdy_remove/rt_ready_push(与 give 侧同不变量:等待集任一变化必须
  重算;站 20 自旋锁/IPI 机器现成)。跨核 owner 落入 §5 已声明残窗。
- give 侧懒跳过(弹头非 BLOCK 即弃弹下一个)保留为纵深防御。

### A3 demo:`tmodemo`(全确定性编排,PI 分支有覆盖——评审核)

三任务 a(8 驱动)/b(9 取者)/c(14 低优先 owner),池内静态
sem/mutex/cap-4 队列;时间线全 tick 锚定:

| t | 事件 | needle |
|---|---|---|
| 0 | a:no-wait 试空 sem | `tmo: sem nowait ret=-2` |
| 0 | a:填 4 条后 send_to(q,42,30) 满挂 | (静默) |
| 0 | b:sem_take_to(s,30) 空挂 | (静默) |
| 0+ | c:持 m,起 10×(sleep 10;打印自报 prio) | `tmo: c prio 14` |
| 30 | a 的 tx 超时 | `tmo: q send timeout ret=-2` |
| 30 | b 的 sem 超时 | `tmo: sem timeout ret=-2` |
| 30+ | b:sem_take_to(s,200) 再挂;a:sleep(20)→give | |
| 50 | b 醒取得(在时唤醒路径) | `tmo: sem got ret=0` |
| 50 | b:mutex_take_to(m,40)——c(14) 持锁,boost 14→9 | `tmo: c prio 9`(≥2 行) |
| 90 | b 超时,扫描摘除+**重算 c→14** | `tmo: mutex timeout ret=-2` |
| 100 | c 自报恢复 | `tmo: c prio 14`(超时行之后,判据断序) |
| 90+ | b:4×recv_to(q,·,0) 排空(no-wait 收) | `tmo: q drain 4 ret=0` |
| 110 | b:recv_to(q,·,20) 空挂超时 | `tmo: q recv timeout ret=-2` |
| — | 收尾 | `tmo: b done` `tmo: a done` `tmo: c done` |

- 判据:上列 needle 全在 + `prio 14`(超时行后)在 `prio 9` 之后;
  返码一律**字面串**印出(`rt_puts_u64` 是 u64,-2 会打成巨数,
  评审核);demo 内 `ret==RT_TIMEOUT` 为真才印,判据行即断言。
- **记残余**:give 醒后条件被夺的重挂分支无确定性编排(需同拍
  窃取),代码评审覆盖;事件组超时(registry 机器)不在本站。

## 2. B 块:panic 观测面

### B1 `rt_panic`(rt_task.c,近 pool)

- **入口先 `msr daifset,#0xf`**(评审核 minor:sync-fault 路径 DAIF
  未掩,打印中可再入 tick 二次 panic);**单次打印保证**:
  `panic_done` 静态旗,二次入即直入 wfi 不再打印(评审核:扫描路径
  每 tick 重入会无限复读 banner)。
- **裸 `puts_uart`,不走 rt_puts 锁**(持锁任务被打断的 tick 语境取锁
  =同核自死锁);`rt_puts_hex` 同律裸打(评审核:若按 rt_puts_ 家族
  带锁实现,[74] 存在时序闪断)。
- **两副面孔(评审核:tick 路径 ESR/ELR 架构上是残值,统一打印误导
  排障)**:
  - 陷阱路径(`rt_sync_fault` 未知 EC,替换静默 wfi):打印
    `esr=0x… elr=0x… far=0x…`(此刻 ESR/ELR/FAR 架构有效);
  - 扫描路径(栈哨兵):只打印指名与 cur,**不印寄存器行**(印
    `context: tick scan`),避免残值冒充现场。
- 格式:
  ```
  rtos panic: <why>
  task: <pool[slot].name> (cur <rt_cur->name>, cpu N)   [或 cur-only]
  esr=0x… elr=0x… far=0x…                              [仅陷阱路径]
  ```

### B2 demo:`overflow`(破坏性,与 C 咬合,[74] 终局步)

- overflower:按 `__builtin_frame_address` 递归下探(**帧地址判深,
  不硬编码深度**;递归体含活过递归点的局部量反 TCO——评审核),
  越过自家 magic 后受控 overshoot 至行内垫区深处即**解栈回浅**,
  随后浅层 `for(;;) wfi` 自停。
  **评审核 CONFIRMED(垫区穿透)关键修正:深 sp 处绝不留栈**——
  异常帧(272B)与 panic 调用链的 C 帧若落在深 sp 之下必穿透行底
  腐写邻行栈顶;解栈回浅后,tick 的帧/panic 链全部落在正常深度,
  垫区只需吸收递归自身的 overshoot(~200B)。
- 停等 tick(≤10ms)以 rt_cur=overflower 语境命中扫描 → panic 指名。
- [74] 判据需 vm0 前此已死(其 exit 判据在 [73])——复活链:
  `vmcreate 0 → vmrun 0 → vmconsole 0`(DEAD 槽直接重建,
  **不可先 vmkill 0**——kill 拒绝路径会 deny,评审核)→ overflow
  → banner 两行 → `vmkill 0`(此刻 RUNNING,合法)→ 宿主完整。

## 3. C 块:栈防护(magic 哨兵 + 水位)

### C1 栈行改造(评审核:布局钉死到字节)

```c
static u8 task_stacks[RT_POOL_MAX][4368] __attribute__((aligned(16)));
/* 行距 4368=16×273(行内 16 对齐保持)。升址布局:
 *   [0..263]    = 264B 牺牲垫(overflower overshoot 预算,~200B+余量)
 *   [264..271]  = magic(u64 一词,RT_STACK_MAGIC)
 *   [272..4367] = 可用栈 4096B(语义不变:可用区恰 4096,frame 顶=行尾) */
```

- **magic 只在 create 时写;扫描只查 `entry != NULL` 的槽**(评审核
  blocker:未创建槽 magic 是 bss 垃圾,首 tick 必假 panic)。create
  在 DAIF 窗内先写 magic+0xA5 填充后发布 entry,同核扫描(ISR I=1)
  序贯可见,无半开槽。
- **水位公式钉死**(评审核):扫描自**可用区底(row+272)升址**找
  首个非 0xA5A5…u64 词,`margin = 该地址 − (row+272)`;全净则 4096。
  垫区/magic 不参与(不是 0xA5)。create 时 memset 可用区 0xA5。
- 每 tick 扫描(核 0 单写者环):已创建槽 magic 一 u64 载入,坏即
  rt_panic 指名(§B1 扫描面孔)。~8 载入/tick。
- `ps` 池行尾追加 ` stk <margin>`;**idle 虚拟行印 ` stk -`**(池外
  无水位对象,评审核 OOB)。
- `stackcheck`:逐已创建槽一行 `stk: <name> margin <B>`,全哨兵完好
  收尾 `stk: magic ok`(**不印槽计数**——[41] 时点槽 5-7 从未占用,
  "8/8"不可判,评审核);坏槽指名且不收尾。
- 导出 `rt_cur_stack_limit()`(可用底,deepdiver 止点)与
  `rt_cur_stack_floor()`(垫内深限,overflower 止点)——pool 与
  task_stacks 皆 rt_task.c 私有 static,demo 拿不到地址(评审核)。

### C2 demo:`stkdemo`(非破坏)

deepdiver:递归至 `__builtin_frame_address` 触 `rt_cur_stack_limit()`
上方 ~256B 即解栈返回(不越哨兵);随后 `stackcheck` + `ps`。
判据:`stk: deepdiver margin <B>` 行在且值 < shell 行的值、
`stk: magic ok` 在、`ps` 行含 ` stk ` 列。

## 4. 判据与门禁 72 → 75(只增不减)

| 新步 | 判据 |
|---|---|
| +[40] | tmodemo:A3 表全部 needle + prio 14(超时后)序断言 |
| +[41] | stkdemo:magic ok + deepdiver margin < shell margin + ps stk 列 |
| +[74] | overflow → `rtos panic: stack overflow` + `task: overflower` → vmkill 0 → 宿主完整(插在旧终局步前,vm2-Linux 仍在场——评审核位次矛盾修正) |

- 号段:[1-39] 不动;[40][41] 新插;旧 [40-71] 顺移 +2=[42-73];
  [74] 新;旧 [72] 终局=[75]。**总数 75**。
- 三处同步:verify 75 表 / shell help 走查表(含 4 新命令:
  `tmodemo stkdemo stackcheck overflow`)/ ARCHITECTURE 同 commit,
  `git show --stat` 验三件在。重编号走降序 + assert 每号恰一次
  (站 20 已验证的手术纪律)。

## 5. 风险与诚实边界

- **跨核残窗(不变)**:对象等待队列 DAIF 窗只屏本核;跨核 give/owner
  与核 0 扫描摘除+PI 重算并发未防护。demo 全核 0 亲和,**记残余**。
- **panic 交错**:裸打无锁,理论上与他核输出交错;判据窗内无并发
  打印者,接受并记档。
- **帧尺寸漂移**:递归止点全用 `__builtin_frame_address` 实测地址;
  -O 级变化只移动 overshoot 量,垫区 264B 吸收。
- **[74] 复活语境**:vm0 重建时 vm2-Linux 仍在场(多 VM 并存真实
  运维路径自证);vm0 新靴判据 needle 与 [12-18] 同款锚。
- 预算:A 半站、B+C 半站;分段可验,每段独立 commit+门禁。

## 6. 实现评审修订(v3:实现后 4 镜头 × 复核链,9 CONFIRMED 全收)

对工作树 diff 的第二轮对抗评审(状态机/栈契约/判据 flake/同步一致性
×逐条证伪)确认 9 major,全数收治:

1. **成功出口缴械**:在时醒成功路径五字段保持武装,任务随后经
   wait_mask 型(壳 RX)/事件注册表型阻塞时,扫描门 BLOCK+stale
   deadline 会误发幻影超时醒(事件等待者伪醒后 READY 仍登记,
   rt_event_set 无条件再推=rdy 双挂腐化)——"一律免疫"承诺破约。
   修:`rt_pend_disarm` 四个 _to 环 got 出口统一缴械(超时出口本就
   清;重挂全量重登记,零语义代价)。
2. **获取即继承**:give 只弹一个等待者,剩余等待者 pend_owner 指
   已移交的旧主(超时重算误写无关任务),且新主从不继承剩余等待者
   优先级(PI 在交接处断链)。修:获取分支顺扫等待队,pend_owner
   重指新主+按剩余等待者自提(勿照抄 give 先弹后算形)。
3. **递归驱动段封 DAIF 窗**:deepdiver 末两级帧距哨兵不足 272B
   (tick ISR 全帧)+扫描 C 链,tick 打断于该深度会把自家异常帧砸
   在哨兵上,同一拍扫描即假 panic——[41] 成为 ~1e-4 级概率闪断
   (实测帧 96B/级)。修:递归段封 µs 级短临界段(站十七同律),
   deepdiver/overflower 同;overflower 解栈回浅后的 wfi 仍在窗外
   (捕获相位设计不变)。
4. **overflower 深限抬位**:floor row+64 覆盖不了一整级递归帧
   (96B),终帧可越行底写邻槽栈顶。修:floor=row+160(96B 帧+余量),
   overshoot 止于 [160,264) 垫内。
5. **handoff/give 弹+推原子**:弹头后、ready 前的无屏窗里 deadline
   恰到期,tick 扫描按 BLOCK+武装把已弹出者再推(双挂)。修:
   ready 入队并入弹的 DAIF 窗(rt_sched 仍在窗外,调度器契约)。
6. 深度判据文本更正:`__builtin_frame_address` 在 -O2 无帧指针下
   不可用(实测零下潜),落点=asm 直读 SP;A3 needle 与实现对齐
   ("tmo: q send ret=-2",spec 原文多一 "timeout" 字样);[40] 计数
   文案 timeouts x3→x4。
7. 同步面收尾:Makefile "72 步" 残留→75;shell help 10-29 段九行
   站十九遗留错位(全偏 -1,含 phantom 跳 29)对齐 verify 实际步号;
   __pycache__ 入 .gitignore。
8. **残窗登记**(评审定谳、不修):owner 退出时持有锁(泄漏锁=
   程序已错)槽位复用后 stale pend_owner 重算可误写无辜新任务
   ——无锁登记表不可得,demo 尺度接受并注释在案;give 侧先弹后
   算形态(让渡者携带被弹者 boost)为站十八存量语义,pidemo/
   mutexdemo 判据所锚,不动、另册。
9. 门禁影响:[41] 闪断面消除后按"连续两轮全绿"重验;R1 在途提示
   符锚(修为表头+列两段锚)、R1b [61] vm1 spin 超时=[54] 观察
   哨家族第 2 案(记档不刷绿)如实入 devlog。
