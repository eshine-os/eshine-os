# 站二十二设计:控制台原子化闭案([54] 定谳)/ 软定时器面

> 承接:站二十一(ead66b4,75 门双绿)。用户令"修复之前的问题;开始
> 下一站"。A 块=四站悬案 [54] 闪断家族根治(调试纪律走完:取证→
> 失败测试→单变量修复→反转验证);B 块=站二十一调研候选的头名
> (软定时器,超时机器的自然延伸)。

## 1. A 块:[54] 闪断家族定谳与闭案

### A1 病灶(R1b dump 取证,站二十三案同签名)

R1b TimeoutError 的 `output so far` 全量 dump 里 needle 不是缺席
而是**字节级撕裂**:`spinni[vm2] [   n 0.1339g17]`——vm1 的
"spinni|n|g"与 vm2 的 `[    0.133917]` 互相嵌进对方字符串中间
(vm2 时间戳里嵌着 vm1 的 n 和 g)。read_until 找连续 `spinning...`
→ 永不匹配 → 假阴性超时,客机全程健康。

### A2 根因(两层)

1. **`console_putc` 原语不持锁**:`console_puts` 持锁,putc 裸奔。
   vuart fg 逐字符透传路径(站六形态,18D/[41] 两轮加锁都漏了它)
   是唯一高频裸写者——cpu0 上 vm1 每字符一陷阱一裸写,cpu1/2 上
   vm2 的锁内整行 emit 拦不住锁外写,两流字节级互撕。
2. **fg 逐字符透传的字符缝隙**:即使每字符原子,字符间缝隙
   (MMIO 陷阱出口/泵边界)可被整行 vm2 输出嵌入——needle 仍碎,
   概率降不归零。

### A3 修复(失败测试先行:probe-tear 60 行风暴,修复前 4+ 撕)

1. **原语持锁**:console_putc 自身按 owner 递归持锁(全类一次治:
   宿主 shell 回显/vmrx dump/vuart fg 同免疫;复入只增嵌套)。
2. **fg 行缓冲化**:fg 字符入 tx 缓冲(与 bg 同律,`tx_tagged=false`
   对称),`'\n'` 经 vuart_tx_emit 锁内**整行原子**——判据 needle
   确定性(输出行实测 60/60 全整)。
3. **泊位冲刷(爆发感知)**:客 WFI 停泊且 RX 环空才冲半行——键
   回显即时性由此保(µs 级,人类敲键尺度无感);RX 环还有字节
   (chardev 分块缝,即刻会醒)跳过冲刷,免把回显拆碎片逐泊吐出。

### A4 残余(机制清晰,记档不修)

**chardev 分块边界处的回显碎裂**:QEMU chardev 把一行输入分多块
送宿主,块间隙客恰停泊且双环皆空,泊位冲刷吐出半行回显;若此刻
有互插者(vm2 打印),回显 needle 仍可能碎(风暴实测 23/60 回显
碎、**输出行 0/60 碎**)。当前门禁判据面**零暴露**(全部 echo
判据都不在互插窗内:[44] vm1 token 时 vm2 未靴;[63]/[68] vm2
token 时 vm0 门控静默+vm1 空闲)。根治需 ≥100Hz 宿主时钟的
**延迟冲刷**(泊位挂 10ms 死线、扫描器到期才冲)——现宿主 CNTHP
=1Hz 即泵量子,动它是调度语义手术,列远期候选"console 服务化"。

## 2. B 块:软定时器面(超时机器的定时器化)

### B1 形态(站二十一超时扫描的自然延伸)

```c
#define RT_TMAX 8u                 /* 静态池,与任务池同哲学 */
typedef struct rt_timer {
    u64 deadline;                  /* 绝对截止;0=停 */
    u64 period;                    /* 0=单发;>0=周期(零漂移重臂) */
    void (*fn)(void *);            /* 回调:核 0 tick-ISR 语境 */
    void *arg;
} rt_timer_t;
void rt_timer_init(rt_timer_t *t, void (*fn)(void *), void *arg);
int rt_timer_start(rt_timer_t *t, u64 ticks, int periodic);
                                   /* 0=成功;-1=池满/ticks==0(可观测)*/
void rt_timer_stop(rt_timer_t *t);
```

- 扫描=核 0 单写者环新增一支(`rt_timer_tick()` 挂在
  rt_tick_bookkeep 的 `c==0` 分支内、wake_sleepers 之后):
  按池槽序扫描,**到期先做重臂/注销决策、后进回调**(评审核
  blocker:此序照欠定 spec 字面实现会产每 tick 风暴假绿):
  周期者 `deadline += period` 重臂(vtimer 零漂移同律),单发者
  `deadline=0`+注销——回调执行时 deadline 已清 0。
- **回调语境=核 0 tick ISR**(I=1),参考RTOS 反转警示:ktimer 回调
  跑在专用 timer 任务语境、可阻塞,本设计语境反转,直译即灾难。
  允许:rt_puts/rt_puts_u64(持锁即屏本核,ISR 不可能再入持锁
  任务)、rt_ready_push(限 SLEEP 态唤醒,抢占决策由 IRQ 出口
  延后)、rt_timer_stop(含自停,幂等:置 0+注销)。禁止:一切
  挂起 rt_cur 的调用(sleep/take 族——ISR 帧内 rt_sched 换帧=
  gic_eoir 永不执行=核 0 中断面死);rt_sem_give/rt_event_set
  (尾段 rt_sched 同病);rt_timer_start(同环重入)。
- 静态池登记:start 时入池(扫空槽,同指针重复 start=改期不双
  登),stop 幂等;**rt_timer_t 须常驻存储**(静态/全局)且
  **活过回调本体**——武装期退场=tp[] 悬垂;**start/stop 仅核 0
  语境调用**(与单写者扫描同核序贯;跨核支持需池自旋锁,记残余);
  start 返 0 成功/-1 池满或 ticks==0(可观测拒绝;0 拒因
  deadline=rt_ticks+0 下一拍即发语义含混)。

### B2 demo:`swtdemo`(判据即演示,三面)

| t | 事件 | needle |
|---|---|---|
| 0 | 单发 30 | (静默) |
| 0 | 周期 15 ×3 后自停 | (静默) |
| 0 | 单发 50 于 t=20 stop | (静默) |
| 15/30/45 | 周期拍 | `swt: periodic 1` `swt: periodic 2` `swt: periodic 3` `swt: periodic stopped` |
| 30 | 单发到点 | `swt: oneshot fired` |
| 20 | stop 单发 50 | `swt: cancelled` |
| t=80 | demo 任务收尾(20+60)| `swt: done`(此后无 `swt: late fired`,判据断**缺席**) |

- 池占 3 定时器;demo 任务 t=20 取消后睡 60 收尾(done=t=80,晚于被取消单发的 50——缺席窗覆盖)。

## 3. 判据与门禁 75 → 76(只增不减)

| 新步 | 判据 |
|---|---|
| +[42] | swtdemo:periodic 1/2/3 恰三拍(计数断)+ stopped + oneshot fired **恰一次**(计数断)+ cancelled;`swt: late fired` **缺席**(done 后 1.5s 尾读扩窗);`swt: done` 收尾;序断 periodic 1 < cancelled < oneshot < periodic 3 < stopped |

- 插入位(评审核折入,结论先行):**新门=[42] swtdemo**(插在
  [41] stkdemo 后、vm0 壳活跃期),旧 [42]-[75] 顺移 +1 为
  [43]-[76],总门 75→76。曾考虑追加终局,因 vm0 已被 [74] 收割
  而否决。计数/负断言硬化(评审核):重臂/注销机制的回归(单发
  每 tick 复发、周期错拍/过发)在纯存在性判据下假绿,计数==N 与
  缺席扩窗封死该盲区(仿真坐实)。
- 三处同步(verify 76 表/help 76 行/ARCHITECTURE)同 commit,
  `git show --stat` 验三件在;降序+assert 手术(站二十已验证纪律)。

## 4. 风险与诚实边界

- **回调语境纪律**:tick-ISR 打印与站二十 panic 裸打不同——这里
  rt_puts 持锁合法(回调非持锁打断语境);但回调时长受 tick 预算
  约束(demo 尺度一行打印,µs 级)。
- **start 于回调内禁用**:入池扫描与回调迭代同环,重入=次序未
  定义;记残余,API 注释钉死。
- **[54] 残余**(A4)与站二十一两残窗不变,同册续档。
- **ISR 帧深预算**(评审核 minor):回调链拉长 tick-ISR 的 C 帧深
  (~272B 异常帧+回调本体),站 21 的 264B 垫区校准口径随之变宽
  ——任务水位低于 ~400B 时 ISR+回调可越垫穿行底;受控深潜已由
  DAIF 窗保护,常规任务栈水位富余,记档待水位守卫站收编。
- 门禁:v3 双绿([54] 修复后)→ B 合入后再连续双绿方真绿;
  probe-tear 风暴(输出行 0/60 碎)与无 vm2 对照(120/120)归档
  为 A 块判据证据。
