# 站五十七 devlog:AF_UNIX socket 服务(批次 C 首站)

> 承接站 56(6860bea)。批次 C(57-59)首站;门禁 **164→167**,冒烟
> 10/10 全 PASS。镜像机 未同步(用户令)。

## 0. 机制

- socksrv=第九内核服务器(own channel+/dev/sock 元名;boot 链尾静默起,
  零 boot 期往返——站 50 纪律)
- 真号八门(socket 198/bind 200/listen 201/accept 202/connect 203/
  sendto 206/recvfrom 207/shutdown 210;aarch64 asm-generic,站 42 定号
  纪律——musl 直连零 patch)
- 控制面 wire ops 12-17;**数据面零新 op**(sendto/recvfrom=fd≥3 读写
  面路由,同 sys_read/sys_write 律)
- connect=即建对(accepted socket 预入听者 accept 队=backlog 语义);
  空读/空 accept/满环=-110 有界窗面(pipesrv v0 同形);shutdown/close
  =断对+对端 EOF(READ 恒 0);DUP=refs 引用计数(fork 继承镜像,
  pipe 同款——注释直陈其反面:不应答=子退出 CLOSE 误收对象)
- fd 辅助三件共享化(fd_zero/fd_slot_ok/fd_used_count 去 static+
  rtos.h 原型;rt_fsrv.c 出+rt_sock.c 吃)

## 1. 战雷

- 首版场景缺 `vmconsole 0` 切台步——`run` 发进了宿主壳("unknown
  command: run");站 50 场景为正解(切台+空行收 rtos> 提示)。日志
  定谳一步(rtos> 提示符 vs eshine>)。
- errno 块重排误删既有 ES_EPERM(sys_setsid 编译雷);裁重时又误删
  自用 ES_EFAULT/ES_ENAMETOOLONG。教训:动共享头定义区,先 grep 全
  消费者再裁。既有 ES_ERESTARTSYS=(-512) 负值语义——臆抄 +512 会
  改语义(重定义编译器不报,同值不同号才报)。

## 2. 判据与验收

冒烟 10/10(sockdemo 回声 "echo ok <ESHINE57>"+hello 共存面);门禁
[165-167] 写而不跑(八门往返+对象回收复用+fd 层无回归);help 167
步;三处同步同批。sockdemo=单进程回环序(connect 先于 accept+send
先于 recv=零重试环零时序——冒烟确定性优先,fork 竞态面=后弧已由
DUP refs 垫底)。
