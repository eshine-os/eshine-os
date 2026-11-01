# 站三十七 devlog:posix_queue 弧——mqueue/pipe/长消息+MsgRead 镜像

日期:2026-11-01 · 作者:Julian Chu + Claude Code
状态:**已收口**——99 门(新 [92] mqdemo/[93] pipedemo/[94]
longdemo,旧 [92-96] 顺移 [95-99])两连 RESULT PASS(r2+r3;
首验 r1 红=两根因见 §3,皆门/流程面非站 37 代码)

## 0. 本站做了什么

参考实现 posix_queue 白描(命名优先级队列/pipe 字节流/长消息拷
缓冲/接收者事后按偏移再读)在我们机器里的形状:

- **mqueue(rt_mq.c 新)**:命名队列注册表(8 队×8 消息,
  u64 载荷+优先级,高优先先出同优 FIFO);阻塞=站 17 pend
  机器第 N 笔复用(满挂发队/空挂取队单窗 DAIF 形+缴械律,
  零新等待逻辑);mqd=内核全局位,子天然可见(POSIX 继承的
  近亲,每进程描述符表=后弧)。close 只降描述符面,**唯一
  销毁口=unlink(POSIX 律:队列活到 unlink)**。
- **pipe(rt_pipe.c 新)**:骑站 31 resmgr **零新 fd 线**——
  pipesrv=第三个内核服务器任务(own channel+挂名
  /dev/pipe),fd 三元组 {coid,ocb} 的 READ/WRITE/CLOSE 走
  既有 wire ops;ocb=(pipe_id<<1)|端号;**写端全关后 READ
  恒 0=EOF 脸**(POSIX 语义);满环/空读=持消息挂对象等待
  队有界窗回 -110(v0 诚实:POSIX 无限阻塞全弧=后弧)。
- **长消息+镜像(rt_msg.c 扩)**:长槽池 8×1024B;现有
  send 号面不动,**len>64 自动走长槽**(copy_in 入槽,消息
  记槽号);recv 按窗 copy_out;**归还三口=所有 used=0 位**
  (sender 正常醒/abort 未收走撤/reply 见 aborted 收尾);
  **msg_read(chid,rcvid,off,buf,len)=接收者收后按偏移再读
  所持**(参考实现 MsgRead 镜像,延迟访问,重复读不重拷)。

## 1. 战果:站 31 潜雷被新消费者定谳

pipedemo 首跑必挂(子写 -9),探针流实抓:**fd 层
rt_fd_write 从站 31 起从不填 req.ocb**——tty/ramfs 的 WRITE
不读 ocb 故无感潜伏三个站;pipesrv 按端分发即爆(服务器侧
PIPEDBG: `WRITE ocb=0` 实抓)。修=fd 层补一行。**潜雷的
规律:契约字段没人读时,漏填可以绿很多年;第一个真读者
上门当天定谳。**

## 2. 冒烟自雷三枚(皆即抓即修)

- mq_streq 在 NUL 后比陈槽残值(注册时 copy_str 不清尾)
  →unlink 永不命中;修=遇 NUL 即收。
- mq_close 把 POSIX 队列当 unlink 提前销毁(无主即 free)
  →open/close/unlink 三拍序破;修=close 只计数。
- demo 判据写岔(mqdemo 多收一记再验 v==5;pipedemo 子写
  返回值初版丢弃无诊断面)——诊断探针(PIPEDBG/FORKDBG)
  上服务器侧后两雷皆一轮定位。**探针先上消费者侧,判据
  的错与机器的错分得开。**

## 3. 首验门红的两根因(皆流程/门面,非站 37 代码)

- **归档自愈缺失**:层 files/ 源 tar=gitignore 工件,站 34
  收口后无人重生——bitbake 恒吃站 34 树,[99] 三轮绿灯全是
  站 34 产品混的(参考实现也有 hello 所以绿)。修=yocto-build.sh
  每次构建前重生归档(源变即新哈希,sstate 自然失配重建)
  ——**门恒验当下提交树**。
- **[99] 会话松形状**:yocto 门自己的 QEMU 会话 attach 后
  立发命令(站 33 前的形状)——RX 注入竞态 ~30% 偶发吞字节
  =本轮门红真身。修=补 \n 唤醒探针+\r\nrtos> 配对锚(主流程
  [19] 同律)。**站 25 家族的账,这次记在门自己头上。**

## 4. 顺移脚本同型雷二犯,assert 二救

站 36 记档"全局分母替换吃锚串自身,assert 写盘前抓"——
本站顺移脚本**又写了带旧分母的锚**(机械惯性),assert 再次
写盘前拦截=零嵌合体。**纪律的形状:同一颗雷,机器接住两
次,才算接住。**

## 5. 门禁与收口

- [92] mqdemo:空队 no-wait -110+优先序 5/3/1+unlink;
  [93] pipedemo:16B 经管道+EOF 脸+双端关;[94] longdemo:
  1024B 长槽+服务器收后两段 512B 镜像再读+回执 42。
- 三处同步 99 步(verify/help 走查/ARCHITECTURE);
  devlog+内容包 5 同批;commit→镜像机→记忆;**直开站 38
  (signal 完整面),不停**。
