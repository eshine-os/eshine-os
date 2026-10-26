<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 我的 RTOS 学会了"等待"——顺便发现自己一直按名字长度发栈

> eshine-os 从零系列 · 第十七站。VM0 的任务内核补上同步原语:
信号量+互斥锁。首跑即炸,炸出的却不是新代码,是一个潜伏了一整站
的老 bug。

## 先看结果:阻塞成了"活状态"

```
[vm0] rtos> mutexdemo
mutex: give denied (not owner)      ← 非拥有者给锁,礼貌拒绝
mutex: waiter wants lock
mutex: contender locking
mutex: contender locked
[vm0] rtos> ps                     ← 趁持锁者睡 300ms,查任务表
  shell     prio 5   RUN
  contender prio 6   SLEEP         ← 持锁睡
  waiter    prio 8   BLOCK         ← 挂在互斥锁的等待队列上
  heartbeat prio 10  SLEEP
[vm0] mutex: contender unlocking
[vm0] mutex: waiter locked (after block)   ← 醒序即证据
[vm0] mutex: recursive take 2 ok          ← 递归重入
```

判据的关键不是打印了多少行——是 **ps 表里的 BLOCK**:阻塞不是任务
自己打出来的日志,是调度器账本里的活状态。这是站 16 的任务表在新
原语下的第一次"体检"。

semdemo 则是有界环:empty(4)/full(0) 一对信号量,producer 优先
级高先灌满 4 槽,挂等;consumer 消费一个就 give,唤醒更高者**立即
让位**——乒乓到 8/8,每条生产/消费行都在日志里排成可数的序列。

## 等待的契约:唤醒=允许重试,非授予

设计里最值得说的一条。take 写成重检环:

```c
for (;;) {
    if (count > 0) { count--; return 0; }
    pend();          /* 挂到对象的 FIFO 队列上 */
}
```

give 让渡资源、唤醒队头,但**不承诺资源归它**——被醒者回到循环里
自己复核。被别人抢走(barge)只会重新挂上:无丢醒、无死锁,FIFO
公平在单核+唤醒即抢占下事实上成立。写条件变量的人都懂这个形状,
RTU 的教科书叫它"挂等-重试";我给它记的账是:**把"等待"从壳任务
的串口位里解放出来,任何任务都能挂任何对象。**

顺带把调度底座封了窗:就绪队列从站 16 起就有任务/ISR 双写者交错
丢任务的潜伏窗口(两个写者隔着 tick 抢切挤尾链,任务会被挤出队列
且尾指针与真链脱钩——后续入队全部落空,永久丢失)。本轮全部短
DAIF 临界段收敛:save/restore 而非 set/clr,ISR 语境不泄掩码。

## 首跑即炸:凶手不是新代码

C 块判据首跑,[26] 超时,VM0 以取指中止(目标 0)死亡。-d int 的
尸检一锤定音:两发 eret 紧邻,第二发**去了 EL0、PC=0**——有人弹出
了一个全零帧。帧基址 0xd430,对表:落在 bss 空洞。反推:
0xd430 = 任务栈基址 + 9×4096 − 272 = **"槽位 8"的帧位**,而任务池
只有 8 个槽(0-7)。objdump 把新原语、调度器、帧伪造的编译产物逐
条审完——全部清白。

最后回头读站 16 的 `rt_task_create`:

```c
for (i = 0; ...; i++)       /* 拷贝名字 */
    t->name[i] = name[i];
...
rt_frame_init(t, task_stacks[i] + 4096, ...);   /* i 已是名字长度! */
```

**栈下标 = 名字长度。** shell(5 字)一直住在 stack[5];heartbeat
(9 字)住在越界的"stack[9]",压着 virtio 队列顶活了整整两轮门禁;
gate16/fptest(6 字)撞同一个 stack[6],但一先一后顺序复用,从未同
框。**站 16 全靠"名字长度不撞车"活着。** 而我的新 demo 一口气造了
第一对同长并发任务:producer 和 consumer,双双 8 字,共享 0xd430
同一帧区——两个身份互踩,栈混乱后弹零帧,eret 去 EL0。

修三行:槽位号独立保存。这条和站 16 的"noreturn 死代码"同级:
**循环下标复用,是静默杀手;两轮门禁绿不等于没有雷。** 新判据第一
次把雷踩响——门禁就是尸检报告。

## 诚实记录:门禁也会闪

双绿认证跑在 [53](Linux 解包到 /init 那步)超时:内核时间冻在
0.489s,而 VM0 心跳照常。这是站 16 记档过的残余物理——
`vgic_wake_waiter` 的**无条件关线**遇上"HW LR 的挂起跟物理线走":
宿主相里一颗在途未消费的定时器拍,关线一落就蒸发;解包期的 Linux
没有任何别的中断来唤醒它重新编程定时器,一拍冻即死。四轮观察一挂,
如实记档不粉饰;真正的修法(在途时立即回业主 VM 消费自己的拍,
而非关线)是站 18 的首选候选——它现在有两次独立症状报告了。

## 尾声

58 步门禁,连续双绿(两次认证通过 + 一次闪断如实入册)。下一步
的候选排开:vgic 在途拍守门扩展(修 [53] 闪断)、优先级继承、
per-task FP。这个站的哲学收获只有一句:**会等,才是内核。**

(eshine-os:Apache-2.0,QEMU virt,从零 clean-room,参考RTOS 公开设计
为基准,参考实现派生树一页未翻。)
