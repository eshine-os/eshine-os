# 站四十五:APS+MPRT 分区化(CPU 预算分区+内存预算分区)

**日期**:2026-11-02
**状态**:实现完成,冒烟 10/10(含四脸连炸定谳);门禁 129(新 [122-124])
按批令写而不跑
**spec**:docs/specs/2026-11-02-station45-aps-mprt.md

## 机制(参考实现 k_aps.c/k_apm.c 白描,零拷贝)

- **APS**:百分比 CPU 预算分区(QNX guarantee+steal)。窗 20 tick,
  credit=budget×20/100;每 tick 对 rt_cur 记 1(两核, idle 池外免);
  超标且**别人还在跑**=节流(复用站 17 睡客机器:state=RT_SLEEP+
  wake_tick=下窗界,零新等待逻辑);**无人跑=偷闲**(free CPU 回冲)。
  分区 0=system(100% 余量池),新分区从它扣,Σ≤100 纪律。
- **MPRT**:任务组内存上限。**声明窗单一咽喉**:as_declare_pool
  是 mmap 与 fork 共口——一钩双面记账;munmap uncharge、退场
  全退、fork 承继入组(参考实现 mprt_fork 白描)。declare-time 模型
  vs 参考实现 resident 页=记档差异。
- **零新 syscall**:demo 蹦床五 op(RT_U_APS/MPART_ALLOC/ATTACH/
  FREE/PARTS),与参考实现 k_part_create 内核面同位。

## 判据

- **apsdemo**:对齐窗界再起任务(两满窗确定性)→ a=8/b=32/
  **share=20%**(核内判带 10-30 宽)→ tb 退场后偷闲窗 45 拍
  独跑满账 ≥40。计数=任务观测 rt_ticks 变化沿(tick 粒度实跑账)。
- **mprtdemo**:16p mmap ok/再 mmap **-12**/**fork 孙被拒 -12**
  (窗全拷=32p>16=内存隔离带 fork 面)/munmap 复 ok/退场表 cur 0。
  壳永不入组(下游门不被绑)。
- **parts**:两表显示(k_aps_show 镜像)。

## 战雷:四脸连炸一根因(冒烟抓,定谳不猜)

首冒烟 6/10:sigdemo 子无输出+sigdemo2 fork failed+pipedemo
read FAIL+mprtdemo munmap 脸 wrong——**一根因**:新域 mpart/
mpart_pages 未入 rt_proc_create 清单,槽残值 0=幻影分区 0
(BSS 零初值恰是合法下标)→ 每个 fork 儿的 VMA 声明撞上
mparts[0].max=0 全拒。**站 38 律第六案**;顺藤第二雷:fork_as
旧码把 identity VMA(pool 0)也一股脑 as_declare_pool——记账
钩子下 .user 块 14 页被当匿名预算收——修=pool 0 走 rt_as_declare
专口零记账;第三补:fork 承继入组一行。三修后 10/10。

## 针线纪律执行

冒烟四失败互为镜像(全 fork 儿),按"一个根因三条症状"归约而
非逐个修;判据先于结论:先核 __user_start/end 实尺寸(14 页)
再定 identity 记账雷,不猜不补丁。

## 非目标(记档)

QNX 临界线程豁免/resident 记账/分区迁移 ISR——无消费者不落码。
