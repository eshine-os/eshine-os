1/ 调试 OS 十四站的老朋友今天归案:boot 20-75% 随机挂,三副面孔(静默楔/panic 指着池数据执行/8.4MB 精确 128B 周期的空格洪水),外加 REPL 1/4 活率。专场取证,全案记档。🧵

2/ 第一步永远是修镜子:QEMU gdbstub 的 VA 读走当前 CPU 翻译体制,EL2 语境下客户内存全读空。钥匙三把:monitor xp 物理读 / monitor 输出在 stderr(解析脚本三度全灭才抓到)/ 每靴端口轮换(TIME_WAIT 竞态制造过整轮假数据)。

3/ 活体解剖三例:panic 靴 ELR=0x40188f50——查符号,无符号,因为那是任务池第 5 槽的数据地址;静默楔主核停 guest_park,rt_ticks 冻在 8(80ms 就死了);洪水靴死在 hello 首个 printf 的 "pid=" 处。

4/ 等标本靠运气,埋守卫靠纪律:tick 全池体检四谓词(sp 栈域/ksp 恰等/READY 队列可达/队链全审计四类)+writev 巨 iov 绊线。案发 10ms 内带全证 panic:受害者/毒值/期望域/双核 cur/tick。

5/ 守卫开火,证据直白到不需要解释:queue poison: cpu 0 prio 31 wild 0x4015e460, ticks 1——0x4015e460 是 idle 的地址。每靴第一拍,idle 伪任务混进就绪队列 prio 31 层。2/2+冒烟 2/2 全复现,确定性的。

6/ 案发回放:IRQ 出口把被抢占的"当前任务"排回队尾——没认出此刻 rt_cur 是引导语境就地转正的 idle。idle 铁律不占池不入队,却顶着 RT_RUN 进了队列。

7/ 一个队列里的 idle 为什么能炸出三种死法?空转 pick 恒返 idle→调度器把它当真任务切进→eret 进陈旧引导帧;idle 的 next 字段与自家 FP 保存区重叠→队链写以它为笔野喷→rdy_tail 野化→新任务被链到 BSS 任意地址(rt_cur_arr=0x1、rt_ticks=代码地址、壳 sp=蹦床域)。

8/ 被毒的帧恢复:x30 槽=任务指针→ret 跳进池数据执行→PC 对齐错→按向量槽分道:SP0 槽=静默驻车,SP_ELx 槽=panic。主核死→vtimer 停→一切 sleep 永睡。三脸+REPL 挂,一条链。修真:rdy_enqueue 池域硬门。五靴队列毒全消,首次完整走通开机序列零开火。残留:fork 子孤儿脸(守卫已活捉×2,全审计下次开火即分类指认)。教训:仪器先于判据;守卫自己要过冒烟;A/B 是修真的牙医椅。
