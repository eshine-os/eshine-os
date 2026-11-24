# 站三十一 devlog:server+rootfs——fd=通道三元组与用户态服务的会合

日期:2026-10-31 · 作者:Julian Chu + Claude Code
状态:**已收口**——88 门(新增 [84] fddemo)连续 2 轮全绿

## 0. 本站做了什么

fd 不再是整数,是**通道三元组 {coid, ocb}**(参考实现 §10 fdtable/
posix_queue"会合点"定谳的站内收编,QNX resmgr 模型):每个 fd
路由到一个服务器的通道,open/read/write/close 全走站 30 三段
消息往返。rootfs=cpio(newc)归档,构建期 tools/gen_ramfs.py 打包
tools/ramfs 树、rtos/ramfs.S .incbin 内嵌内核 .rodata(用户窗
不可见——文件内容只经消息送达,这正是本站的本义)。开机链=
固定序列 tty 服务器→ramfs 服务器→PM(绑 stdio 0/1/2 后退场),
参考实现 "PM 固定序列起步"形状,F3-18 僵化审计记档、协商位留后。

## 1. 机器落点(rt_fsrv.c 新文件)

- **fd 表**:rt_proc 增 `struct rt_fd fds[8]`{used,coid,ocb}。
  路由面=连接实体(→服务器通道,站 30 六差距①),状态面=ocb
  (服务器侧每打开一份)。stdio 0/1/2=PM 开机绑 /dev/tty。
- **wire 契约**(载荷 64B 内):请求{op,a,ocb,rsv,data[44]}
  五 op:OPEN(path→ocb)/READ(ocb+窗长)/WRITE(数据内联)/
  CLOSE(ocb 引用减)/**DUP**(ocb 引用加,fork 继承面)。回复走
  站 30 status 面(status 即 send 返回值),READ 数据走 rbuf 面
  (56B 窗,EOF=0)。
- **POCB 引用计数**:fork=fdtable 组拷(参考实现 COPY_UCONTEXT
  同款)+**双侧引用**各+1——客户端 coid(rt_msg_conn_ref 新面)
  与服务器 ocb(DUP 消息)互为镜像。单侧只加不减半会悬空:父先
  CLOSE 释放 ocb,子再开同号=文件位置串写。exit=逐 fd CLOSE
  (sys_exit 内,死者本人语境阻塞合法)——死亡关流是 POCB
  引用计数的客户端镜像。
- **tty 服务器**(/dev/tty):WRITE→rt_puts_n 落串口;READ=v0
  恒 EOF(stdin 走原始 sys_read 字符面);OPEN/DUP/CLOSE=ocb 簿记。
- **ramfs 服务器**:开机解析 newc(魔数 070701/110B 头/13 段 8
  字符十六进制域,filesize@54/namesize@94/头+名补 4/数据补 4/
  TRAILER!!! 收卷;归档名存相对名,挂名补前导 '/')→逐路径
  name_attach→OPEN 查索引立 ocb{node,off}/READ 自 ocb 位置推进
  56B 窗(resmgr 本义:文件位置在每打开态)/CLOSE 引用减。
- **PM**(一次性任务):重试开 /dev/tty ×3 绑 stdio 后退场
  (任务返回即 DEAD);打印 `srv: pm bound stdio, ramfs files=2`
  为开机链判据锚。

## 2. 判据面([84] fddemo,门 87→88)

fdwrite(1,"fddemo: via fd tty")经 tty 服务器落串口——该行即
fd→连接→通道→resmgr→串口全链活证;/etc/motd(67B,含锚行
"fd=channel triple live")+/etc/version(5B)经 ramfs 服务器读出
全文+字节数自证。fork 继承面由 [78] forkdemo 覆盖(子承 stdio,
退出逐 fd CLOSE,父的 stdio 幸存——冒烟补跑 fddemo 验证)。

## 3. 冒烟战况

- **首跑哑火**:ps 表里没有 ttysrv/ramfs/pm,也无 spawn 失败
  行——真身是查存量 warning 时 `git stash` 后 `make` 重建了
  **参考实现二进制**,pop 回源码却忘了重建:对着幽灵二进制查了
  两轮。教训:**任何 stash/checkout 类环境操作后,重建再判**。
  (此雷非代码雷,是流程雷;ps 直证法两分钟定谳。)
- **ocb 池贴上限**:SRV_OCB_MAX=4 时 stdio(3)+motd(1)恰好顶
  满,version 并开即 -12。预排雷放宽到 8(冒烟前算账发现,
  未等到炸)。
- **冒烟自身的锚错**:fork 判据字面写成 "parent got exit=42",
  实际输出 "reaped pid=2 code=42"——测试者的字面记忆错,非
  被测面(站 30 smoke-arg 同款,两站连犯,记档)。
- 全链一次通:开机链(pm bind 行)/fdwrite 四脸/fork 继承
  +exit 关闭/stdio 幸存,全锚命中。

## 4. 边界与不做(v0 诚实)

无 ELF 装载(用户态 server 二进制=站 33 musl;v0 服务器=内核
任务直调 IPC 层);无 seek/目录列举/权限;stdin 经 fd 恒 EOF;
READ 窗 56B/次(客户端循环);PM 协商位不做;blk FS 观察哨记档
(rtos 已探 virtio-blk,blk-backed FS=后站);tty 服务器对脉冲
v0 丢弃(记档)。

## 5. 站利

- 三段消息的**第一个真实用户**:fd 层零新等待逻辑,阻塞/超时
  /优先级传播全复用站 30 机器,服务器体=纯 recv 循环+switch。
- cpio newc 是**真格式**(构建期打包工具+内核解析器同格),
  非"自制归档"——gen_ramfs.py 444 字节、解析器 80 行,格式
  知识本身入账。
- fd 表+POCB+conn 三层引用镜像(fork/exit 对称),是把
  参考实现 "fdtable 与 同步队列件 的会合点"翻成代码的一天。
