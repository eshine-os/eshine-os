# 站四十五:APS+MPRT 分区化(CPU 预算分区+内存预算分区)

**日期**:2026-11-02 | **序**:批令 43-48 第三站 | **基准**:参考实现
k_aps.c(1108 行)白描——QNX APS 机制;MPRT(参考实现 k_apm.c)
白描——任务组内存记账(charge/uncharge,max+usage)。

## §1 机制白描(参考实现真身,零拷贝红线)

- **APS(自适应分区调度)**:k_part_create(perc_budget %,name)
  ——百分比 CPU 预算分区;任务入组(k_part_task_join);每系统
  tick 记账(k_part_stat_tick_update 对 cur 任务);超标门
  (is_part_stat_free);切换面(k_part_swtich);分区显示
  (k_aps_show)。QNX 语义核心=**保证(guarantee)+偷闲
  (steal)**:预算是保证下限,空闲 CPU 可偷。
- **MPRT(内存分区)**:mprt_alloc(name)/attach(pid,mprtid)/
  mprt_charge(mm,nr_pgs)/mprt_uncharge——任务组内存上限 max+
  当前 usage,在**页面分配点**记账,超限拒绝;fork 继承
  (mprt_fork)。

## §2 v0 站内形状(契约决定)

- **CPU 分区(rt_part.c)**:`struct rt_cpart{name,budget_pct,
  credit}` 池 4;**分区 0=system(boot 建,100%)**;新分区
  从 system 扣预算(Σ≤100 纪律)。task->cpart(默认 0)。
  **tick 记账**:每 tick 对 rt_cur 记 1(两核同面;idle 池外
  免记账);**窗=20 tick**:rt_ticks%20==0 全 credit 重置=
  budget×20/100。**超标**:credit<0 且**别人还在跑**(池内他任务
  RUN/READY)=节流——复用睡客机器(state=RT_SLEEP,wake_tick=
  下窗界,站 17 零新等待逻辑);**无人跑=偷闲**(free CPU,
  QNX steal 同义)。粒度=tick(参考实现同款 tick 粒度白描)。
- **内存分区(rt_part.c)**:`struct rt_mpart{name,max_pages,
  cur_pages}` 池 4。proc->mpart/-1+mpart_pages。**记账点=
  声明窗**单一咽喉:as_declare_pool(mmap 与 fork 同走此口,
  一钩双面)——charge 失败 -12 拒绝(vma 回滚);munmap 成功
  uncharge;退场 rt_proc_exit 全退(fork 继承=子 VMA 重声明即
  自带记账)。**参考实现记 resident 页,我们记声明窗(declare-time)
  =v0 边界记档**;attach 前 VMA 不追溯(策略起点面)。
- **零新 syscall**:demo 级内核 API(参考实现 k_part_create 同为
  内核面);shell `parts` 命令=k_aps_show 镜像。

## §3 判据(demo 三件)

- **apsdemo**(CPU):建分区 A=20%(system 100→80);起两个同
  低优先级忙任务 ta(A 组)/tb(system);壳睡 40 tick(两窗)
  → ta 实得 ~8 tick,tb ~32,**share≈20%**(带 10-30 宽判);
  tb 退场后再窗:ta 独跑**偷闲面**(30 tick 实得 ≥25)。
  计数=每任务观测 rt_ticks 变化沿(tick 粒度实跑账)。
- **mprtdemo**(内存):建 mp{max 16p};fork 子自 attach:
  mmap 16p ok → 再 mmap 拒 **-12** → **fork 孙被拒 -12**
  (子窗全拷入组=32p>16p,内存隔离带 fork 面)→ munmap 后
  mmap 复 ok(uncharge 生效)→ exit;父收尸+表打印 cur=0
  (退场全退面)。壳永不 attach(下游门不被绑)。
- **parts**:两表显示(name/budget/credit;name/max/cur)。

## §4 非目标(记档)

- QNX APS 临界线程豁免(criticality exempt):v0 全组同律。
- MPRT resident 记账/页池粒度:declare-time 模型记档差异。
- 跨分区优先级传播/分区迁移(migrate ISR):无消费者不落码。
