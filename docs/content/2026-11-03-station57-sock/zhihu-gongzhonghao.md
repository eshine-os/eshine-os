# socket 上了微内核,数据面却零新 wire

站 57,批次 C 首站:AF_UNIX socket 服务。这个站最好的部分不是
"有了 socket",而是看微内核架构在第九个服务器身上兑现复利。

## 八门真号,零臆造

socket 198、bind 200、listen 201、accept 202、connect 203、
sendto 206、recvfrom 207、shutdown 210——aarch64 asm-generic 的
syscall 号,一个都不是编的。判据很硬:musl 的 demo 源码里一行
patch 都没有,`socket(AF_UNIX, SOCK_STREAM, 0)` 编出来直连,
回声打印 `sockdemo: echo ok <ESHINE57>`。

站 42 立下的定号纪律(号面从 musl 头钉死,不靠记忆)在这里直接
兑现:八门全真,直连全通。

## 数据面零新 op

这是本站的架构题眼。fd 层早就有一套 wire 契约(fd=通道三元组
{coid, ocb},READ/WRITE 走三段消息往返到 resmgr 服务面)。socket
的 fd 也住进这张表——于是:

- sendto → fd≥3 的写路由(同 write)
- recvfrom → fd≥3 的读路由(同 read)

**数据面一条新 wire 都没加。** 第九个服务器只带来六条控制线:
SOCKET/BIND/LISTEN/ACCEPT/CONNECT/SHUTDOWN(wire ops 12-17)。
上一个拥有这种待遇的是 pipe(站 37):当时 READ/WRITE/CLOSE/DUP
全是复用。

## connect 即建对

POSIX 语义里,connect 返回后、accept 之前,数据已经可以流了
(backlog 缓冲的本义)。v0 的形状:connect 时就建好 accepted
socket,预入听者的 accept 队列——accept 只是出队,数据在
accept 前即可流。队满拒连(-12),空队 accept 回 -110 有界窗
面(pipe 同款,消费者重试环自持,POSIX 无限阻塞=后弧记边界)。

## fork 是隐藏考题

demo 没用 fork(单进程回环序,connect 先于 accept、send 先于
recv——零重试环零时序,冒烟确定性优先)。但 rt_fd_fork 对每个
继承的 fd 都发 DUP 消息给服务器:pipe 的注释早就写了反例——
不应答 DUP,子进程一退出,CLOSE 就会把父进程还在用的对象收走
(EOF 假脸)。所以 sock 对象带 refs 计数,DUP++/CLOSE--,归零
才收。

一块木头没敲,雷先拆了。

## 战雷两枚,都在定义区

- 首版冒烟场景少了 `vmconsole 0` 切台步,`run` 发进了宿主壳
  ("unknown command: run")——看一眼日志,提示符是 eshine>
  不是 rtos>,一步定谳。
- errno 定义块重排,误删了既有的 ES_EPERM(别处编译雷);裁重复
  时又误删了自用的 ES_EFAULT/ES_ENAMETOOLONG。教训:动共享头,
  先 grep 全消费者再裁。还有一发哑弹:既有 ES_ERESTARTSYS=(-512)
  是负值——臆抄成 +512,同值不同号,编译器不报,语义已经变了。

门禁 164→167,冒烟 10/10。下一站:virtio-net,OASIS spec
clean-room。
