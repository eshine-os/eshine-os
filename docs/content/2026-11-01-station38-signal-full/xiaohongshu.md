# 站38:信号完整面 📶(22 站的谓词雷现形)

给 RTOS 补齐信号完整面:

✅ SA_RESTORER:投递改写 x30=restorer,handler ret
   自调 sigreturn——musl 真兼容
✅ sigqueue 携值+pid/tid 双定向(任务级位图先于进程面)
✅ 默认动作=终止(无 handler 的信号=128+sig 走 exit 链)
✅ sigtimedwait 轮询面(真号 137/138 零偏差)

## 22 站的谓词雷 ⚡

sigqdemo 首跑必挂:fork 子上床打印后 2s 自然睡永不醒,
心跳照跳=vtimer 活。一轮定谳:四条唤醒走查的过滤器是
`t->entry != NULL`——站16 帧自铸时代的活判据。站29 起
fork/clone 子恒 entry==NULL(帧改写形),被永久排除。
**22 站没人走"自然睡"这条路,雷就不响;本站第一个走
新路的顾客到达,当天定谳。**

修=活判据改状态面(TASK_LIVE)。
谓词的时代错位,一直在等第一个走新路的人。

## 流程雷清算日 🧹

pkill -f 自匹配自杀×3(老坑再犯)/遗留 find / 烧 25 分钟
(pt 楔的真身)/自伤半径内构建产物失踪。教训:杀进程按
PID 点名;谈机制先查负载。

eshine-os · clean-room ARM64 hypervisor+RTOS
Apache-2.0 · 100 门 ×2 双绿 · 站 38/41
#嵌入式 #RTOS #内核开发 #操作系统 #程序员日常
