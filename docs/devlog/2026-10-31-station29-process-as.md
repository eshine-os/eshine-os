# 站二十九 devlog:进程+地址空间——每进程半区活体,84 门双绿

日期:2026-10-31 · 作者:Julian Chu + Claude Code
状态:**已收口**——84 门(新增 [78-80] 进程三门)连续 2 轮全绿

## 0. 本站做了什么

EL0 从"单一用户段全体恒等"升格为**每进程地址空间**(参考实现 §7/§8
机制白描的站内尺度收编):每进程自持 L0/L1/L2 三级表、ASID 位图、
调度切换点随任务装 TTBR0;EL0 缺页从"诚实终态面"退役为按 VMA
申报补页;进程生命周期三态(CREATE 挂起/COPY_UCONTEXT fork/
EXEC_SETUP 帧改写式 exec——**内核零镜像字节知识,by construction**)。

## 1. VA 分区(l1 槽,2MB 粒度)——站 28 两区式的换装

```
[0]    内核块        EL1-only   恒驻每 mm(含内核 PGD)
[1]    .user 段      per-proc 表  壳/子进程的镜像+用户栈
[2..5] 页池 8MB      EL1-only   恒驻每 mm = "内核高半区"
[6]    mmap 窗 2MB   per-proc 表  ANON 缺页零填的 VA 落点
[7]    DTB           EL1 RO     恒驻每 mm(内核 fdt 读)
```

**设计自抓(实现中段翻案)**:首版把页池当"用户 mmap 的同一 VA
区"——fork 的内核态页拷贝要写池页,而内核 PGD 用户块已剥、
copy 语境的进程表也不含未映射的池 VA:**拷贝目标不可达**。解法=
VA 分区:页池自任"内核高半区"(所有 mm 恒驻 EL1,用户永不可见,
参考实现高低半区布局的服务面形状),用户 mmap 另立块 6 窗。一次
重设计消掉整类可达性问题。

## 2. 机制落点

- **切换点**:RT_SWITCH_BOOKKEEP 增一段——任务 ttbr(@696 契约,
  asid<<48|L0)每切换必装,0=内核 PGD(双间接经 rt_mm0_l0);
  ASID 标签保 TLB 跨切换存活,免逐冲。x3 在 sp 取值后已死,复用
  零加 scratch。
- **缺页**:rt_el0_fault 先判 FSC=0x04..0x07(翻译错)→ rt_pfault
  查本进程 VMA:ID 型(镜像/栈)补 VA→同址恒等页;POOL 型补
  页池零页。命中即返,vec.S 的 RT_EL0_RET 原地重执行(fault 路
  原为"不返回",本站补上返回面)。VMA 外/其余 FSC=终态面不动
  (v0 边界诚实保留)。
- **fork(COPY_UCONTEXT)**:子进程 eager 全拷(父块 1/6 已映页
  逐页拷入页池,子 VMA 全转 POOL 型=物理隔离即本站存在意义);
  子任务帧整拷 272B+x0=RT_FORK_MAGIC。协作式无抢占窗,帧覆写
  与子首派发零竞(父 prio 恒低于子)。
- **exec(EXEC_SETUP)**:7-参帧改写参考实现形状(站内 x0-x2 入参+
  x3-x30 整帧卫生清零),内核不碰任何镜像字节;exedemo 二相函数
  演毕 exec 回壳任务续行(v0 无用户态 loader 的诚实替代)。
- **wait4/exit**:站 17 等待机器(RT_PROC_BIT)+ ZOMBIE 收尸
  (exit 释页池/L2/ASID+TLBI 全冲,reap 清登记)。
- **号面**:exit 93/mmap 222/munmap 215/wait4 260=标准号(子集
  语义入档);fork 520/exec_setup 521=扩展(参考实现形状非 Linux
  旗语,诚实独立号)。

## 3. 自雷两枚(当晚复现器抓回)

1. **exec arg=0**:分发器的 `FR(0)=返回值` 回写发生在 impl 之后,
   把帧改写的 x0=arg 覆成返回值 0。修=exec 返 a0——exec 语义里
   "返回值"本就是新语境的 x0,一字两用。
2. **门锚 `\n` 撞串口 `\r\n`**:verify 首轮 [79] 门
   `mmapdemo: unmapped\n` 永不至——vuart 行尾转义,锚不带换行
   (站 28 utest 门接线同族教训:判据锚先在串口实貌上过一遍)。

## 4. 判据账

- 冒烟一面即活:fork 全链(child alive+reaped pid=2 code=42)、
  mmap 缺页零填+rw+munmap、exec 二相改写+回壳,sleep 链,三法医
  旗(tp-corrupt/panic/CARM)全无。
- **84 门连续 2 轮全绿**(RESULT PASS ×2):[78] forkdemo 全生命
  周期/[79] mmapdemo 缺页零填/[80] exedemo 帧改写;[81-84] 顺移
  后热键/迟退/overflow 复活链/终局全过。
- 三处同步:verify(84 门)/宿主壳走查表(84 步)/ARCHITECTURE
  同批;guest help 补 forkdemo/mmapdemo/exedemo。

## 5. 边界与不做(v0 诚实入档)

无 COW(fork=eager 全拷,页池 8MB 充裕)、无写保护缺页、无文件
缺页(消息协议=站 30/31 接管)、无 brk、无 ASID 精细 TLBI(销毁
全冲)、mmap 无 flags 分档(v0 恒 ANON RW)。tlbi vmalle1 的代价
在多进程高频生灭场景再议。

## 6. 下一站

站 30(消息 IPC):三段消息+rcvid+短/长消息+abort 脉冲,六差距
修复清单在站 27 spec §9 已普查回填。
