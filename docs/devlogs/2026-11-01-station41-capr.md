# 站 41 devlog:CAPR 能力框架——能力与边界(既定序列终站)

日期:2026-11-01 · 作者:Julian Chu + Claude Code
spec:docs/specs/2026-11-01-station41-capr.md
门禁:111→116(新 [107-111];旧 [107-111] 顺移 [112-116])

## 0. 一句话

进程从"全权裸奔"到"能力上岗":四位能力位图(VFS/SPAWN/KILL/
MM)create=零能力、壳=CAP_ALL、fork 按位继承、**无任何加能力
路径**(self-inc-out 由构造保证);fail-closed 执法点=choke point
(fd 六门+四系统调用族,缺能力即拒);rlimit 自降单向(fd 满额
-EMFILE/子数满额 -EAGAIN);uuid 创建随机身份(fork=新 uuid,
资源承继身份不承)。参考实现五面(表/fail-closed/self-inc-out/
rlimit/uuid)的最小诚实站内形。

## 1. 面

- **cap 表(粗粒度位图 v0)**:rt_proc 增 `u64 caps` 四位
  (CAP_VFS 0x1/CAP_SPAWN 0x2/CAP_KILL 0x4/CAP_MM 0x8)。
  生命域:rt_proc_create 种 0(fail-closed 由此起);壳=
  rt_proc_boot_shell 后置 CAP_ALL;fork=rt_proc_fork_as 按位
  继承。**无加能力路径=self-inc-out 不需要执法,由构造保证**
  ——唯一的 mutation 是 cap_drop 580 的 `caps &= ~mask`。
- **fail-closed 执法点**:fd 六门(rt_fd_open/create/unlink/
  dir/getdents64/fstatat 判 CAP_VFS);sys_fork/sys_clone 判
  CAP_SPAWN;sys_kill/sys_sigqueueinfo 判 CAP_KILL;
  sys_mmap/shm_create/mq_open/pipe 判 CAP_MM。
  **choke point 非全系统调用铠甲**(v0 边界记档):read/write
  已开 fd=已授权窗,时间/随机/日志面不设卡。
- **rlimit**:max_fd 默认 RT_FD_MAX/max_child 默认
  RT_PROC_MAX-1,fork 继承;fd_used_count 满额拒 -24
  (ES_EMFILE),rt_proc_child_count 满额拒 -EAGAIN;
  rlimit_self 583 只降不升(0=不动)。
- **uuid**:create/fork 各取站 40 混出器 8B(rng_fill 首用
  自种子:uuid 走 create 期,早于 rngsrv 起立——rng_state
  零态在此窗首现,站 40 自种子修正即为此),零值 pid 换挡
  (0xA41 尾);判据=两子互异非零+壳自查非零。
- **号面**:cap_drop 580/cap_get 581/uuid_get 582/
  rlimit_self 583(ES_EPERM 1/ES_EMFILE 24 复用真 errno 面)。

## 2. 判据([107-111],111→116)

- [107] capdemo:子先开 /etc 目录窗(能力未削),再削 CAP_VFS
  ——fdopen -EPERM+**已开 dir fd 上 getdents64 同拒**(操作级
  执法非只开门,spec §2 面)+削尽 cap_get==0(自削单向)。
- [108] capdemo2:削 CAP_SPAWN→fork -EPERM([78] forkdemo=
  未削对照)。
- [109] rlimitdemo:max_fd 自降 3→第 4 open -EMFILE。
- [110] uiddemo:两子 uuid 互异非零+壳非零。
- [111] fail-closed 回归:壳 CAP_ALL 全脸照常——整个电池=
  隐式回归,本门显式打印自证。
- 三处同步:verify 116 步+shell help 走查(116 步头部修正
  ——**站 35-40 六站漏更的 93 步陈账一并清**,见 §4)+ARCH
  ITECTURE 站 41 节。

## 3. 战果:吞行自雷+三小时法医

本站最大战果不在 CAPR 面(机制直白),在**一次 Edit 吞行引发
的全系统死锁与它的完整法医弧**。

### 3.1 病灶(一行)

插 CAP_SPAWN/child_count 两道检查时,Edit 把
`rt_proc_create(NULL, &cp)` **整行吞掉**(替换而非追加):
`cp` 成未初始化野指针。sys_fork→rt_proc_fork_as(parent,
野指针)→fork_as 首行 `child->mmap_next = parent->mmap_next`
=对野地址狂写→EL1 数据中止→**静默驻车(原 guest_park 语义,
wfi 永环)**=全系统死锁,零故障行。开机 inittab sysinit spawn
initmark 首个 fork 即中弹;壳保持 state=RUN(不是 BLOCK!)、
CPU 停 wfi、所有服务器安睡在 recv,无一醒。

### 3.2 三重伪象(法医弯路账)

- **尸检栈读错**:shell 内核栈上 sem_take_to+360/sys_write+60
  以为是活链——实为早前 sys_puts 落控制台 tx 的陈栈(272B 帧
  是寄存器保存区不是 C 帧,活链只有 dispatch→sys_fork+
  fork_as 内联帧无返回地址)。
- **watchpoint 双零命中**:state 与 rt_cur_arr[0] 双硬件
  watchpoint 零触发=从未阻塞也从未被切走——推翻"调度器
  丢任务"假设,锁定"任务自己跑进了驻车"。
- **断点后零停点**:fd_fork/msg/sem/child_create/wait4 全
  陷阱集 190s 零命中=wedge 在 sys_fork 前 88 字节内,且
  -O2 内联让"没停点"看着像"没执行"。
- 终局一击:`git diff` 照面——`-rt_proc_create(NULL, &cp)`
  赫然在减号侧。**判据先于结论:diff 是最后才打开的,应当
  是最先打开的**(改动面仅 7 文件 +297,一屏可尽)。

### 3.3 教训(带机器背书)

- **-Wall -Wextra 本就在构建里,吞行版构建当轮必已打
  "cp 未初始化"警告**——警告在,没人读。freestanding 无
  -Werror,构建输出必须过眼;后续弧议:内核构建面加
  -Werror 或至少 CI 警告计数=0 面(记档,本站不动构建面)。
- **静默驻车=零可观测性陷阱**:EL1 未知异常停车无故障行,
  把"一行数据中止"伪装成"调度器 bug"三小时。后弧议:
  EL1 abort 终态面补一行打印(fault 面已有先例,EL1 面
  补齐=后弧)。
- **判据教训重犯**:fddemo 回归针线不看实输出(等 "fd: ",
  实打 "fddemo: via fd tty"+motd 67 bytes)——站 40 总
  结里的教训当天重犯一次。新 demo 针线永远从 transcript
  抄,不从记忆写。
- 流程雷一颗(pkill -f 自匹配自杀,记忆规则在场仍踩——
  清进程永远按 PID,kill $pid)。

## 4. 陈账清理

- **站 40 提交完整性洞**:e3edddc 漏收六文件——busybox 构建
  配方(其头部政策自宣"配方与判据入仓",配方自己没入仓!)
  +ramfs-beat.c/ramfs-initmark.c 源+beat/initmark 产物
  +etc/inittab。fresh clone 的 gen_ramfs 会打出无 inittab
  的 ramfs([105] 必挂);busybox 二进制仓外是**故意**
  (GPL 隔离,站 15 vm2 先例,.gitignore 单列一行)非洞。
  **git archive 纪律站 34 立、站 40 没用**(验证对着工作区
  绿,不是对着提交树绿)。本站六文件补收。
- shell help 走查头部 "93 步"=站 34 后六站漏更(走查行到
  [111] 而头部停在 93)——本站三处同步一并修到 116 步。
  **同步纪律的"步数"字面也要对账,不只是新增行**。
- DBG41 临时路标 11 处全拔(rt_main 4+rt_shell 7),成品
  strings 无残留。

## 5. 边界(v0 记档)

粗粒度位图(非句柄式对象表);读写不分档(已开窗即全权);
时间/随机/日志面不设卡;无 cap 转让(仅自削);rlimit 两项
非全集;uuid 非密码级(站 40 同律);真世界 MAC/RBAC/LSM=
后弧。41=站序定稿的终站(参考实现差距清单既定序列 28→41 齐收)。

## 6. 收口

116 门×2 双绿(r1/r2 连续 RESULT PASS);三处同步同 commit;
内容包 5 文件同批;镜像机 同步;愿景记忆站 41 收口+站序全图。
