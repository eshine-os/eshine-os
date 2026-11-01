# 站 42:syscall 补全 🧩(第一受害者定律新案)

参考实现终审计揪出站 27 清单漏掉的号面,一站补 27 个真号 ✅

✅ 调度管理组:setparam/setscheduler/affinity/yield/nice
✅ 信号补全:sigpending/sigsuspend/sigaltstack
✅ posix timer+itimer:sigevent 到账+overrun
✅ robust futex:持锁线程死,等者自动醒(判据活证)
✅ 进程组会话+close_on_exec+uname

## 🔥 第一受害者定律新案

getpid 返回的是任务池槽号+1,不是 pid。
壳 28 站全绿,只因任务槽 0 恰好等于进程 1。
第一个 fork 子调 getpid 的 demo,当天现形。

## 两种 child 两种纪律 🕵️

clone 线程恢复后落回父 C 帧=[sp+1304] 读出
字符串字面当指针,far 直接是 ASCII。
fork 子整栈拷贝无此约束,clone 必入自有帧入口。

门禁 116→123,两连绿 ✅
