# 站37 口径:posix_queue 弧——mqueue/pipe/长消息+MsgRead 镜像

**eshine-os** · 站 37(posix_queue)· 2026-11-01

## 本站做了什么

参考实现 posix_queue 机制白描的站内形状,三件套:

- **mqueue**:命名优先级队列(高优先先出同优 FIFO);阻塞=
  站 17 pend 机器第 N 笔复用;POSIX 律——close 只降描述符
  面,**队列活到 unlink**;空队 no-wait -110 脸。
- **pipe**:字节流双端,**骑站 31 resmgr 零新 fd 线**——
  pipesrv=第三个内核服务器任务,fd 三元组 {coid,ocb} 走
  既有 wire ops;**写端全关后 READ 恒 0=EOF**(POSIX 语义)。
- **长消息+MsgRead 镜像**:len>64 自动走 1024B 长槽池,归还
  三口=所有 used=0 位;接收者收后按偏移再读所持=延迟访问,
  重复读不重拷。

## 本站战果:站 31 潜雷被新消费者定谳

fd 层的 rt_fd_write **从站 31 起从不填 req.ocb**——tty/ramfs
的 WRITE 不读 ocb,漏填绿了三个站;pipesrv 按端分发即爆
(服务器侧探针 `WRITE ocb=0` 实抓)。修=一行。**潜雷规律:
契约字段没人读时,漏填可以绿很多年;第一个真读者上门
当天定谳。**

## 门禁

99 门 ×2 双绿(新 [92-94],旧 [92-96] 顺移 [95-99])。
首验门红两根因皆流程面:层源 tar 自站 34 后无人重生
(bitbake 恒吃参考实现)——修=构建脚本自愈重生归档,**门恒验
当下提交树**;[99] 会话 attach 后立发命令(RX 竞态)——修=
补 \n 探针+配对锚。

## 诚实边界(v0)

mq 无 notify/事件位;pipe 无非阻塞档;长消息>1024B 映射形
不做;iov 快路不做;POSIX 无限阻塞全弧=有界窗+超时脸。

## 关键词

ARM64 hypervisor · clean-room · RTOS · mqueue · pipe ·
MsgRead · 消息传递 · 潜雷 · resmgr
