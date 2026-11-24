# 站 40 devlog:服务收编+busybox

日期:2026-11-01 · 作者:Julian Chu + Claude Code
spec:docs/specs/2026-11-01-station40-services-busybox.md
门禁:103→111(新 [99-106];旧 [99-103] 顺移 [107-111])

## 0. 一句话

vm0 用户态从"musl hello"长成"真 busybox":同一 264KB musl
精选集字节在 vm0(我们内核+用户态 loader)与 vm2(真 Linux)
两侧跑出同一 md5——同工具同输入同输出,一台 hypervisor,两个
OS。服务收编三新服务器(random/procfs/slogd)+时间真号
(clock 族)+fd 层真号族(openat/close/getdents64/newfstatat/
dup3/fcntl/uid 族/brk/getrandom)。

## 1. 面

- **busybox 精选集**:allnoconfig+35 applet 表(echo/cat/ls/
  date/uname/md5sum/sh+FEATURE_EDITING/mount/umount 等)。
  **defconfig 1.3MB 塞不下**——rtos 内核像区=两区式 stage-1
  块 0(0x40000000..0x40200000)=2MB 硬界,ramfs+内核+bss 合账
  挤爆(首版 ld 直接 overlap 报警);精选集 264KB 落码,窗内
  余量回 940KB。**applet 表 qemu-user -strace 实测定谳**(零
  臆测):echo 面=set_tid_address/newfstatat(getuid/setgid/
  setuid/clock_gettime/brk/ioctl ENOTTY/writev;ls 面=
  openat(O_DIRECTORY)/fcntl(F_SETFD)/getdents64/fstat 每名。
  构建面两坑:①musl specs 自带 -nostdinc(默认 sysroot 整个被
  摘,busybox 的 <linux/*.h> 失供)——外科修=uapi 三目录
  (linux/asm/asm-generic+后续补齐 mtd/scsi)独立 -isystem,
  musl 头恒先;②LDFLAGS_EXTRA 不是最终链接变量→EXTRA_LDFLAGS
  (-Wl,-Ttext-segment=0x40C00000:noseparate-code 下头入段,
  AT_PHDR 律站 33 同源;-Ttext 会把头拆成 0x400000 微段=窗外面,
  实测撞过)。
- **fd 层真号**:getdents64=每调一名(POSIX 合法面),fd 私域
  dircur 游标,dirent64 内核零填(NUL 收尾+尾垫干净=busybox
  消费面);newfstatat=STAT wire op(mode<<32|size,ls 分类面;
  musl aarch64 stat 144B 零填+定点写,inst 头文件实测定谳);
  brk=窗内游标堆(mallocng brk(NULL) 探测面,exec 后种子=
  mmap_next);read/write fd≥3 路由 fd 机器(fd 0=控制台,
  站 28 契约不动——**雷见 §3.4**)。
- **目录前缀柄**:名空间归服务器(参考实现律)——直名未挂者
  ("/etc")回退路由 ramfs 元名,服务器侧前缀解析:目录柄
  0x100|idx 与文件 ocb 分隔,READ 拒 EISDIR,READDIR 两脸
  (全局全路径=站 39 lsdemo 契约 / 前缀柄=basename 枚举+首现
  去重 O(n²)+深径判 DIR)。busybox `ls -1 /etc` 直落。
- **三新服务器**:rngsrv(xorshift64* 混 cntvct/ticks,fd READ
  与 getrandom 278 两脸同源;熵源非密码级 v0 记档)/procsrv
  (/proc:任务表文本行+READDIR 第二消费者,站 38 TASK_LIVE
  状态谓据)/slogsrv(环 16×48B,WRITE=首字节级别,READ=倾倒,
  warn+ 控制台回显单行)。
- **时间真号**:clock_gettime 113/getres 114/gettimeofday
  169;monotonic=cntvct/freq,realtime=BOOT_EPOCH 常量+mono
  (2026-11-01T00:00:00Z;RTC/drvfw=后弧)。busybox date -u
  打出 "Sun Nov 1 00:00:05 UTC 2026"=判据面。
- **loader 泛化**:`run <path> [args...]`——argc/argv 变长
  启动块;页包含映射(busybox RW 段 vaddr 非页对齐,Linux 同律:
  映射含段页);栈预触(段预触同律,32 页)。
- **init+inittab 形态**(壳代处理器,真 PID1 init=后弧):
  /etc/inittab 四 action(sysinit/wait/once/respawn),sysinit=
  /bin/initmark 落 /tmp/init40 标记(busybox cat 判据);respawn=
  **门拍制**(见 §3.6);wait4 补 WNOHANG 位。
- **双世界判据**[106]:vm0 `run /bin/busybox md5sum /bin/busybox`
  与 vm2 `md5sum /bin/busybox` 同哈希(264KB 经 64B 窗=4125
  消息往返 ≈2s 实测)+ /etc/motd 同摘要。build-initramfs.sh 改
  优先落 vm0 同字节 musl busybox+共用 motd。

## 2. 容量账(站 39 律第三课,本站两次交学费)

- **栈 hint 窗口账**:UX_STACK_HINT 为 busybox 上移 1.5MB
  →mmap 游标被顶到窗尾→musl pthread 的线程栈族(4×128KB+
  guard)塞不进剩 384KB→pt 第 3 线程 EAGAIN(报错在 pthread
  不是 mmap,musl 把 mmap 失败翻成 EAGAIN——**号面错位的最
  佳教材**)。修=hint 回 1MB 位(busybox 264KB 图像 ≤
  0x40C52000 不撞)。**游标恒越 hint 段尾的 rt_as 契约下,hint
  就是全局水位线:挪 hint=挪全队伏击位**。
- POOL 16→20(11 常驻+demo 峰)/CH 8→16/CONN 16→24/NAME
  16→32,账注 rtos.h。busybox/beat/initmark 每件挂名=+7 名。

## 3. 雷账(七案连破,两枚站 29/16 遗产)

1. **hvc_yield 误用=宿主暂停面**(冒烟即抓):init_inittab 等
   ramfs 起立的重试环里用 sys_hvc(1) 当"调度让位"——HVC
   yield=**VM 级暂停**("vm0: yielded -> PAUSED"),VM 首轮
   即被宿主整体冻结,boot 静默。EL0 等待=futex 超时睡拍(站
   32 单窗阻塞,宿主零介入)。**同一 primitive 在"壳命令"
   语境(yield demo)与"boot 等待"语境是两个物种**。
2. **ASID 从不还位=站 29 注释谎言**(verify 连环 EAGAIN 定
   谳):rt_proc_exit 注释写"页/L2/ASID 已尽释",代码只还
   页和 L2——**11 站无雷只因电池从未超 15 次进程创建**;
   respawn 每命令一拍=首个高频循环者,第 16 次 fork 即 EAGAIN
   连环。修=exit 还位(exit 已全 TLB 冲,复用无旧标签别名)。
   站 38 律同型:回收缺口的真判据=第一个高频循环者。
3. **read/write 的 fd 不看=控制台被文件读吞**(cat 楔死定
   谳):sys_read(63) 自站 28 起是纯控制台面(fd 参数不读),
   busybox cat 的 read(3, buf, 8192) 把控制台当文件读——
   后续命令全被吞。修=fd≥3 路由 fd 机器(fd 0..2 控制台契
   约不动)。
4. **inittab 读环哨兵早断**:`if (n >= sizeof(buf)-64) break`
   在 128B 处切断 150B 文件——initmark 截成 "initmar"→
   open failed→respawn 行全失。修=窗满哨兵只防溢,EOF 为正途。
5. **偏移寄存器跨 EL0 缺页=11 站潜伏保存洞第一受害**(
   build 敏感,多轮全息画像):ux_exec 的 argc 变长启动块把
   偏移放 x8-x27 跨打包环,环内首触栈页缺页后寄存器被毒
   (far=0x40D80000+.user 级野值;argc 探针证明入口 sane)。
   站 33 的块=常量偏移(免疫),**argc 泛化=首个持运行时偏移
   寄存器跨缺页的代码**。修=块写入抽 noinline 辅函数(调用
   边界=帧重载,与"加 print 就好"的假象同效但为正身代码)
   +栈预触消窗。**内核侧 EL0 缺页保存面=挂账(记 §5)。**
6. **respawn 自主连拍=竞态放大器**(两轮 verify+孤立二分定
   谳):init_poll 每命令边界重拉 beat,其加载窗与任意门窗
   共窗——mutex/PI 遗留态共窗时壳静默楔(孤立复刻一次现、
   一次不现=窄窗竞态)。**门拍制收口**:init_poll 只收尸,
   `initrespawn` 命令=确定性单拍(sync 等死即拍即收),[105]
   门锚它。自主连拍=后弧(§5)。
7. **el0 域复用残值=站 38 律第三现**([41] deepdiver 定谳):
   el0 域站 28 入槽时漏复位清单——boot 期 initmark/beat 子
   (首批早期 el0 亡者)残值 1 被 deepdiver 复用,
   rt_cur_stack_limit 判 el0 返 NULL=受控深潜静默不潜,
   水位门红(栈只触 320B,水位 3776 全场同貌)。修=create
   清单补 t->el0 = 0。**站 40 的 EL0 子高频化=又一次"第一
   个走新路的人"炸出潜伏缺口**——同站内还炸了 ASID(案
   2),两案同源:高频循环者=回收路径的真判据。
8. **阻塞 wait4 无 ECHILD 路**(七轮 verify 全息画像):站
   29 v0 子集"指定子+无 options"的阻塞循环从无"非子即返"
   分支——sync respawn 首撞:wait4 已收 pid=永挂(壳在
   respawn 等待里死,WNOHANG 路有 ECHILD、阻塞路没有)。
   修=阻塞环补 child_alive 判(POSIX 正身)。
9. **musl 精选集缺 sh/mount=vm2 PID1 即亡**([73] 实抓):
   vm2 /init 的 `exec /bin/sh` 失败→Linux 杀 init。applet
   表=构建期判据面的一部分——**双世界判据的右翼依赖表也要
   当需求管理**(我在 strace 定 vm0 需求时忘了 vm2 的 /init
   也是同一二进制的消费者)。另:busybox `mount --help` 报
   applet not found 而 `busybox mount` 正常——杂音记档勿判。

## 4. 判据

[99] rngdemo(16B×2 互异+getrandom)/[100] clockdemo(mono
递增+wall=1793491xxx+res)/[101] procdemo(≥8 名+shell-seen)/
[102] slogdemo(3 行入环+warn 回显)/[103] busybox echo+cat
initmark 标记/[104] busybox ls(getdents64)+date -u/[105]
init 门拍(initrespawn:respawn 行+beat exit 42)/[106] 双世界
重整形:vm0 侧 264KB md5(电池最大单窗)撤——完整性职能由
[103][104] 整像可运行覆盖;"同字节"改由 **vm2 运行时 md5
/bin/busybox == 构建期常数(qemu-user 静测定谳 5b2fcc55...,
幂等重发 ×2 抗脏摘要瞬态)+motd 双翼同哈希**(跨 OS 同工具
同输入同摘要)证明。旧 [99-103]→[107-111]。三处同步:
verify 111 步+壳走查表([99-106] 段新增)+ARCHITECTURE 站 40
门走查段。收口=三轮连掷 r9+r10 两连 RESULT PASS(112 计=
[106] 幂等重发双打印,门数 111 不变)。

## 5. 挂账(诚实边界)

- **EL0 缺页保存洞**(§3.5):callee-saved 寄存器跨 EL0 缺页
  的保存面疑点,build 敏感+共窗相关——站 25 级取证弧(迹环
  见证位)候选,站 41 前置。
- **respawn 自主连拍竞态**(§3.6):beat 加载窗×mutex/PI 遗留
  态共窗=壳静默楔,窄窗竞态未根因——门拍制规避,自主连拍=
  后弧(与上条同取证弧)。
- **verify 散布族**(站 25/33 记档家族,本站高频复现):控制
  台字节窗损(输入"mu"/"pm"半吞、输出 handler 行消失、cpu1
  零行、QEMU EOF)+负载敏感——散布在不同门([19]/[36]/
  [88]/[95] 各轮异位),孤立复刻全过=非产品回归;**已到
  "常态拦路"档**(若常态拦路再议 ps 门装 ev9 见证位——站
  33 记档原句)。ev9 见证位=站 41 取证弧第一刀。
- sock/unix/drvfw/coredump/sysfs=后弧;熵源非密码级;
  realtime 无 RTC;getdents64 每调一名(慢=合批后弧)。

## 6. 战果(可讲面)

- **"第一受害者定律"三连**:ASID 不还(11 站潜伏)/el0 残值
  (12 站潜伏)/阻塞 wait4 无 ECHILD(站 29 起空缺)——三案
  都是"缺口一直在,等第一个高频循环者上门"。站 40 的
  busybox+respawn=把用户态从"每次一两发"变成"每命令一发"
  的循环机器,一次性炸出全部三家。**判据面越像真用户,遗产
  缺口越无处藏**。
- **"加 print 就好"的解构**:探针修好的构建=寄存器从帧重
  载的副作用,不是 print 本身——把"探针"翻译成正身代码=
  noinline 辅函数(调用边界同效),才叫修复而非遮盖。
- **窗口账=容量账**:栈 hint 上移一档,mUSL 线程栈族全队挪
  挪到窗外——hint 不是私有地址,是全局水位线。

**直开站 41(CAPR),不停。**
