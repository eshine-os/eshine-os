# 站三十五 spec:线程(clone+TLS)——多执行体纵队的第一柱

日期:2026-11-01 · 作者:Julian Chu + Claude Code
基线:站 27 普查 §10(参考实现每任务 pending/sigqueue+TLS/tid);
musl 1.2.5 aarch64 线程源(clone.s/pthread_join.c/pthread_create.c)
实测契约。红线:机制参考、零代码拷贝。前置:站 34 收口 2fc49f8
(93 门双绿);用户令 11-01:35→41 连续开发,站间不停。

## 0. 目标

musl pthread 在 vm0 落地:pthread_create/join/mutex(经我们
futex)全链活。解锁三缺口(线程自身+per-thread 信号面+
futex PI/robust 的地基)。判据=新门 [94] `run /bin/pt`:
4 线程互斥累加,joint 后计数自证。

## 1. musl 实测契约(从源码钉死)

- `__clone(func,stack,flags,arg,ptid,tls,ctid)`→SYS_clone
  (220,flags,stack,ptid,tls,ctid);子=帧整拷+**x0=0**+SP=
  传入栈(父已把 func/arg 压其上);子 func 返回→SYS_exit(93)
  =**线程退场必是纯任务死**(非 leader 不动 proc)。
- flags=CLONE_VM|FS|FILES|SIGHAND|THREAD|SYSVSEM|SETTLS|
  PARENT_SETTID|CHILD_CLEARTID|DETACHED。
- **join=纯用户态 futex**:等 detach_state;**CHILD_CLEARTID=
  内核责任**:线程退出时 *ctid=0+futex 唤醒(musl 用它锁
  thread_list);PARENT_SETTID:内核写 tid 到 *ptid。
- TLS:**TPIDR_EL0 每任务上下文**——内核在任务切换点存还
  (CLONE_SETTLS 给新线程 tp 初值;musl 主线程自写)。aarch64
  的 set_thread_area=用户态 msr,无内核面。
- **futex 契约修正(关键雷面)**:musl 传 op|FUTEX_PRIVATE_FLAG
  (128)→**掩掉**;x3==NULL=无限等(旧=禁等探针,撞 musl join
  假 EINVAL 速回);x3=timespec 指针→相对时长折 ticks
  (CLOCK_REALTIME 无墙钟,WAIT_BITSET 绝对面 v0 降格相对,
  记档)。自家 futexdemo 同步迁新契约(NULL=无限/指针=时长)。
- exit_group(94)/leader exit:清扫同 proc 全部线程任务
  (孤儿 futex 持有者不悬空)。

## 2. 内核件

- task 尾追加:tpidr_el0@736/ctid@744/tid@752(+断言);vec.S
  簿记宏加 TPIDR_EL0 存还(EL1 可读写 EL0 寄存器)。
- rt_thr.c(新):sys_clone_impl(帧整拷同 fork 无 AS 拷/同
  proc 共享 fd+handler/ptid 写入/tid 分配)/线程 exit
  (ctid 清零+futex 唤醒+纯任务死)/exit_group 清扫。
- set_tid_address:返 per-task tid+记 ctid 地址(Linux 同位)。
- 池与窗扩容:RT_POOL_MAX 8→16(任务栈+64KB bss),
  RT_AS_VMA_MAX 6→32(musl 线程栈 mmap+TLS 地图;2MB 窗
  4 线程×~140KB+主栈+stdio≈700KB 余量足)。
- 号面:clone=220/gettid=178(真 Linux 号)。

## 3. 判据面(门禁只增不减:93→94)

pt 门落 [89](插 [88] musl 左翼后,旧 [89-93] 顺移 [90-94],
站 33 同款):`run /bin/pt`——musl pthread demo(4 线程+
PTHREAD_MUTEX_INITIALIZER+join;build-musl.sh 扩 pt.c,产
物入仓同 hello)——锚 "pthread: 4 threads joined, counter=10"+
"pthread: self="+"run: exited code=0"。mutex 争用路径即
futex WAIT/WAKE 真线程对(站 32 扑克代演→真第二执行流,
代演退役)。futexdemo 门 [86] 契约迁移:x3=NULL=无限等/
timespec 指针=真超时(PRIVATE 旗掩);sigdemo [87] 子支
futex 同步迁(旧 400=ticks 字面在新契约=野指针,10s 垫背)。

## 6. 战果定谳:帧恢复机器不可重入(vec.S 两掩)

pt 冒烟 50% 挂率,两脸共一根:**panic 脸**(esr=0x96000006
EL0 数据中止 / elr=0x4000b088=弹栈尾 `ldr x5,[x0,#672]` /
far=0x2a0)与**无声楔**(child 标 RUN 永不续跑,tick 爬行靠
泵救,gdb 帧尸=弹栈中途位)。根因:站 28 SVC 语境开中断
(vec.S daifclr 契约)下,**帧伪造+簿记+.Lpop 弹栈全程裸奔**
——tick 落窗即嵌套 IRQ 存嵌合帧(.Lpop 顶 msr spsr_el1 已装
新任务 EL0 态):不切换路径 eret 把执行流以 EL0 送回尾巴 PC
(x0=刚弹出的新任务 x0,clone 子=0)→ `ldr [x0,#672]`@EL0
far=0x2a0 三数全咬合;切换路径帧半消费弹垃圾→eret 野址→
RUN 标记永失。**站 28 前 7 站无雷只因无同优先级 RR 竞争者
令 tick 真切走 mid-tail 任务——pt 4 线程同优先级=首个命中
场景**(hello/旧 demo 嵌套 tick 走不切换路径,窗内返回
EL1 续跑无害)。修:①rt_switch_to push 后、psr 伪造前
`msr daifset,#0xf`(push 段可被抢占无害:帧完整+psr=EL1h
非嵌合);②RT_EL0_RET 宏头同掩。站 28 契约不破:泊位
pstate 由 C 侧显式写帧,eret 携用户 psr 自动回开。

## 4. 边界(v0 诚实)

per-proc 位图 pending(每任务 pending/sigqueue=站 38);
futex PI/robust 不做(musl 默认 mutex 用不到);pthread_cancel/
信号定向线程不做;SMP:线程单核亲和(v0 全核 0);
CLONE_PARENT/vfork 等非线程族拒;线程优先级=创建者同优
(Linux SCHED_OTHER 同类近似)。

## 5. DoD

spec→实现→94 门×2→devlog→内容包 5→commit→镜像机→记忆→
**直开站 36(shm+xspace),不停**。
