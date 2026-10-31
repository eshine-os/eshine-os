# 站三十 devlog:三段消息 IPC——微内核的对话机器,87 门双绿

日期:2026-10-31 · 作者:Julian Chu + Claude Code
状态:**已收口**——87 门(新增 [81-83] IPC 三门)连续 2 轮全绿

## 0. 本站做了什么

三段消息(Send-Recv-Reply)+rcvid 复合+优先级序到达队列+脉冲
+abort/UNBLOCK(参考实现 同步队列件 的站内尺度收编,机制参考零拷贝),
六差距修复四项落码:coid 实体+引用计数(①)、消息优先级传播
(②,QNX adjust_priority 镜像)、ChannelCreate flags(③,
CHF_UNBLOCK 首面)、命名层级化(④,层级路径分段比较);事件
绑定族(⑤)的 v0 面=msg_pulse(coid) 即 SIGEV_PULSE 形;VM_SET
单拷窗(⑥)=跨址空间/跨 VM 面,站 31(用户态 server)后议——
v0 中继=内核槽 64B,每拷贝都发生在对应进程的 SVC 语境,零跨
址空间 VA 访问。

## 1. 机器落点(rt_msg.c)

- **通道**:静态表;到达面=站 17 信号量机器(rt_sem_take_to 的
  阻塞/超时/重检环整套复用——RECV 的等待/唤醒/超时一行没写
  新机器);
- **到达队列**:按发送者优先级插队(低值高优先);
- **优先级传播**:发送者阻塞→提权阻塞中的接收者(boost 到发送
  者 prio;还原锚=base_prio,站 18B PI 同律;与 PI 互斥叠加
  交互 v0 简化记档);
- **三段**:SEND(拷 sbuf→内核槽+give 到达面+阻塞在回执信号量,
  支持超时)→RECV(take 到达面+拷内核槽→服务器缓冲+返回
  rcvid;脉冲 rcvid=0)→REPLY(拷服务器回复→内核槽+status 作
  发送者返回值+give 回执+提权还原);
- **脉冲**:8B(code+value)异步不阻塞;系统码负(UNBLOCK=-1)
  用户码≥0;
- **abort**:发送者超时先走:未收走(QUEUED)=直接撤返
  -ETIMEDOUT;已收走(RECEIVED)=递 UNBLOCK 脉冲给服务器
  (value=被持 rcvid),服务器 REPLY 静默收尾(QNX unblock 镜像);
- **coid 实体**:连接表+引用计数(同目标聚合复用),断连脉冲
  (CHF_DISCONNECT)=站 31 接管后议;
- **命名**:路径表+分段比较(逐段精确,杜绝前缀误中)。

## 2. 判据面

- **[81] ipcdemo 三段+传播活证**:壳(prio 5)spawn 服务器
  (prio 20)+占先干扰(prio 6,1 拍自旋/1 拍睡占空,窗 30 拍)
  →name_open→send(41)→reply 42。**干扰在跑,dt=0 快返**=
  提权让接收者越过 prio-6 干扰服务——不传播就轮不到(20>6 永远
  让路),dt 就不是 0。冒烟曾 dt=100 满窗超时,正是 rcvid 校验
  自雷(见 §3)。
- **[82] pulsedemo**:5 连发异步脉冲(code 0..4)→服务器排空
  自报"drained 5 sum=10"。
- **[83] abortdemo**:发送者 30 拍超时先走(-110)+服务器收到
  UNBLOCK 脉冲(value=被持 rcvid)自报——两脸齐=QNX unblock
  闭环。

## 3. 自雷两枚(冒烟当场抓回)

1. **rcvid 校验反谓词**:`rcvid & 0xFULL`(槽+1,恒非零)写成
   拒绝条件=合法 rcvid 全数被拒 -22,回执永不达,发送者恒满窗
   超时(dt=100,reply -110 打无符号大数=0xFFFF...FF92 定谳)。
   修:`!(rcvid & 0xFULL)` 才拒。
2. **三段语义搅锅**:壳 demo 把"回执数据落点"和"返回值
   (REPLY status)"共用一个变量——内核先写 rv=42,返回值 0 又
   覆写它(reply 0 定谳)。修:分家(r=status, rv=data)。
   两雷都是**判据面自己抓的**:dt=100 指向回执不到,reply 0
   指向变量搅锅——门禁先于修复。

## 4. ABI 注记

- sys6 壳裹:x3-x5 已是入参不得再列 clobber(gcc 报 asm 冲突);
  六参满额后剩余 caller-saved x6-x18 照列——**站 28 ABI 的
  六参上限,IPC 面(msg_send 恰 6 参)即其首个满额用户**;
- 参考实现的 x0-x6 七参(比 Linux 多一)走结构体指针=登记偏差
  #27 的延续,IPC 面按六参排布零冲突。

## 5. 边界与不做(v0 诚实入档)

消息 ≤64B 内核槽中继(参考实现 1024B+长消息映射=跨址空间面后议);
iov 不做;断连脉冲不做(站 31);VM_SET 单拷窗不做(跨 VM 站);
提权还原与 PI 互斥的叠加交互简化(单 boost 源,叠加场景待
站 32+);服务器=内核任务直调(用户态 server=站 31 本题)。

## 6. 下一站

站 31(server+rootfs):开机链(cpio→PM→ramfs→procfs→tty
driver→busybox init→rcS)+fd=通道三元组——三段消息的地基
今天打好,明天让用户态 server 用它服务世界。
