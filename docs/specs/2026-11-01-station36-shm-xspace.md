# 站 36 spec:shm+xspace——页共享机器与固定共享窗(站 30 差距⑤⑥)

日期:2026-11-01 · 作者:Julian Chu + Claude Code
参考实现机制白描参照(零拷贝红线):xspace=全局唯一共享 VA 窗
(单例 vmm+xvma 表+位图+引用计数;VALLOC 占位→PMAP 首映
→VMAP 后进者同虚址同物理);shm=动态 VA+缺页按页补;
VM_SET=登记线性窗免页表遍历单拷(纠"零拷贝"直觉:单拷+
延迟访问)。**新仓按机制形状自创实现,非逐行移植。**

## 1. 我们的世界里这两件事是什么

站 29 的 fork=eager 全拷=物理隔离即存在意义;跨进程共享
为零。本站补上**受控的共享面**:内核登记的共享段(页池取
页)+固定共享窗(块 8 @0x41000000,2MB)+fork 对共享 VMA
**不拷只挂**(同一物理页进子表)=共享语义的三根柱。

- **段注册表(rt_shm.c 新)**:8 段×≤16 页,页自页池
  (as_pool_alloc),create 即取页清零;refs=attach 计数;
  destroy 需 refs=0(退场漏 detach 由进程退场钩卸)。
- **xspace 固定窗**:段 i 固定映 VA=0x41000000+(i-1)*64KB
  ——**同一段在任何进程同一 VA**(参考实现"全局唯一窗口"的
  我们形状;v0 无 VALLOC 子窗分配器,按段整窗,记边界)。
- **VMA 第三型**:pool=2=SHM(0=ID 恒等/1=页池零填/2=共享
  段),shm_id 驻 rsv 槽;pfault 命中 SHM VMA=按段页索引补映
  (健壮面;attach 本体 eager 全映)。
- **fork 律**:VMA 驱动重写拷贝环——SHM VMA 子表**同物理直
  挂零拷贝**(refs++;同 start/end/shm_id),其余 VMA 维持
  eager 全拷。**共享段页永不入 fork/munmap/退场的页回收路径
  (三处守卫)**:munmap 拒 SHM VMA(路由 shm_detach);退场
  块 1/6 页回收不涉块 8;段页归段所有,destroy 才还页池。
- **VM_SET 单拷脸(站 30 挂账⑤⑥的 v0 兑现)**:三段消息线
  64B 不动,**载荷>64B 走共享窗**——发送者写段(一拷),
  消息只带段引用(脉冲 8B value=seg 足矣),接收者 attach
  后原地读(零拷)。免页表遍历在我们机器里天然成立(页池
  恒驻+同物理直挂),单拷+延迟访问的形状兑现;长消息
  1024B wire+READV/WRITEV 镜像=站 37。

## 2. 号面与 API

扩展号(536-539 msg/550-553 fd 已用):ES_NR_shm_create 560
(len 字节→段号)/shm_attach 561(seg,va=0 取固定 VA→实际
VA)/shm_detach 562/shm_destroy 563。musl 静态面不触
(shm_open/memfd/mq=站 37+记边界);demo 走直连 syscall 面
(站 28-31 同款)。

## 3. 判据面(门禁只增不减:94→96)

- **[90] shmdemo**:create 2 页→attach 固定 VA→父写标记→
  fork 子写同 VA+8→wait4→父读回子标记(**共享判别器:
  eager 拷会把子写私有化,父读 0=FAIL**)→detach+destroy
  refs=0。锚:"shmdemo: va=0x41000000"+"shmdemo: fork
  shared, p2=0x12345678"+"shmdemo: destroyed ok"。
- **[91] msgsegdemo**:壳建通道+连接→fork→**子**create 1KB
  段+attach+写 1024B 图案→pulse(8B,value=seg)→exit→
  父收脉冲→attach(by-reference)+原地验图案→"1KB via
  window, wire=8B" 单拷活证→detach+destroy。锚:
  "msgseg: 1024B via window, wire=8B"+"msgseg: pattern ok"
  +"msgseg: destroyed ok"。
- 插 [89] pt 后,旧 [90-94] 顺移 [92-96];三处同步 96 步。

## 4. 边界(v0 诚实)

无 POSIX 名注册(shm_open/mq=后弧);无 per-seg 子窗分配
(VALLOC);无 COW(共享段免拷即其对立面,私有页仍 eager);
attach 重复拒 -EEXIST;destroy 非零 refs 拒 -EBUSY;跨 VM
共享=站 30 差距⑥的另一半(VM_SET 单拷面只做进程间,VM 间
=后弧);段页无只读档(RW 恒)。

## 5. DoD

spec→实现→96 门×2→devlog→内容包 5→commit→镜像机→记忆→
直开站 37(posix_queue/mqueue/pipe),不停。
