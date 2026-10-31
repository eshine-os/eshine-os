# 站31 口径:fd=通道三元组,server+rootfs

**eshine-os** · 站 31(服务器+根文件系统)· 2026-10-31

## 本站做了什么

fd 不再是整数,是**通道三元组 {coid, ocb}**:每个 fd 都路由到
一个服务器的通道,open/read/write/close 全走站 30 的三段消息
往返——参考实现普查里"fdtable 与 同步队列件 的会合点"(QNX
resmgr 模型)翻成代码的一天。

- **rootfs=真 cpio(newc)**:构建期工具打包目录树,110B 头+
  13 段十六进制域+4 对齐,TRAILER!!! 收卷;内核 ramfs 服务器
  开机解析、逐路径挂名。归档内嵌内核只读区——用户窗不可见,
  文件内容只经消息送达,这正是本站本义。
- **开机链固定序列**:tty 服务器→ramfs 服务器→PM;PM 把
  stdio 0/1/2 绑到 /dev/tty 后退场(参考实现 "PM 固定序列起步"形状,
  僵化审计记档、协商位留后)。
- **POCB 引用计数双侧镜像**:fork=fd 表组拷+**两侧**引用各+1
  (客户端 coid 与服务器 ocb 互为镜像,DUP 消息);exit=逐 fd
  CLOSE。单侧只加一半会悬空:父先 CLOSE 释放 ocb,子再开
  同号=文件位置串写。

## 门禁与判据

[84] fddemo:fdwrite(1) 经 tty 服务器落串口(fd→连接→通道→
服务器→串口全链活证);/etc/motd+/etc/version 经 ramfs 服务器
读出全文+字节数自证。门 87→88,连续 2 轮全绿。

## 诚实边界(v0)

服务器=内核任务直调 IPC 层("用户态 server"二进制=站 33 musl
弧);无 seek/目录列举/权限;stdin 经 fd 恒 EOF;56B 窗客户端
循环;blk FS 观察哨记档。

## 流程雷(记档)

查存量 warning 时 `git stash` 后 make 重建了**参考实现二进制**,
pop 回源码忘了重建——对着幽灵二进制查了半轮(ps 直证法定谳)。
教训:任何 stash/checkout 类环境操作后,重建再判。
