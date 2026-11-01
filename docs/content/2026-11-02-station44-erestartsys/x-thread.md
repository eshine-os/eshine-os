# X 8 推(站 44 ERESTARTSYS)

1/8
eshine-os 站 44:信号打断阻塞中的系统调用,老契约直接返 -EINTR——把"其实没事发生"的翻译成本转嫁给每个用户。Linux 的答案 ERESTARTSYS:内核自己吞下这句话。本站翻案兑现。 #kernel #signals

2/8
机制只有两笔帧手术:elr 减 4(ELR 本指 svc 下一条指令,减 4=重取 svc);x0 槽不写(正常路径是返回值槽,重启路径保入参=同参再入)。四行代码。 #ARM64

3/8
最漂亮的发现是机制咬合:信号投递(站 32)本来就是整帧 272B 存档+改写。重启手术赶在存档前落笔,handler 跑完 rt_sigreturn 恢复整帧,eret 自然落在回拨后的 elr。零新机器。 #architecture

4/8
帧手术的纪律是"动什么还什么";可重启的纪律是"假装什么都没动"。后者更难——272B 帧外用户可见态(SP_EL0 等)一样要清点。 #discipline

5/8
判据埋在 handler 里:handler 自己把 futex 字戳成 1,重启再入的入口值校验立刻 -EAGAIN。一判决两事:elr 没回拨=执行错窗垃圾;x0 没保住=拿 -512 当地址野访 panic。 #testing

6/8
判据自带反面雷:重启哪天破了,旧脸 -4 落 FAIL 行,门红自抓。好的判据连自己的失效方式都设计好了。 #regression

7/8
哪些不重启,与重启的一样重要:sigsuspend 恒返 -EINTR(POSIX 特例,Linux 同律);poll 家族 never-restart;sem/msg 打断被 take 环吸收重挂(站 32 既有契约),不生产重启。语义表完整,不是一刀切。 #honesty

8/8
冒烟 7/7 首试全绿,零战雷——站 32 帧手术完整性清单的红利兑现。门 [87] 原位改针,总数 126 不变。批令推进中:43-48 六站连开,48 站统一验收。从零打造,第 44 站。 #eshineos #buildinpublic
