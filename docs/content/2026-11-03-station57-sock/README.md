# 站 57:AF_UNIX socket 服务(内容包口径)

## 一句话

RTOS 长出了 socket:八门真号 syscall 直连 musl 零 patch,数据面
不新增一条 wire——sendto/recvfrom 就走 fd 的读写路。

## 技术叙事

1. **真号**:socket 198/bind 200/listen 201/accept 202/connect
   203/sendto 206/recvfrom 207/shutdown 210——aarch64 asm-generic
   号面,一个都不臆造,musl 编出来直连零 patch。
2. **微内核正形**:第九个内核服务器 socksrv(own channel+/dev/sock);
   控制面新 wire ops 12-17,数据面零新 op——sendto 就是 fd WRITE,
   recvfrom 就是 fd READ,路由同 read/write 的 fd≥3 律。
3. **语义诚实**:connect 即建对(backlog=预入队,数据在 accept 前
   即可流);空读/空 accept/满环=-110 有界窗面(pipe 同形,POSIX
   无限阻塞=后弧记边界);shutdown=对端 READ 恒 0(EOF);fork 的
   DUP=refs 计数——注释直陈其反面:不应答,子进程退出就会把父进程
   还在用的 socket 收走。

## 金句

- 数据面不加 wire,是微内核架构的复利:第九个服务器只带来六条
  控制线。
- 阻塞语义不撒谎:回 -110 让消费者重试,比假装无限等待更诚实。

## 数字

- 门禁 164→167;冒烟 10/10;sockdemo 回声 "echo ok <ESHINE57>"。
- 8 门真号,6 条新 wire op,0 条新数据 wire。
