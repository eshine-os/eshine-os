# X 8 推(站37 posix_queue)

1/8
eshine-os 站37:POSIX 队列族日。mqueue(命名优先级队列,高优先先出同优 FIFO)+pipe(字节流双端,骑站31 resmgr 零新 fd 线:pipesrv=第三个内核服务器任务,fd 三元组走既有 wire ops)+长消息(>64B 自动走 1024B 长槽池)。 #embedded #kernel

2/8
mqueue 的阻塞=第17站就建好的 pend 机器第 N 笔复用——满挂发队/空挂取队,单窗 DAIF 形+成功出口缴械律。等待逻辑只建一次,每新一个同步对象都是红利兑现日。 #RTOS

3/8
本站最大的战果:fd 层的 rt_fd_write 从站31 起从不填 req.occ 那个字段——不对,是 req.ocb。为什么三个站没事?因为 tty/ramfs 的 WRITE 根本不读 ocb。一个没人读的契约字段,漏填可以绿很多年。 #lessons

4/8
第一个真读者(pipesrv 按端分发)上门的当天,雷爆了,也当天定谳:服务器侧探针抓到 WRITE ocb=0 实况,修一行。潜雷规律:契约字段的测试,唯一的真形式是找到第一个消费者;在那之前,字段只是碰巧没人看。 #debugging

5/8
MsgRead 镜像(参考实现味道最好的一小块):接收者收到消息之后,还能按偏移回头再读发送者的缓冲——延迟访问,不是急拷。1024B 长消息走内核长槽池,归还口只有"所有 used=0 位":生命周期跟着消息本尊走,三口收回一个不漏。 #design

6/8
POSIX 细节:mq_close 只降描述符面,队列本体活到 mq_unlink。我第一版把 close 写成销毁,冒烟用 open/close/unlink 三拍序当场打出来。语义边界,让判据序列去踩。 #testing

7/8
首验门红,两根因全在门自己头上:①源归档(gitignore 工件)站34 后没人重生,bitbake 恒吃参考实现——前几轮绿灯是旧产品混的;修=构建脚本自愈重生,门恒验当下提交树。②会话 attach 后立发命令撞 RX 竞态;补 \n 探针+配对锚。 #build

8/8
eshine-os:clean-room ARM64 Type-1 hypervisor+RTOS,Apache-2.0。站37/41:99 门 ×2 双绿(新 [92-94],旧顺移 [95-99])。下一站 signal 完整面,直开不停。 #opensource #hypervisor
