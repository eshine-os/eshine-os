# 站二十一 devlog:三块合成站(A 同步超时面 / B panic 观测面 / C 栈防护)

> 承接站二十(SMP RTOS 72 门双绿)。本站主体=9-agent 参考实现对标调研
> 的三个最高性价比缺口(用户令"继续推进下一站全部任务",三块全做):
> rt_pend 无超时、未知 EC 静默停车、4KB 栈无护栏。spec v2 折入对抗
> 评审工作流(4 镜头 20 agents)的 **16 条 CONFIRMED(2 blocker+
> 14 major)** 后实施。

## 评审翻案三案(先记最有价值的)

1. **PI boost 滞留(v1"零改动"断言被证伪)**:boost 只写
   owner->prio,base_prio 全树唯一回收点是 give;owner 永不放锁
   (超时面要防的病)即 boost 无界滞留,prio 10-19 确定性饿死。
   参考RTOS 公开设计同不变量:等待集任一变化必须重算 owner——参考实现
   k_rtmutex 在 pend 超时摘除时正是这么做的。修订:扫描摘除后
   重算(回 base 再按剩余等待者取最强,READY 摘挂,与 give 同律),
   tmodemo 补低优先锁主全弧——`tmo: c prio` 14→9(被 boost)→14
   (超时重算)三段序断进判据。
2. **两窗 gap=丢醒窗**:现形 take 环的条件检查窗与 rt_pend 挂等窗
   之间可被 tick 切走——窗内 give=资源在而取者已挂,站 21 前永久
   吊死,站 21 后错报超时。修订:复核失败→五字段登记→挂链→摘 rdy
   **同一 DAIF 窗**一气呵成;deadline 由 API 入口一次算死(锚点=
   调用点,重挂恒用首绝对值)。
3. **magic 首拍假 panic(blocker)**:v1 tick 扫描查全部 8 行,而
   magic 只在 create 时写——未创建槽是 bss 垃圾,首 tick 必炸。
   修订:扫描只查 `entry != NULL` 的槽,create 序贯=同核扫描
   (ISR I=1)见 entry 即见完好哨兵。

## A 块:超时族(参考RTOS 公开语义)

- `0=no-wait`(单次复核即判负)/ `RT_WAIT_FOREVER=~0UL` /
  其他=绝对截止;旧名(rt_sem_take 等)=FOREVER 薄壳零签名破坏。
- 五字段登记:`pend_deadline`(0=未武装哨兵,扫描门——wait_mask
  型壳 RX 与事件注册表型 BLOCK 一律免疫)、`pend_qh/qt`(自摘回指)、
  `pend_owner`(互斥挂等时=锁主)、`timed_out`;create 复用槽必清
  清单+超时返回处清(评审核 blocker:残留=复用槽首个挂等假超时)。
- 扫描(核 0 单写者环)摘除+owner 重算;give 侧懒跳过僵尸头为纵深
  防御。事件组(registry 机器)不进本站,记残余;give 醒后条件被夺
  的重挂分支无确定性编排,代码评审覆盖,记残余。

## B 块:panic 观测面

- `rt_panic` 扫描面(无寄存器行——ESR/ELR 此刻是残值,印了冒充
  现场,只印 `context: tick scan`)/ `rt_panic_fault` 陷阱面
  (ESR/ELR/FAR 架构有效才印)。入口 `daifset #0xf` 防打印中再入,
  `panic_done` 单次打印保证(扫描路径每 tick 重入会无限复读)。
  **裸 `puts_uart` 不取控制台锁**——持锁任务被打断的 tick 语境取
  锁=同核自死锁。`rt_sync_fault` 未知 EC 静默 wfi 退役。
- `rt_puts_hex` 同律裸打;判据 needle 取 "esr=0x" 前缀稳。

## C 块:栈防护

- 栈行 4368B=16×273(行内 16 对齐保持):[0..263] 垫 / [264..271]
  magic u64 / [272..4367] 可用 4096B(顶=行尾,语义不变)。
- 每 tick 哨兵扫描(已创建槽);水位=create 时 0xA5 填充,
  自可用底升址找首个非模式词;`ps` 追加 ` stk <B>` 列(idle 行
  ` stk -`);`stackcheck` 逐活任务水位行+全哨兵判定,完好才印
  `stk: magic ok`(不印槽计数——[41] 时点槽 5-7 从未占用,"8/8"
  不可判,评审核)。

## 实测三雷(全部进了代码注释)

1. **`__builtin_frame_address` 在 -O2 无帧指针下不可用**:首版
   deepdiver/overflower "深度判据"零下潜——"dived" 瞬印、水位满格、
   哨兵完好,全静默。修=asm 直读 SP(`mov %0, sp`),任意优化级皆稳。
2. **递归帧稀疏足迹**:fp 对+单字节 pad 的下探写不连续——深潜
   穿过哨兵而 magic 恰落缝中(无 panic 无 margin)。修=每级全宽
   `volatile u64 pat[8]` 逐词写+活过递归点(反 TCO 同源),足迹
   连续铺满下探路径;deepdiver 止点符号错误(limit-256 穿透误植)
   一并修为 limit+256。
3. **overflower 垫区穿透(评审核 CONFIRMED)**:v1 越哨后在深 sp
   处 wfi——tick 的 272B 异常帧+panic 调用链 C 帧全部压在垫区,
   必穿透行底腐写邻行栈顶。修=**解栈回浅再 wfi**:overshoot 只留
   在递归足迹里,异常帧永远落在正常深度;垫只需吸收 ~200B 足迹。

## 判据与门禁 72→75(三处同步同 commit)

- +[40] tmodemo:a(8 驱动)/b(9 取者)/c(14 锁主)全确定性编排,
  8 needle + PI 14→9→14 序断;
- +[41] stkdemo:dived→stackcheck(deepdiver margin 224 << shell
  3648 + magic ok)→ps 水位列;
- +[74] overflow→panic 指名(vm0 exit 死后 vmcreate/vmrun/
  vmconsole 复活链,vm2-Linux 仍在场=多 VM 并存真实运维路径;
  panic 壳即死,逃生夺回再 vmkill 0);
- 号段手术:/72]→/75] 后降序 40..72 各 +2,终局 [72]→[75],新门
  最后插入零碰撞,assert 每号恰一次。
- verify 锚工程:[41] 首版锚 `rtos> ` 等到的是上一命令**在途旧
  提示符**(跨过 start 线后到,秒匹配假回,ps 表本体未达)——
  修为两段锚(表头→首个 ` stk ` 列);站二"read_until 下界纪律"
  的又一实例,且探针侧同病先现过一次("CREATED" 陈旧匹配假阴性)。

## 门禁战况(诚实记录)

- R1:[41] 在途提示符锚炸(修锚,非产品伤);
- R1b:[61] vm1 spin 超时等 "spinning..."——**[54] 观察哨家族
  第 2 案**(历史 1/8、1/12 单发;本站 diff 零接触 vm1/RX/spin
  路径,签名完全吻合,按哨记档不刷绿);
- R2/R3:**连续两轮 75/75 全绿**(RESULT PASS ×2)。
- **实现后评审(4 镜头 × 逐条证伪链,13 agents)**:9 major
  CONFIRMED 全数收治——成功出口缴械(stale 武装穿透"wait_mask/
  事件型免疫"承诺,幻影超时醒经 rt_event_set 可致 rdy 双挂腐化)/
  获取即继承(pend_owner 交接 stale+新主不继承,PI 断链)/递归
  驱动段封 DAIF 窗(末两级帧距哨兵不足 272B tick ISR 帧,同拍
  假 panic,[41] ~1e-4 级闪断面)/overflower floor 抬 160(96B
  帧不越行底)/handoff+give 弹推原子/Makefile 与 help 10-29 段
  九行站十九遗留错位(全偏 -1 含 phantom 跳 29)等;两残窗
  评审定谳不修、注释在案(泄漏锁后槽位复用 stale 重算、give 先弹
  后算为站十八存量语义)。收治后重建零警告、探针全弧无损,v3 双绿
  见上。
