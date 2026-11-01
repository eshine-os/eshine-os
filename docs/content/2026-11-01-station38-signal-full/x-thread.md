# X 8 推(站38 signal full)

1/8
eshine-os 站38:信号完整面。SA_RESTORER 真兑现(投递改写 x30=restorer,handler ret 自调 sigreturn,musl 真兼容)+sigqueue 携值 pid/tid 双定向+默认动作终止+sigtimedwait。真号零偏差:137/138。 #embedded #kernel

2/8
本站最好的故事:一颗从站16潜伏至今的谓词雷。sigqdemo 首跑,子进程自然睡 2s 永不醒,心跳照跳=vtimer 活。一轮定谳:四条唤醒走查的过滤器是 t->entry!=NULL——帧自铸时代的活判据。 #RTOS

3/8
站29 起 fork/clone 子恒 entry==NULL(帧改写形),被所有走查永久排除。22 个站没有任何 fork 子走过自然睡这条路,雷就不响。本站第一个走新路的顾客到达,当天定谳,修=活判据改状态面。谓词的时代错位,一直在等第一个走新路的人。 #debugging

4/8
SA_RESTORER 两立设计:新契约(投递写 x30=restorer,handler ret 落其内自调 sigreturn)与站32 旧契约(handler 末尾显式调)并存——新旧 demo 零迁移成本,一个门里同测。 #design

5/8
默认动作=终局:无 handler 的信号以 128+sig 走 exit 链(fd 清+线程群扫+进程退+收尸唤醒)。POSIX 面兑现,sigqdemo 的判据:子收无 handler 的 12 → wait4 收尸 code=140。 #POSIX

6/8
流程雷清算日,记三笔:pkill -f 自匹配把自己的 shell 连环杀(老坑再犯);遗留 find / 烧 25 分钟把 pt 楔送上门(宿主污染窗家族第三现,站36 失速档同理重判);自伤半径内构建产物全失踪,3 分钟全重建复原。 #lessons

7/8
工具级教训沉淀:杀进程永远按 PID 精确点名,不玩模式匹配;谈机制前先查负载。站25 定的老定律,价值在违反之后认账的时候。 #devops

8/8
eshine-os:clean-room ARM64 Type-1 hypervisor+RTOS,Apache-2.0。站38/41:100 门 ×2 双绿(新 [95] sigqdemo,旧顺移 [96-100])。下一站 FS 深面+VFS+blk,直开不停。 #opensource #hypervisor
