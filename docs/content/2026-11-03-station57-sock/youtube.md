# YouTube 分镜(站 57 AF_UNIX sock,批次 C 首站)

标题:socket 上微内核——八门真号,数据面零新 wire

## 分镜

1. **冷开场(0:00-0:40)**:musl 的 `socket(AF_UNIX, SOCK_STREAM, 0)`
   直连成功画面("sockdemo: echo ok <ESHINE57>")——抛题:一个
   socket 服务器,为什么只加了六条 wire?
2. **真号纪律(0:40-1:50)**:八个 syscall 号从 aarch64 asm-generic
   钉死(站 42 定号纪律回顾:号面不靠记忆);musl 编译零 patch=
   判据本体。
3. **架构题眼(1:50-3:30)**:fd=通道三元组 {coid, ocb};socket fd
   住进同一张表;sendto/recvfrom 的 fd≥3 路由演示(同 read/write
   律);对比:pipe(站 37)先例——数据面复利是微内核的回报。
4. **语义诚实面(3:30-5:00)**:connect 即建对(backlog=预入队);
   -110 有界窗面 vs POSIX 无限阻塞(边界记档);shutdown=对端
   EOF;CLOSE 的 refs 计数——fork DUP 消息的隐藏考题(注释反例
   讲解:不应答=子进程退出误收父 socket)。
5. **战雷复盘(5:00-6:00)**:vmconsole 切台步缺失(提示符定谳法);
   errno 定义区重排误删(动共享头先 grep 全消费者)。
6. **收尾(6:00-6:30)**:冒烟 10/10+门禁 [165-167];下一站
   virtio-net(OASIS spec clean-room)预告。

## 素材清单

- QEMU 实机:sockdemo 全链(socket/bind/listen/connect/accept/
  send/recv/shutdown 输出行)
- help 走查表 [165-167] 三行
- rt_sock.c DUP case 注释(反例注释特写)
- ARCHITECTURE.md 站 57 段落
