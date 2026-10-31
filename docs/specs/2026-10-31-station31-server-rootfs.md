# 站三十一 spec:server+rootfs——fd=通道三元组与用户态服务的会合

日期:2026-10-31 · 作者:Julian Chu + Claude Code
基线:参考实现普查 spec §10(基础库/io 三层 QNX-resmgr 风/开机链/
fdtable+posix_queue 的 fd=通道三元组);红线:机制参考、零代码
拷贝、参考实现权属私藏。
前置:站 30 收口 8ddd44f(87 门双绿,三段消息在役)。

## 0. 目标

fd 不再是整数,是**通道三元组**:{coid, ocb}——每个 fd 都路由
到一个服务器的通道,read/write 经三段消息到达 resmgr 服务面
(参考实现/posix_queue 的"fdtable 与 同步队列件 的会合点"定谳,
QNX resmgr 模型)。rootfs=cpio(newc)嵌入式归档,ramfs 服务器
开机解析+逐路径挂名;开机链=固定序列(内核 spawn tty 服务器
→ramfs 服务器→PM;PM 绑 stdio 三元组后收场)——参考实现 "PM 固定
序列起步"形状,协商位=F3-18 僵化审计记档后议。

## 1. 机制映射(参考实现 → 新仓站内尺度)

| 参考实现(§10) | 站 31 新仓形态 |
|---|---|
| fd=通道三元组{channel,p_chid,node_type,node_priv,ocb_id} | rt_fd{coid,ocb} 两字段的站内收编:路由面=连接(→通道),状态面=ocb(每打开一份) |
| PNODE 服务端持久节点/POCB 每打开控制块(引用计数) | ramfs 服务器:PNODE=cpio 索引项(路径/偏移/尺寸,静态);POCB=ocb 槽{文件号,offset,used} |
| resmgr io 层(core/handler/libc iofunc) | v0 wire 面三 op:OPEN(path→ocb)/READ(ocb→数据)/WRITE(数据)/CLOSE(ocb→释)——三段消息载荷 64B 内定契约 |
| 开机链:内核 spawn PM→PM 拉服务序列→busybox init→rcS | 固定序列:内核 spawn tty→ramfs→PM(绑 stdio 后退场);PM 拉后续/协商位/init=busybox=站 33+ 后议 |
| ramfs 挂 "/" | v0 挂名:/etc/motd /etc/version 逐路径进站 30 命名表(name_attach) |
| cpio_load_pm+elf_load 建映射 | v0 静态链接内嵌(.incbin rtos/ramfs.cpio,tools/gen_ramfs.py 生成 newc);ELF 装载=站 33 musl 弧 |
| RTOS 侧接 virtio-blk;块 FS 观察哨 | rtos 已探("virtio blk on");blk-backed FS=后站,v0 观察哨记档 |

## 2. 结构

- **fd 表**:rt_proc 增 `struct rt_fd fds[RT_FD_MAX=8]`
  {used, coid, ocb};stdio 0/1/2 = PM 开机绑 /dev/tty;fork=
  **fdtable 组拷**(参考实现 COPY_UCONTEXT 同款)+每继承 coid 引用
  计数+1(六差距①的兑现面)。
- **wire 契约**(载荷 64B 内,实现终形):
  请求{op,a,ocb,rsv,data[44]}五 op:OPEN=data 路径(→立 ocb);
  WRITE=a 数据长+data 数据;READ=a 窗长+ocb(文件位置驻 ocb
  服务器侧推进=每打开态,resmgr 本义,无 wire off 域);CLOSE/
  DUP=ocb(引用减/加,fork 继承面)。回复走站 30 status 面
  (status 即 send 返回值),READ 数据走 rbuf 面(56B 窗,0=EOF)。
- **tty 服务器**(内核任务,通道 /dev/tty):WRITE→rt_puts_n
  落串口;OPEN→ocb=1(无每打开态);READ→v0 恒 0(EOF,stdin
  经原始 sys_read 面不变)。
- **ramfs 服务器**(内核任务,通道 /ram? 不:逐路径挂名):开机
  解析 newc(≤16 文件索引)→逐 name_attach(路径)→循环:
  OPEN(路径→ocb 槽)/READ(ocb+56B 窗,文件位置在 ocb 推进)/
  CLOSE(ocb 释放)。
- **PM**(内核任务,一次性):spawn 序 tty→ramfs→PM;PM 开
  /dev/tty ×3 绑 stdio,退场(任务返回即 DEAD)。
- **号面**(扩展段,Linux 号迁移=站 33 统一):fdopen 550/
  fdclose 551/fdread 552/fdwrite 553。

## 3. 判据面(门禁只增不减:87→88)

- **[84] fddemo**:fdwrite(1,…) 经 tty 服务器落串口("via fd
  tty" 锚=fd→通道→resmgr→串口全链);fdopen("/etc/motd")+
  循环 fdread 打出全文(内嵌锚行)+fdclose;再 open /etc/version
  读关。三门脸齐=open/read/write/close 走服务器。
- 旧 [84-87] 顺移 [85-88];壳走查表 88 步;ARCHITECTURE 同步;
  guest help 补 fddemo。

## 4. 边界与不做(v0 诚实)

无 ELF 装载(用户态 server 二进制=站 33 musl;v0 服务器=内核
任务直调 IPC 层——"用户态 server"的最后一跳是站 33 的题);
无 seek/目录列举/权限;READ 窗 56B/次(客户端循环);stdin 经 fd
不做;PM 协商位不做;blk FS 观察哨记档。

## 5. DoD

spec→实现→88 门×2 双绿→devlog→内容包 5→commit→镜像机→记忆。
