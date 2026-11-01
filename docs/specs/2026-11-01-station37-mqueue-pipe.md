# 站 37 spec:posix_queue 弧——mqueue/pipe/长消息+MsgRead 镜像

日期:2026-11-01 · 作者:Julian Chu + Claude Code
参考实现机制白描参照(零拷贝红线):posix_queue 完全体
(PNODE+msg/stream 双 ops+优先级+poll-notify AND/OR/XOR
+mqueue/pipe/sysvipc 架于 k_obj 通用句柄表);长消息
≤1024B 拷内核栈缓冲/>1024B 映射;MsgRead/MsgWrite=接收者
事后按偏移读写发送者缓冲。站 36 边界(长消息+镜像=本站)。
新仓按机制形状自创。

## 1. 三件东西在我们机器里的形状

- **mqueue(rt_mq.c 新)**:内核命名队列注册表(8 队×
  8 消息,u64 载荷+优先级字节,高优先插入同优先 FIFO);
  阻塞=站 17 pend 机器第 N 笔复用(满挂发队/空挂取队+超时);
  跨进程=fork 继承 mqd(内核全局表,子天然可见)。号面
  570-574(mq_open 查名或建/mq_send(mqd,val,prio)/
  mq_receive(mqd,timeout)/mq_close/mq_unlink)。
- **pipe(rt_pipe.c 新)**:**骑站 31 resmgr 零新 fd 线**:
  pipesrv=第三个内核服务器任务(own channel+own ocb 号段=
  pipe_id*2+端号);pipe() 号面 576=内核建环+连 pipesrv+
  fd 表登记两三元组 {coid,ocb=read/write};READ/WRITE/
  CLOSE 走既有 wire ops。满环=服务器持 WRITE 消息自挂写
  待队(站 30 "占住服务器"形,abort/UNBLOCK 天然覆盖);
  空环+写端活=持 READ 自挂读待队;**写端全关=READ 回 0=
  EOF 脸**(POSIX 语义)。
- **长消息+镜像(rt_msg.c 扩)**:长槽池 8×1024B;msg_send
  现有号面 536 不动,**len>44 自动走长路**(copy_in 入槽,
  消息记槽号+len);recv 按接收者窗 copy_out;reply 归槽。
  **msg_read(chid,rcvid,offset,buf,len) 号面 575**=接收者
  收后按偏移再读所持消息(参考实现 MsgRead 镜像;长消息持有的
  延迟访问=站 36 单拷窗的近亲,先映后读不重拷)。>1024B
  映射形=后弧记边界。

## 2. demo 与判据面(门禁只增不减:96→99)

- **[92] mqdemo**:open→fork→子发三消息 prio 5/1/3→父
  收三=**5,3,1 优先序活证**+空队阻塞脸(父先收后子发,
  醒后到序)。锚:"mqdemo: order 5 3 1 ok"+"mqdemo:
  blocked-then-woke ok"+"mqdemo: unlinked ok"。
- **[93] pipedemo**:pipe()→fork→子 fdwrite 短语+关写端→
  父 fdread 循环收齐+**EOF 脸(写端关,read 0)**。锚:
  "pipedemo: got 'through the pipe' ok"+"pipedemo: eof ok"
  +"pipedemo: closed ok"。
- **[94] longdemo**:ipcdemo 形服务器任务;客户发 1024B
  图案(len>44 走长路);服务器收后 **msg_read 两段 512B**
  (镜像脸:收后按偏移再读所持消息)+验证+reply 42。锚:
  "longdemo: 1024B via long slot ok"+"longdemo: msg_read
  mirror ok"+"longdemo: reply 42 ok"。
- 插 [91] msgseg 后,旧 [92-96] 顺移 [95-99];三处同步。

## 3. 边界(v0 诚实)

mq 无 mq_notify/poll 位(AND/OR/XOR=参考实现事件组近亲,
已有 eventdemo 内核面;用户面=后弧);无 sysvipc(arm64
真表已无);pipe 无 fcntl/非阻塞档;长消息>1024B 映射
形不做;iov 快路不做;WRITE 内联 44B 分片照旧(chunked
by fdwrite);mq 消息载荷 u64(结构面=后弧)。

## 4. DoD

spec→实现→99 门×2→devlog→内容包 5→commit→镜像机→记忆→
直开站 38(signal 完整面),不停。
