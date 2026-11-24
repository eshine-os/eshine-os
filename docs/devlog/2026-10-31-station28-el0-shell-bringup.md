# 站二十八 devlog:EL0 用户态+syscall 骨架——十一定谳与一块挂账

日期:2026-10-31 · 作者:Julian Chu + Claude Code
状态:**已收口**(本档 = WIP 期十一雷的立档;收口六雷
[.Lel1 错库/无主第四类/帧复用漏站16 字段/跨 VM 串台/CurrentEL-EL0/
哨兵碰撞]与 81 门双绿见同日 devlog
2026-10-31-station28-vtimer-closure.md——§3 挂账由三个独立缺陷
担纲,全案终结)

## 0. 本站目标回顾

vm0 的壳从 EL1 内核任务升格为 EL0 用户任务:两区式 stage-1
(内核区/用户区分离)、EL0 异常入口蹦床、Linux arm64 标准 syscall
ABI(号面 x8/参 x0-x5/返 x0 负 errno)、壳整目标文件落 .user 段。
判据面:壳输出与站 27 末逐字节一致(80 门不改),新增 [81] utest。

## 1. 冒烟战报:十一个根因,全部 -d int / -d exec / gdb 三镜定谳

站 14 教训(裸机首验先 `-d int`)在本站反复应验。**判据先于结论,
仪器先于判据**——每个根因都有独立运行时证据,非纸面推演:

| # | 根因 | 证据 | 修法 |
|---|------|------|------|
| 1 | 页表几何:按三级链搭表,而 T0SZ=25 只容两级走、块描述符落末级=保留编码,MMU 开后首条取指即 Instruction Abort→guest_sync 静默停车 | vmrun 后串口全静;-d int 无自陷(被 park 吞) | 三表两级走:首查 1GB 表(l0),次查即 2MB 块(l1_dev/l1_ram);索引算术随注修正 |
| 2 | AP 编码:AP[2:1]=0b11 是**全级只读**而非"EL1-only RW"(Linux `PTE_RDONLY=PTE_AP2` 同律),内核块变只读,取指活/栈写即爆 | -d int:DFSC=14 permission fault L1,ELR=guest_sync 首条 stp,FAR=引导栈 | MM0_AP_EL1=0(00=EL1-only RW),注补 AP 真值表 |
| 3 | `msr sp_el1` 在 EL1 落 UNDEF(banked SP 直访仅 EL2+ 合法;QEMU cortex-a57 实测) | -d int:EC=0/IL=1,ELR=msr 指令 | vec.S 蹦床复位改 spsel 换库习语;rt_main 初始化舞蹈整个删除(不变量移交 eret→EL0 边界) |
| 4 | spsel 舞蹈方向反了:RTOS 全帧契约实为 **EL1h**(psr=5/0x3C5 三处铁证),我把 SP_EL1 里的引导栈覆写成蹦床址,sp=0 压栈环绕 address-size fault→slot 0x000 park 永眠 | FAR=0xffffffffffffffe0,ELR=rt_puts 首条 prologue | .Lpop/RT_EL0_RET 承担双库不变量:eret→EL0 前 SP_EL1:=蹦床顶;回 EL1h 前 SP_EL1:=task->sp |
| 5 | .Lpop 先置 usp 再借 sp 弹栈,SP_EL0 寄存器被帧顶覆写,壳以内核栈入场,首压栈即 EL0 越权→诚实终态面 | EL0 fault elr=rt_shell_task+0,far=rt_kstacks 区 | usp/蹦床值藏进已消费的 elr/psr 槽,弹完 31 寄存器(无 scratch)再取回归库 |
| 6 | 汇编器立即偏移超程(ldr [sp,#-272] 超 imm9 上限 -256);**且 zh locale 的"错误"+大写 Error 双重逃逸我的 grep 构建判据**——rtos.bin 未重生、QEMU loader 打不开直接退出,险些误判为"宿主裸靴坏" | make 明文报错;裸 eshine.elf 单测健康 | sub sp+零偏移取回;**构建判据改 make 退出码,grep 只作参考** |
| 7 | 宿主泵陈疾**"停泊双重推进"**(resume 落 wfi+8 而非 +4;站 24 [41] 家族,曾致 far=0x9 panic)+8 直落重排队路径消费陈旧 caller-saved 寄存器 | 零噪声探针实锤:park=0x40000df8 resume=+8 | 客人侧落点加固:全部 wfi 后双 nop 垫(rt_sched/panic_park/guest_park),+4/+8 皆滑回循环顶;宿主根治留专站 |
| 8 | **banked SP 直访同律第三例**:`msr/mrs sp_el0` 在 EL1 亦 UNDEF(0x9200004e 家族,ELR=RT_EL0_RET 尾) | -d int EC=0/IL=1 于 msr sp_el0 | 三处改经 MOV SP 当前库(spsel 换库) |
| 9 | **FR 帧槽步长错**:RT_PUSH_GPRS 紧打包(stp 双寄存器 16B/对,仅 x0 槽带 8B hole——站 16 注释自证),FR(i) 按 16B 步长读,FR(8) 落 x15 槽→壳全部 syscall 打 sys_ni 返 -ENOSYS→**静默提示符+每秒 5.9M 次 ENOSYS SVC 风暴** | 风暴 TB 级分解:SVC ELR=0x40200304=x8=63 的 read 重试环;EFX 探针零命中自证非 access_ok | FR(i)=字节偏移:i==0→+16;i>=1→24+8i;vec.S 蹦床回填偏移同步修(96/104/112) |
| 10 | **syscall wrapper clobber 缺口**:内联 asm 只声明 x0/x1/x8,内核分发链合法踩遍 x0-x18,GCC 把壳活值(idx)寄放 w16 横跨 svc 假存活→strb 落内核区 EL0 权限故障(确定性) | far=蹦床区内的 [x10+w16] 野值 | wrapper 补 x3-x18 全 clobber 列表 |
| 11 | **RT_EL0_RET 弹错帧**:阻塞唤醒路径以 SPSel=1 抵(coop 帧 eret EL1h),蹦床舞后 sp 落回 SP_EL0 库的陈旧 coop 帧顶,31 GPR 弹出 C 语境值(x20=0)→strb 写 0 | far=0x0,ELR=strb wzr,[x20,w26](寄存器重排后 w26/x20 皆 callee-saved,自证非 #10 复发) | 入宏先 mov x2,sp 抓 GPR 块址(库无关),舞后显式归还;同任务路幂等 |
| 12 | sys_demo/sys_sysinfo/sys_hvc 从 x1 读首参,而壳把首参映射 x0→全命令静默 EINVAL | selftest 100 点阵消失但提示符存活 | 统一 ABI 序:首参=FR(0) |

(附)EL0 诚实终态面(rt_el0_fault:ESR/ELR/FAR 裸打+panic)在 #5/#10
两案中均按设计行刑并全息自报——**判据面先于功能面建成,是本站能
在 11 雷里活着走完的首功**。

## 2. 现在的真实状态

- 靴全链字节级稳定:[1-18] 与站 27 末完全一致(横幅/GIC/FDT/
  virtio/身份探针/rtos up/cpu1 online)。
- **EL0 壳交互面活了**:[19-23] 连续多轮绿(RX 唤醒链、selftest
  100 MMIO、ps 活表、spawn 生灭、fp 双精自证)。semdemo/mutexdemo
  在正确时序下全锚通过(独立复跑实测)。
- 门 [24](sleep 3s 醒转)起**间歇游走失败**:睡眠心跳打到
  tick=250±后 tick 停走,"slept" 永不回归;mutexdemo 曾一轮
  idle-PC 野值(0x345)panic。故障点随轮次漂移=竞态而非确定缺陷。

## 3. 挂账:站 24 [41]/[69] vtimer 陈疾是本站双绿的拦路石

tick 计步实测 ~7Hz(313 Virtual IRQ / 45s)对设计 100Hz;
失败签名与站 24 服役记录逐字吻合——**"回复在而 tick 冻结"**
(用户回复已到、tick 停摆,唤醒源只剩后继 IRQ)。该缺陷当时已
定谳并"路由专站",站 25-27 未触发、站 28 的 EL0 壳(每命令一次
阻塞-唤醒往返)首次将其撞到判据面上。

本站客人侧已做的一切(蹦床双库不变量、落点加固、帧契约修正)
是**必要非充分**:泵侧 vtimer ctx 面(进场装回/出场保存/27 分支
介入)的根治属于其专站范围。在它落地前,门 [24+] 的间歇失败
预期持续,双绿不可承诺。

## 4. 工艺沉淀(跨站复用)

1. **三镜法则**:-d int(异常流)/-d exec(TB 执行流,可分解热转
   环)/gdb(断点现场)交叉定谳;串口静默≠死亡,tx 缓冲/分块滞后
   会制造假尸,逃生键强制冲刷是揭穿手段。
2. **banked SP 三禁**:EL1 下 msr/mrs sp_el0/sp_el1 皆 UNDEF,
   唯一习语=spsel 换库+MOV SP;任何"直写省事"的诱惑都在 QEMU
   cortex-a57 上以 EC=0/IL=1 处决。
3. **帧槽以字节偏移立契约**:紧打包布局(仅 x0 带 hole)必须与
   每个访帧宏互证;静态断言防回归待站 29 迁入 rtos.h。
4. **内联 syscall wrapper 必须声明全 caller-saved clobber**——
   ABI 契约缺口在 GCC 寄存器分配面前零宽容。
5. **构建判据=make 退出码**;grep 文本过滤受 locale 与大小写双重
   蒙蔽(本案实锤:中文"错误"逃逸 error 匹配,险些把"没编出来"
   误诊为"宿主裸靴坏")。
6. **海森伯预算**:取证仪器(-d int/-d exec/探针打印)都会挪动
   竞态窗口;每轮取证后须以"无仪器复跑"核对结论是否仍立。

## 5. 下一步(推荐序)

1. **vtimer 专站**(站 24 路由的陈疾):泵侧 ctx 面根治,tick 计步
   判据入 verify;本站 [24+] 预期随之收敛。
2. 站 28 收口:vtimer 落地后跑全 80 门双绿,加 [81] utest 门
   (重编号 [77]→[78]→[79]→[80]→[81]),三处同步(verify/壳
   help/ARCHITECTURE),内容包 5 文件。
3. 站 29(process+AS)接管:per-process 页表/ASID/缺页,rt_el0_fault
   诚实面退役为缺页入口。
