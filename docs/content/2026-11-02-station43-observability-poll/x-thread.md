# X 8 推(站 43 观测面+ppoll)

1/8
eshine-os 站 43:上一站补完 27 个 syscall,审计账上"裁剪"的三面本站翻案兑现——calltrace、ps 归属列、ppoll。观测面是取证成本的对数器:一屏调用链 vs 三小时静默腐坏法医。 #kernel #observability

2/8
calltrace:x29 帧指针链走查。ARM64 约定 [x29]=caller x29,[x29+8]=返回地址——沿链走就是调用栈。16 帧保险丝+内核窗门(用户帧止步=域分离),挂 panic 钩子:崩溃自带调用链。 #ARM64 #debugging

3/8
ps 新列 blk:任务挂起时挂在哪个原语。逐点打标:sem/mut/rx/sleep/futex。彩蛋——站 30 的消息等待显示 sem,因为它本来就是拿通道信号量实现的。复用如实,不造 msg 假档。 #honesty

4/8
顺手抓自己的洞:futex 走 pend_q 阻塞,不经信号量=漏标,归属列会显示上一次的陈旧标签。观测面自己犯的错当场修。判据先于结论,先用在自己头上。 #dogfooding

5/8
ppoll(号面 73):Linux pollfd={int fd;short events;short revents}——events 在 +4。第一版我按"fd 是 long"的肌肉记忆解析高 16 位:掩码恒 0,POLLIN 永不命中。 #fail

6/8
最讽刺的取证:pipe 服务器全程如实回答"可读"(count=9,answer=1),客户端死不认。被调方无辜,调用方失聪——站 30 rcvid 反谓词同款老戏码第三演:先查调用方的解析。 #debugging

7/8
wire 层一个 RT_FDOP_POLL 消息,tty/ramfs/pipe 三服务器各答可读脸;超时=tick 轮询环,骑站 31 fd 三元组往返。零新等待原语——内核的体量来自机制,不来自特例。 #architecture

8/8
冒烟 6/6;门禁 123→126 写而不跑——批令在手:43-48 六站连开,48 站后统一验收两连。工程节奏从站站庆功切到战役收官。从零打造,第 43 站。 #eshineos #rtos #buildinpublic
