# 站二十三设计:三块合成站(A irq 注册表 / B 水位守卫 / C console 延迟冲刷)

> 承接:站二十二(6009bf5,76 门双绿,[54] 闭案)。用户令"继续
> 下一站的三个任务"=站二十二终报三候选全上。三块同属"把硬编码
> 面与残余面收编为结构"的质检站:门禁 76→**79**。

## 0. 三块的共同背景(全部有档)

- 调研 residual:"中断管理形态——单入口 rt_irq_dispatch 三 INTID
  硬编码(27/33/0/35),无注册表;三设备时代站得住,设备面一扩
  即僵"(irqdemo 排站建议);
- 站二十二 residual:"ISR 帧深预算拉宽——回调时代任务水位低于
  ~400B 时 ISR+回调可越垫穿行底"(水位守卫收编);
- 站二十二 residual:"chardev 分块边界的回显碎裂——根治需延迟
  冲刷;宿主 CNTHP=1Hz 即泵量子"(**本站用 CNTPCT 自由读时钟
  绕开宿主时钟手术**,C 块可行性由站内证明)。

## 1. A 块:irq handler 注册表

### A1 形态

```c
#define RT_IRQ_MAX 8
void (*rt_irq_register(u32 intid, void (*isr)(void), u32 wake_mask))(void);
void (*rt_irq_swap_uart(void (*isr)(void)))(void);   /* 33 号槽换钩助手 */
```

- `rt_irq_tab[]`:intid→(isr,wake_mask) 表,rt_irq_register **替换式
  登记**(一 intid 一槽,重复登记=替换,**返回旧 isr** 供链式包装);
  启动期 guest_main 按 27/0/33/35 四路登记(现硬编码逻辑原样
  移槽:vtimer→ticks++/bookkeep,IPI→resched,uart→drain,
  virtio→blk isr)。rt_irq_swap_uart:INTID 常量留 gic.h 合法 TU
  (单含静态头拖进 demo TU 会产未用静态警),demo 经它换/恢复
  33 号槽。
- rt_irq_dispatch:IAR→查表→**isr 先行,mask 后醒**
  (rt_task_wake_all(mask));无登记 intid → "irq: unclaimed N"
  一行报(默认拒绝面诚实化)。RT_RX_BIT 等待位由登记方携带
  (uart→RT_RX_BIT,其余 0)——"按掩码唤醒"即泛化完成。

### A2 demo:`irqdemo`(注册表活证)

- 登记期:guest_main 四路登记后,`irqdemo` **先建任务后换钩**
  (池满路径不把 33 号槽永久留 wrap——登记面不被 demo 污染),
  经 `rt_irq_swap_uart(wrap_uart)` 把 33 号槽**链式包装**(返回
  旧 drain 存 prev);wrap 计数后调 prev(RX 面不断)。
- **计数供给=探针 x,非命令字节**(探针二轮定谳):命令字节在
  换钩发生前已被壳消费殆尽——首版"命令行自身的 UART IRQ 即流入
  wrap"机理为假;且同拍爆发的 x 整批被换钩前 ISR 消费。门判
  `irqdemo\n` 后 **0.1s 延迟补发探针 `x\n`**(壳已处理完命令、
  仍在 demo 窗内)→ 探针 IRQ 经 wrap 入账。demo 任务**自同步**
  等探针(5 tick 一探,预算 2s;超时保底诚实报 0)后打印
  `irq: chained N`(N>=1 断),随后**恢复原登记**
  (rt_irq_swap_uart(prev)),打 `irqdemo: done`。
- 判据:`irq: chained ` 前缀 + N>=1 + `irqdemo: done` + 恢复后
  壳继续可用(`ps` 出 task table 即证)。

## 2. B 块:水位守卫(live-sp 判)

### B1 形态(区别于站二十一的水位=历史最深)

- 站二十一的水位(margin)是**历史高水位**;守卫要防的是**此刻
  在跑的 sp**——tick+回调 ISR 链(~272B+回调)落点低于可用底
  时穿行底。核 0 tick 单写者环新增一支:**bookkeep 入口读当前
  sp(asm)对 `rt_cur_stack_limit()+RT_GUARD_LINE`**——低于即
  一次性告警 `stk: lowmargin <name> <B>`(ISR 安全契约同站
  二十二 B;**告警不 panic**——深潜是合法瞬态,magic 哨兵已防
  真越界;任务尾字段加 `low_warned`,create 清零,一次性;
  lim==NULL 的 idle 语境免判)。
- **读点坐标系(探针修真)**:守卫读的 sp 是 **ISR 链读点下方**
  的值——tick 守卫自身跑在被打断任务的栈上,272B 异常帧+
  dispatch/bookkeep 链帧已压在任务真 sp 之下,实测读点差
  D≈350(lowdemo 打印 160=真 sp≈510)。故:
  - `RT_ISR_BUDGET=384`:**任务真 sp 系**危险线(ISR 链落点
    低于可用底即写 magic 区,下一拍哨兵必 panic);
  - `RT_GUARD_LINE=640`:**守卫读点系**告警线=危险线+读点差
    (384+D≈350≈734 取整 640)再留打印链/帧漂移余量——两线
    分属两坐标系,不可混比(评审余量算术定谳)。
- **告警行单窗原子**:整行先组后一发 rt_puts(前缀+名+数字+
  换行组入 line[96])——三段各自 rt_puts 是三次锁窗,同 VM 跨
  核打印可劈 needle(本仓纪律:rt_puts_u64 注释/smpdemo 定谳),
  且压 ~90B 低栈链深。
- 深潜保护面不冲突:deepdiver 的递归段有 DAIF 窗(站二十一评
  审核),tick 永不在其深 sp 处出现——守卫看不到它,零假阳性。

### B2 demo:`lowdemo`(受控贴线)

- `lowrunner` 任务:asm-SP 判深递归,停点 `limit+RT_GUARD_LINE+
  256`=lim+896(告警线外侧一整步):帧步 96 → 最深真 sp ∈
  [800,896],读点(真 sp−D)∈ [450,546] < 640 **恒触发**;
  warn 打印链 ~190B 自读点向下,底 ≥ 260 不碰 magic。
  (初版 lim+600 读点 ∈ [154,250],warn 打印链贴刀锋可写穿
  magic——评审余量算术抓出的坐标系混用,修真后重锚。)
  **不封窗**,在深度处 volatile 忙旋 30 tick(tick 必在深 sp
  处命中守卫)→ 解栈回浅 → 打 `lowdemo: done`。
- 判据:窗内出现 `stk: lowmargin lowrunner`(一次性,恰一行)
  + `lowdemo: done` + 随后 stackcheck `magic ok`(未被误伤)。

## 3. C 块:console 延迟冲刷(chardev 分块残余收编)

### C1 机制(绕开宿主时钟手术的关键)

- 病灶回顾:chardev 把一行输入分多块送宿主,块间隙客恰停泊
  (双环皆空)→ 泊位冲刷吐半行回显 → 互插者落缝=回显 needle 碎。
- **延迟化**:泊位不再即时冲,改**武装** `vu->flush_at =
  CNTPCT + 20ms`(freq/50,CNTPCT/CNTFRQ EL2 直读无陷=零手术);
  冲刷由**泵入口扫描**执行(sched_pump 入口,检查全部 RUNNING
  VM 的 flush_at)。
- **时间基面(评审纠偏)**:扫描的真正驱动=CPU0 宿主壳忙自旋
  (shell_run 每迭代一圈 sched_pump,µs 级,boot 序即恒在),
  **不是** vm0 100Hz tick——vm0 未建/已杀期扫描照常;pump_cpu
  (CPU1/2 专泵)无扫描。长量子期间扫描停摆 ≤1 量子,fire 复验
  (下条)下迟冲无害。
- **fire 时刻复验(评审 major 折入)**:到期不清即冲——先在
  console_lock 内复验武装时刻的同一静默判(全帧泊位+RX 环空):
  陈旧武装(泊后自醒长跑、死线已过)在行在途时拦腰冲出半行
  =[54] 双雷复发(新路径共享旧状态);不满足**保留武装**留待
  下一泵入口复扫,行 '\n' 收口后自然变 no-op。
- **锁仲裁(评审 minor 折入)**:泊位武装(exceptions.c)与 fire
  判-清(vm.c)同入 console_lock——无锁清零可吞并发武装
  (读旧死线→他核泊位武装→清零),该行吸收窗失效。多核同到双
  冲无害(emit 空缓冲早退)。
- 20ms 定标:块间隙实测 µs-ms 级(发生即被延迟吸收,行完整后
  才见天日);人类敲键间隙 100ms+ → 冲刷照发,回显延迟 ≤20ms
  无感。TCG 偶发 >20ms 停顿窗=残余(见 §5)。
- 武装条件沿用站二十二双雷修复:全 VM 静默(任一同侪 vCPU 未
  泊位=行或在途,不武装)+ RX 环空(环有字节=块在途,连武装
  都不必——醒来续行,'\n' 收口原子)。
- 切换/死亡/溢出路径的即时冲刷语义不变(不是泊位路径);
  **即冲口(vmconsole 双向切换/escape/vm_finish/owner 落位)
  同步熄武装 flush_at=0**——陈旧死线不得对新起的半行拦腰开冲
  (评审 c 场景)。

### C2 判据:echo 风暴门(真并行窗内,vm1 前台)

- **门位(评审迁窗定谳)**:vm2 靴流正冲的**真并行窗**内
  (vm1 复活 spin 判据后、vm2 前台化前),vm1 前台壳连发 10 条
  `echo stormNNN`(0.3s 间隔,唯一 token)——判 **10/10 needle
  连续**(碎片期实测 23/60 撕;延迟化后探针 0 撕)。
  初版门位插早期 spin 判据后:彼处 owner=host 且 vm2 未建,
  needle 由宿主壳自身 echo 产出,门恒绿空转(评审 blocker
  抓出)——迁窗后 needle 才真经 vuart 泊位冲刷面。
- 判据窗诚实性:>20ms TCG 停顿造成的残余碎裂以观察哨形式记档
  (复现即取证,不刷绿)。

## 4. 判据与门禁 76 → 79(只增不减)

| 新步 | 判据 |
|---|---|
| +[43] | irqdemo:chained N>=1 + done + 恢复后壳活 |
| +[44] | lowdemo:恰一行 lowmargin lowrunner + done + magic ok 未误伤 |
| +[65] | echo 风暴 10/10(真并行窗内 vm1 前台,vm2 靴流正冲) |

- 号段(落地实况):irqdemo/lowdemo 插 [42] swtdemo 后=
  [43][44],旧 [43]-[76] 顺移 +2;风暴门初插 [51](旧 [51]-
  [78] 再顺移 +1);评审迁窗后 [51] 退役(旧 [52]-[65] 回移
  -1=[51]-[64]),风暴门落 **[65]**(真并行窗 [64] 判据后),
  [66]-[79] 不动。三处同步(verify 79 表/help/ARCHITECTURE)
  同 commit,`git show --stat` 验三件在。

## 5. 风险与诚实边界

- **C 的 >20ms 停顿残余**:TCG 重载窗理论上可造 >20ms 块隙,
  延迟吸收失效即碎——判据窗内 10/10 若现 9/10 即观察哨取证,
  不刷绿不硬调(调大延迟=敲键迟滞)。
- **B 的守卫时机**:守卫挂核 0 单写者扫描段(c==0)是**选择
  而非结构必然**——核 1 每拍也跑 bookkeep 的轮转/抢占段,只是
  不含守卫;核 1 覆盖补丁点=bookkeep 入口(per-core-local 读写
  免锁),留待项,记残余;受控 demo 全核 0。
- **A 的替换式登记**:登记窗口期(替换瞬间)到达的 IRQ 走旧
  槽或新槽皆可(同核序贯,免锁);但 (isr,mask) **双字替换非
  原子**——两条 store 之间到达的 IRQ 可见"新 isr+旧 mask"混合
  槽,通用 API 契约=先写 mask 后发布 isr;当前唯一投放面(33
  号槽掩码恒 RT_RX_BIT)使其无害。irqdemo 恢复窗口期同理(同
  核序贯,语义无害)。
- 门禁:三块合入后连续两轮全绿方真绿;每块独立探针先行。
