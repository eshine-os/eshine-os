/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#ifndef RTOS_H
#define RTOS_H

typedef unsigned long long u64;
typedef long long s64;
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

#ifndef NULL
#define NULL ((void *)0)
#endif

/* eshine-rtos:参考RTOS 公开设计为基准的最小任务内核(第十六站)。
 * 32 级优先级(0 最高,31 最低),位图找就绪,同级 tick 轮转,
 * 统一任务帧(elr 自描述续跑点,见 vec.S)。 */

#define RT_NPRIO     32u
#define RT_POOL_MAX  20u   /* 站40 容量账:常驻 8+3 服务器=11+run/respawn 子+demo 峰 */
#define RT_NCPU      2u               /* 站二十:vm0 双核(per-CPU 就绪队列) */
#define RT_SLICE     4u               /* 同级轮转量子(tick 计) */
#define RT_HZ        100u             /* vtimer tick 频率 */

#define RT_RX_BIT    0x0001u          /* BLOCK 等待位:串口 RX
                                       * (通用原语——信号量/互斥——留十七站) */
#define RT_SIG_BIT    0x0008u         /* 站42:sigsuspend 等待位——任何
                                       * kill/queueinfo/timer 投递都会
                                       * 清 wait_mask(sig_wake 律) */
#define RT_PROC_BIT  0x0004u          /* 站29:子进程收尸唤醒位
                                       * (sys_exit 置 ZOMBIE 后广播) */

#define RT_READY  0u
#define RT_RUN    1u
#define RT_SLEEP  2u
#define RT_BLOCK  3u
#define RT_DEAD   4u
#define RT_FREE   5u   /* 站35:素槽态(开机清池后显式标记;bss 零态
                        * =RT_READY 与"已建未跑的帧改写子"同貌,旧槽
                        * 判 entry==NULL 把活 fork/clone 子当空槽整
                        * struct 复用=pt leader 被 clone 蒸发,gdb 池
                        * 像定谳) */

struct rt_task {
    /* asm 契约(vec.S):sp 必须在偏移 0;统一帧顶指针(elr 槽) */
    u64 sp;
    void (*entry)(void *arg);
    void *arg;
    u32 prio;
    u32 state;
    u32 wait_mask;
    u64 wake_tick;
    u32 slice_left;
    struct rt_task *next;        /* 就绪 FIFO 尾链(非环,摘除简单) */
    struct rt_task *wq_next;     /* 同步对象等待队列链(第十七站) */
    char name[12];
    u32 base_prio;               /* 站十八B:PI 基准优先级(boost 还原用;
                                   * 置结构尾:前部偏移是 asm/帧契约不许动 */
    /* per-task FP 帧(站十八C):16B 对齐 q 阵列,偏移 96 = vec.S 的
     * .equ RT_TASK_Q asm 契约(下方 _Static_assert 钉死);帧即完备,
     * 切换全量存取,泵 per-VM FP 同哲学第三兑。idle 同结构同享。 */
    u64 q[64] __attribute__((aligned(16)));
    u64 fpcr;
    u64 fpsr;
    u32 cpu;                     /* 站二十:就绪队列亲和(0/1)。结构尾
                                   * 追加——前部偏移 asm/帧契约钉死,
                                   * q@96/fpcr@608/fpsr@616 断言不动 */
    /* 站二十一:超时挂等登记(结构尾追加,同上契约;扫描门
     * pend_deadline!=0——wait_mask/事件注册表型 BLOCK 一律免疫) */
    u64 pend_deadline;           /* 绝对截止一次算死;0=未武装 */
    struct rt_task **pend_qh;    /* 超时自摘回指针(对象等待队) */
    struct rt_task **pend_qt;
    struct rt_task *pend_owner;  /* 互斥挂等时=锁主(PI 重算用);
                                  * sem/queue=NULL(typedef 于 struct 后,
                                  * 同 next/wq_next 用 struct 形) */
    u32 timed_out;               /* 扫描置位,take 环消费 */
    u32 low_warned;              /* 站23B:live-sp 守卫一次性告警旗
                                  * (create 清零;deepdiver 封窗深潜
                                  * tick 看不见,零假阳性) */
    /* 站二十八:EL0 用户任务面(结构尾追加,同上契约)。
     * usp=用户 SP(SP_EL0 硬件域,异常进出由 vec.S 存还);
     * ksp=本任务内核栈顶(EL0 陷入的帧栈+阻塞续跑栈);
     * el0=EL0 标记(tick 栈守卫对用户栈不适用,守卫跳过)。
     * 偏移钉死供 asm .equ 契约(下方断言)。 */
    u64 usp;                     /* @672 */
    u64 ksp;                     /* @680 */
    u32 el0;                     /* @688 */
    u32 _pad0;                   /* 尾扩前的 4 填充(显式,免暗排) */
    /* 站二十九:进程+地址空间(结构尾追加,同上契约)。
     * ttbr=本任务的 TTBR0 全值(asid<<48|L0;0=内核 PGD——RT_SWITCH_
     * BOOKKEEP 每切换必装,偏移钉死供 asm .equ);
     * proc=所属进程(EL0 任务非空;内核任务 NULL)。 */
    u64 ttbr;                    /* @696 */
    struct rt_proc *proc;        /* @704 */
    /* 站三十二:futex 键随等待者+分诊位(尾加纪律,asm 断言不涉)。
     * fut_proc/fut_va=等待者登记自己的 key(桶内多 key 共存,
     * WAKE 摘链按 key 过滤);fut_woken/sig_intr=唤醒方置位,
     * 等待环消费后自清。 */
    struct rt_proc *fut_proc;    /* @712 */
    u64 fut_va;                  /* @720 */
    u32 fut_woken;
    u32 sig_intr;
    /* 站三十五:线程面(尾加纪律,asm 断言不涉;偏移钉死供簿记
     * .literal 用)。tpidr=TPIDR_EL0 每任务存档(CLONE_SETTLS 初值,
     * vec.S 簿记切换点存还);ctid=CLEARTID 地址(set_tid_address 同位,
     * 线程退出内核写 0+futex 唤醒=musl join/thread_list 锁的兑付面);
     * tid=每任务线程号(0=未分,leader=pid)。 */
    u64 tpidr_el0;                /* @736 */
    u64 ctid;                     /* @744 */
    u64 tid;                      /* @752 */
    u32 sig_pending;              /* 站38:任务级待决位图(tgkill 定向)@760 */
    u32 _pad2;                    /* @764 */
    u64 sig_val;                  /* 站38:定向信号载荷值@768 */
};
typedef struct rt_task rt_task_t;
_Static_assert(__builtin_offsetof(rt_task_t, usp) == 672, "RT_TASK_USP=672");
_Static_assert(__builtin_offsetof(rt_task_t, ksp) == 680, "RT_TASK_KSP=680");
_Static_assert(__builtin_offsetof(rt_task_t, el0) == 688, "RT_TASK_EL0=688");
_Static_assert(__builtin_offsetof(rt_task_t, ttbr) == 696, "RT_TASK_TTBR=696");
_Static_assert(__builtin_offsetof(rt_task_t, tpidr_el0) == 736, "RT_TASK_TPIDR=736");
_Static_assert(__builtin_offsetof(rt_task_t, ctid) == 744, "RT_TASK_CTID=744");
_Static_assert(__builtin_offsetof(rt_task_t, tid) == 752, "RT_TASK_TID=752");
_Static_assert(__builtin_offsetof(rt_task_t, sig_pending) == 760,
               "RT_TASK_SIGPEND=760");
_Static_assert(__builtin_offsetof(rt_task_t, sig_val) == 768,
               "RT_TASK_SIGVAL=768");


/* asm 契约断言:vec.S 的 .equ RT_TASK_Q/FPCR/FPSR 必须与此一致
 * (改结构必编译期炸——站 17 名字长度栈下标的教训:契约要机器盯) */
_Static_assert(__builtin_offsetof(rt_task_t, q) == 96, "RT_TASK_Q=96");
_Static_assert(__builtin_offsetof(rt_task_t, fpcr) == 608, "RT_TASK_FPCR=608");

extern rt_task_t *volatile rt_cur_arr[RT_NCPU];   /* 站二十:per-CPU 当前
                                                   * 任务;宏保旧名,vec.S
                                                   * 契约符号转数组 */
extern volatile u64 rt_ticks;

/* 站二十:本核号。TPIDR_EL1 引导/次核入口各写一次(0/1),此后只读
 * ——per-CPU 数据索引的标准位置(免每次读 MPIDR)。 */
static inline u32 rt_cpu_id(void)
{
    u64 v;

    __asm__ volatile("mrs %0, tpidr_el1" : "=r"(v));
    return (u32)v;
}

#define rt_cur (rt_cur_arr[rt_cpu_id()])   /* C 树零改动:读写点全保旧名 */

/* 站23B:当前 SP 直读(asm;判深/守卫共用——__builtin_frame_address
 * 在 -O2 无帧指针下不可用,站21 实测定谳)。 */
static inline char *rt_cur_sp(void)
{
    char *p;

    __asm__ volatile("mov %0, sp" : "=r"(p));
    return p;
}

/* rt_task.c */
rt_task_t *rt_task_create(const char *name, void (*entry)(void *),
                          void *arg, u32 prio);
rt_task_t *rt_task_create_cpu(const char *name, void (*entry)(void *),
                             void *arg, u32 prio, u32 cpu); /* 站二十 */
void rt_task_exit(void) __attribute__((noreturn));
void rt_sleep_ticks(u64 n);
void rt_block_rx(void);
const char *rt_state_name(u32 s);

/* ---- 站二十八:EL0 用户态+syscall 骨架 ---- */
rt_task_t *rt_task_create_el0(const char *name, void (*entry)(void *),
                              void *arg, u32 prio, void *ustack_top);
void rt_syscall_dispatch(u64 *frame);
void rt_syscall_init(void);
void rt_el0_fault(u64 *frame);
u64  rt_pid_of(rt_task_t *t);
void rt_mm0_init(void);
extern u64 rt_el0_tramp[RT_NCPU][4];   /* 站28:EL0 异常蹦床(vec.S 契约) */
extern char rt_shell_ustack[16384];    /* 站28:壳用户栈(.user 段) */

/* 站二十八 syscall ABI(偏差 #27:Linux arm64 标准——号 x8/参
 * x0-x5 六参/返 x0 负 errno;标准号=asm-generic 原样,eshine 扩展
 * 段 512 起;超六参走结构体指针,对参考实现七参是登记偏差)。 */
#define ES_NR_read        63
#define ES_NR_write       64
#define ES_NR_getpid      172
#define ES_NR_sysinfo     512
#define ES_NR_hvc         513
#define ES_NR_demo        514
#define ES_NR_MAX         600
#define ES_ENOSYS         38
#define ES_EINVAL         22

/* 站二十九:进程+地址空间号面(标准号优先——站 27 偏差 #24 续行;
 * fork/exec_setup 走扩展段:语义为参考实现 COPY_UCONTEXT/EXEC_SETUP
 * 形状,非 Linux clone 旗语/execve,诚实独立号) */
#define ES_NR_exit        93    /* 标准 __NR_exit:进程退场+回收链 */
#define ES_NR_munmap      215   /* 标准 __NR_munmap(ANON 段摘除) */
#define ES_NR_mmap        222   /* 标准 __NR_mmap(ANON 零填,lazy 补页) */
#define ES_NR_wait4       260   /* 标准 __NR_wait4(子集:指定子+无 options) */
#define ES_NR_fork        520   /* 扩展:COPY_UCONTEXT eaged 全拷 */
#define ES_NR_exec_setup  521   /* 扩展:7-参帧改写式 exec(内核不读镜像) */
#define RT_FORK_MAGIC     0x464B4348ULL  /* "FKCH":fork 子进程返回值自证 */

/* 站三十:消息 IPC 号面(QNX 镜像三段语义走扩展段——非 Linux 号,
 * 诚实独立;参考实现形状:三段消息/pulse/rcvid 复合/abort+UNBLOCK) */
#define ES_NR_ch_create    530   /* 通道建立(flags) */
#define ES_NR_ch_destroy   531
#define ES_NR_name_attach  532   /* 命名登记(路径, chid) */
#define ES_NR_name_detach  533
#define ES_NR_name_open    534   /* 层级化查名->coid(连接实体+引用计数) */
#define ES_NR_conn_detach  535   /* coid 引用减一(0 即自由) */
#define ES_NR_msg_send     536   /* 三段之一:SEND(sbuf->内核槽)+等回执 */
#define ES_NR_msg_recv     537   /* 三段之二:RECV(阻塞/超时;脉冲 rcvid=0) */
#define ES_NR_msg_reply    538   /* 三段之三:REPLY(status 即发送者返回值) */
#define ES_NR_msg_pulse    539   /* 异步脉冲 8B(code+value),不阻塞 */
#define RT_CHF_UNBLOCK     1u    /* abort 面递送 UNBLOCK 脉冲(系统码负) */
#define RT_PULSE_UNBLOCK   (-1)  /* 参考实现 PULSE_CODE_UNBLOCK 镜像 */

/* 站三十一:fd=通道三元组号面(参考实现 fdtable/posix_queue 会合点的
 * 站内收编;QNX resmgr 模型。Linux 号统一迁移=站 33 musl 弧,v0
 * 走扩展段诚实独立——read/write 63/64 仍属原始字符面) */
#define ES_NR_fdopen      550   /* 路径->fd(三元组{coid,ocb}) */
#define ES_NR_fdclose     551   /* fd 释放(CLOSE->服务器 ocb 引用减) */
#define ES_NR_fdread      552   /* fd 读(READ 走三段消息;0=EOF) */
#define ES_NR_fdwrite     553   /* fd 写(WRITE 数据内联≤44B/次) */

/* 站三十六:shm+xspace——内核登记共享段(参考实现 xspace 全局窗/
 * VM_SET 单拷窗的站内形状;shmid 传引用=单拷,载荷走共享窗)。
 * v0 固定 VA:xspace 块 8 @0x41000000,段 i 映
 * XSPACE_BASE+(i-1)*SPAN=同段任何进程同 VA。 */
#define ES_NR_fdcreate    554   /* 站39:路径->fd(建文件) */
#define ES_NR_fdunlink    555   /* 站39:路径摘除 */
#define ES_NR_fddir       556   /* 站39:目录枚举(索引->路径) */
#define ES_NR_shm_create  560   /* len 字节->段号(页池取页清零) */
#define ES_NR_shm_attach  561   /* (seg,va=0 固定窗)->实际 VA */
#define ES_NR_shm_detach   562   /* 摘挂(refs--) */
#define ES_NR_shm_destroy  563   /* 段销毁(refs!=0 拒;页还池) */
#define RT_SHM_MAX         8     /* 段表容量 */
#define RT_SHM_SEG_PAGES   16    /* 每段最大页(64KB 跨度) */
#define RT_XSPACE_BASE     0x41000000UL
#define RT_XSPACE_SPAN     (RT_SHM_SEG_PAGES * 4096UL)

/* 站三十七:posix_queue 弧——mqueue(命名优先级队列)+
 * pipe(骑站 31 resmgr 的字节流)+长消息 1024B+MsgRead 镜像
 * (参考实现机制白描:msg/stream 双 ops;长消息拷内核缓冲;
 * 接收者事后按偏移再读所持消息)。 */
/* 站37 起:fd wire 契约驻此(跨文件:rt_fsrv.c+rt_pipe.c 同吃) */
#define RT_FDOP_OPEN    1u
#define RT_FDOP_READ    2u
#define RT_FDOP_WRITE   3u
#define RT_FDOP_CLOSE   4u
#define RT_FDOP_DUP     5u
#define RT_FDOP_SEEK    6u     /* 站33 起;站39 rsv=whence(0SET/1CUR/2END) */
#define RT_FDOP_CREATE  7u     /* 站39:建文件即开(ramfs 写时复制面) */
#define RT_FDOP_UNLINK  8u     /* 站39:摘名摘节点(页还池) */
#define RT_FDOP_READDIR 9u     /* 站39:a=游标,data=[d_type][名],status=下一游标(0=末) */
#define RT_FDOP_STAT   10u     /* 站40:data=路径,回执=status=(mode<<32)|size */

#define RT_FD_DATA      44u    /* 请求内联窗:OPEN 路径/WRITE 数据 */
#define RT_FD_RDATA     64u    /* READ 回复数据窗(站33:status 走
                                 * reply 面,rbuf 可满 64) */
struct rt_fdreq {
    u32 op;
    u32 a;                     /* READ:窗长;WRITE:数据长 */
    u32 ocb;                   /* READ/CLOSE/DUP:ocb 号 */
    u32 rsv;
    char data[RT_FD_DATA];
};

#define ES_NR_mq_open      570   /* 路径->mqd(查名或建) */
#define ES_NR_mq_send      571   /* (mqd,val,prio) */
#define ES_NR_mq_receive   572   /* (mqd,timeout)->val(FR(0)=val 出) */
#define ES_NR_mq_close     573
#define ES_NR_mq_unlink    574   /* 路径摘名 */
#define ES_NR_msg_read     575   /* MsgRead 镜像(chid,rcvid,off,buf,len) */
#define ES_NR_pipe         576   /* ->两 fd(read/write 端) */
#define RT_MQ_MAX          8     /* 命名队列表 */
#define RT_MQ_DEPTH        8     /* 每队列深 */
#define RT_MSG_LONG        1024u /* 长消息上限(>64B 走长槽池) */
#define RT_MSGLONG_MAX     8u    /* 长槽池 */

/* 站三十二:signal+futex 号面——真 Linux arm64(asm-generic)原号,
 * 站 33 musl 直连。v0 偏差(spec §2 记档,站 33 对齐):futex 四参
 * (uaddr,op,val,ticks——Linux 第四参 timespec 指针)/sigaction
 * 两参(sig,handler——Linux 三结构指针)。 */
#define ES_NR_futex         98   /* __NR_futex:WAIT/WAKE 两脸 */
#define ES_NR_kill         129   /* __NR_kill:跨进程投递 */
#define ES_NR_rt_sigaction 134   /* __NR_rt_sigaction:装 handler */
#define ES_NR_rt_sigreturn 139   /* __NR_rt_sigreturn:恢复帧 */
#define RT_SIG_MAX          32u  /* 信号号域:1..31(pending 位图宽) */
#define RT_FUTEX_WAIT       0
#define RT_FUTEX_PRIVATE_FLAG 128u /* 站35:musl 恒带,掩掉=同 proc 提示 */
#define RT_FUTEX_WAIT_BITSET  9u   /* 站35:musl 定时面用;无墙钟,v0 绝对降格相对 */
#define RT_FUTEX_WAKE       1

/* 站三十三:musl 薄面号(全真 Linux asm-generic;musl 启动期实测
 * 需求面:writev=stdio 真通路/exit_group=真退场/set_tid_address=
 * __init_tp/ioctl=isatty 探测/rt_sigprocmask=掩码兑现/lseek=
 * loader 段定位;exit_group 94 与 exit 93 同面直挂) */
#define ES_NR_ioctl         29
#define ES_NR_lseek         62
#define ES_NR_writev        66
#define ES_NR_exit_group    94
#define ES_NR_set_tid_address 96
#define ES_NR_rt_sigprocmask 135
#define ES_NR_rt_sigtimedwait 137  /* 站38:v0 轮询面(阻塞全弧后弧) */
#define ES_NR_rt_sigqueueinfo 138  /* 站38:kill+值+tid 定向 */
#define ES_NR_gettid         178   /* 站35:每任务线程号 */
#define ES_NR_clone          220   /* 站35:真 __NR_clone(musl pthread) */

/* 站四十:busybox/服务收编薄面(qemu-user -strace 实测需求面,零
 * 臆测)。全真 Linux asm-generic 号。uid 族=0(root 世界);brk=窗内
 * 游标堆(mallocng 探测);getdents64=每调一名(POSIX 合法面);
 * newfstatat=STAT wire(mode+size 出,ls 分类面)。 */
#define ES_NR_dup3           24    /* fd 复制(DUP 面:ocb+conn 双引用) */
#define ES_NR_fcntl          25    /* v0:F_SETFD/F_GETFD 回 0 */
#define ES_NR_openat         56    /* fdopen 面+目录回退(服务器拥有名空间) */
#define ES_NR_close          57    /* fdclose 面 */
#define ES_NR_getdents64     61    /* fd 层:一次一名 dirent64 */
#define ES_NR_newfstatat     79    /* STAT wire:mode<<32|size */
#define ES_NR_setuid         105   /* root 语义:回 0 */
#define ES_NR_setgid         106
#define ES_NR_clock_gettime  113   /* REAL/MONO;real=BOOT_EPOCH+cnt */
#define ES_NR_clock_getres   114
#define ES_NR_gettimeofday   169
#define ES_NR_getuid         174   /* uid 族全 0 */
#define ES_NR_geteuid        175
#define ES_NR_getgid         176
#define ES_NR_getegid        177
#define ES_NR_brk            214   /* proc 堆:游标 mmap 增量 */
#define ES_NR_getrandom      278   /* 混出器直面(与 /dev/random 同源) */
#define RT_BOOT_EPOCH_NS  1793491200000000000ULL /* 2026-11-01T00:00:00Z(drvfw/RTC=后弧) */

/* ---- 站四十一:CAPR 能力框架(参考实现五面的最小站内形) ----
 * 句柄式 cap 表=粗粒度位图 v0;fail-closed=执法点缺能力即拒;
 * self-inc-out=无任何加能力路径(只减不增由构造保证);
 * rlimit=max_fd/max_child 自降单向;uuid=创建时随机身份
 * (站 40 混出器同源,fork=新 uuid)。 */
#define ES_NR_cap_drop   580   /* 自削掩码(单向) */
#define ES_NR_cap_get    581   /* 自查位图 */
#define ES_NR_uuid_get   582   /* 自查身份 */
#define ES_NR_rlimit_self 583  /* 自降 max_fd/max_child(单向) */
#define ES_EPERM          1
#define ES_EMFILE         24
#define CAP_VFS    0x1ULL     /* fd 六门:open/create/unlink/dir/dents/stat */
#define CAP_SPAWN  0x2ULL     /* fork/clone */
#define CAP_KILL   0x4ULL     /* kill/sigqueue */
#define CAP_MM     0x8ULL     /* mmap/shm/mq/pipe */
#define CAP_ALL    0xFULL

/* ---- 站四十二:syscall 面补全(参考实现终审计补齐弧;号面从
 * musl-1.2.5 aarch64 头钉死零臆测) ---- */
#define ES_NR_set_robust_list  99   /* musl pthread 启动即注册 */
#define ES_NR_get_robust_list  100
#define ES_NR_ppoll            73   /* musl poll 走 ppoll */
#define ES_NR_timer_create     107
#define ES_NR_timer_gettime    108
#define ES_NR_timer_getoverrun 109
#define ES_NR_timer_settime    110
#define ES_NR_timer_delete     111
#define ES_NR_getitimer        102
#define ES_NR_setitimer        103
#define ES_NR_sched_setparam   118
#define ES_NR_sched_setscheduler 119
#define ES_NR_sched_getscheduler 120
#define ES_NR_sched_getparam   121
#define ES_NR_sched_setaffinity 122
#define ES_NR_sched_getaffinity 123
#define ES_NR_sched_yield      124
#define ES_NR_sched_get_priority_max 125
#define ES_NR_sched_get_priority_min 126
#define ES_NR_sched_rr_get_interval 127
#define ES_NR_restart_syscall  128
#define ES_NR_sigaltstack      132
#define ES_NR_rt_sigsuspend    133
#define ES_NR_rt_sigpending    136
#define ES_NR_rt_sigtimedwait  137
#define ES_NR_setpriority      140
#define ES_NR_getpriority      141
#define ES_NR_setpgid          154
#define ES_NR_getpgid          155
#define ES_NR_getsid           156
#define ES_NR_setsid           157
#define ES_NR_getcpu           168
#define ES_NR_uname            160
#define SCHED_OTHER  0
#define SCHED_FIFO   1
#define FD_CLOEXEC   1          /* fcntl F_SETFD 位(ES_NR_fcntl 25 已在真号块) */


/* 内核镜像命令号(demo 蹦床:EL0 壳经 sys_demo 让内核语境跑既有
 * 命令体——80 门判据逐字节不变;30+ 各面 syscall 化后逐批退役) */
enum {
    RT_U_SELFTEST = 1, RT_U_PS, RT_U_SPAWN, RT_U_FP, RT_U_SEMDEMO,
    RT_U_MUTEXDEMO, RT_U_PIDEMO, RT_U_FPDUAL, RT_U_EVENTDEMO,
    RT_U_QUEUEDEMO, RT_U_SMPDEMO, RT_U_IPIDEMO, RT_U_FPSMP,
    RT_U_TMODEMO, RT_U_SWTDEMO, RT_U_IRQDEMO, RT_U_LOWDEMO,
    RT_U_STKDEMO, RT_U_STACKCHECK, RT_U_OVERFLOW, RT_U_FPSTAT,
    RT_U_HB, RT_U_SLEEP, RT_U_IPCDEMO, RT_U_PULSEDEMO, RT_U_ABORTDEMO,
    RT_U_LONGDEMO,
    RT_U_FUPOKE,
};

/* ES_NR_sysinfo 的共享结构(user<->kernel) */
struct rt_sysinfo {
    char ver[24];
    u64 tick;
    u32 el0;
    u32 rsv;
};
void rt_task_pool_init(void);   /* 站35:素槽 RT_FREE 清池 */
void rt_task_wake_all(u32 mask);         /* 扫池:命中等待位的 BLOCK->READY */
void rt_task_wake_sleepers(void);       /* 扫池:到期的 SLEEP->READY */
rt_task_t *rt_pool_base(void);            /* 站28:tp 守卫用池域判据 */

/* ---- 站二十九:进程+地址空间(rt_as.c) ----
 * 进程对象(参考实现 task_group 的站内尺度):pid/ASID/自持 L0-L1-L2/
 * VMA 申报/父子关系。高低半区几何与页池驻 rt_as.c(实现面)。 */
#define RT_PROC_MAX 4u
#define RT_AS_VMA_MAX 32u  /* 站35:musl pthread 每线程栈 mmap+TLS 地图(旧 6 被 fork 继承 2+loader 段 2+stdio 恰满) */

/* 站三十一:fd 表(=通道三元组,参考实现 fdtable 站内收编)——
 * 路由面=连接实体(->服务器通道),状态面=ocb(服务器侧每打开
 * 一份;fd 只是这两字的本地名)。stdio 0/1/2=PM 开机绑 /dev/tty。 */
#define RT_FD_MAX 8u
struct rt_fd {
    u32 used;
    int coid;
    u32 ocb;
    u32 dircur;                    /* 站40:getdents64 每游标(READDIR 回执兑付) */
    u32 cloexec;                   /* 站42:FD_CLOEXEC 位(fcntl F_SETFD) */
};

enum { RT_PROC_CREATE, RT_PROC_RUN, RT_PROC_ZOMBIE };

struct rt_vma {
    u64 start, end;               /* [start, end) */
    u32 pool;        /* 0=ID 恒等(镜像/栈),1=页池,2=SHM 共享段 */
    u32 shm_id;      /* 站36:pool=2 时的段号(rsv 槽转正) */
};

struct rt_proc {
    u32 used;
    u32 state;                    /* CREATE(挂起)/RUN/ZOMBIE */
    int pid;
    int parent;
    int exit_code;
    u32 asid;
    u64 *l0, *l1;
    u64 *l2[9];                   /* l1 槽 1..8 页表(按需;8=xspace) */
    struct rt_vma vma[RT_AS_VMA_MAX];
    u32 nvma;
    u64 mmap_next;                /* ANON VA 游标(mmap 窗) */
    struct rt_fd fds[RT_FD_MAX];  /* 站31:fdtable(=通道三元组) */
    /* 站三十二:signal 面(v0 位图态,每进程单任务;参考实现每任务
     * pending 的站内尺度)。sig_hand[0] 不用(号 0 非法)。 */
    u64 sig_hand[RT_SIG_MAX];   /* handler 用户地址(0=默认=终止,站38) */
    u64 sig_rest[RT_SIG_MAX];   /* 站38:SA_RESTORER 面(handler ret 落点) */
    u64 sig_val[RT_SIG_MAX];   /* 站38:kill 携值(x1=sigval,per-sig 单值) */
    u32 sig_pending;
    u32 sig_blocked;
    u32 sig_active;             /* handler 在役:嵌套投递拒,留下轮 */
    u32 _pad1;
    u64 sig_saved[34];          /* 投递时被中断的 272B 用户上下文 */
    u64 sig_saved_usp;          /* 同上,SP_EL0(驻 t->usp,不在帧内
                                  * ——handler 序言压帧+sigreturn 跳
                                  * 过其 epilogue,SP 不还原=错窗读
                                  * 局部,冒烟+反汇编双定谳) */
    struct rt_task *leader;
    u64 brk;                      /* 站40:堆游标(0=未起;exec 后=mmap_next;
                                  * brk(0)=读,brk(a)=窗内 hint-mmap 增量) */
    u64 caps;                     /* 站41:能力位图(create=0;壳=CAP_ALL;
                                  * fork 继承;无加能力路径=self-inc-out) */
    u64 uuid;                     /* 站41:进程身份(创建随机,fork 新) */
    u32 max_fd;                   /* 站41:rlimit(fd 槽上限,默认 RT_FD_MAX) */
    u32 max_child;                /* 站41:rlimit(子数上限) */
    /* 站42:参考实现终审计补齐弧——进程组/会话+robust futex+
     * posix timer(尾追纪律:前部偏移 asm/帧契约钉死)。 */
    int pgid;                     /* 进程组(leader 自成组 pid;fork 承继) */
    int sid;                      /* 会话(setsid=自成会话) */
    u64 robust_head;              /* musl pthread robust 链头(0=未注册) */
    u32 robust_len;
    u64 sig_altsp;                /* 站42:sigaltstack 注册(v0=记档不切栈) */
    u32 sig_altsize;
    u32 sig_retmask;              /* 站42:sigsuspend 还原掩码(sigreturn 兜) */
    u32 sig_suspflag;             /* 1=本次投递源自 sigsuspend 窗 */
};
typedef struct rt_proc rt_proc_t;


/* rt_sync.c(第十七站):同步原语——每对象 FIFO 等待队列(参考RTOS 公开
 * 设计为基准的最小形态:唤醒队头,非广播)。take 重检环(醒后自己复核
 * 条件),give 让渡+唤醒队头,被醒者更高优先则立即让位;
 * "唤醒=允许重试,非授予"。 */
typedef struct rt_sem {
    long count;                   /* 可用资源数 */
    rt_task_t *wq_head;           /* 等待队列(单向,尾链) */
    rt_task_t *wq_tail;
} rt_sem_t;

/* ---- 站三十:三段消息 IPC(rt_msg.c;参考实现 同步队列件 的站内收编) ---- */
#define RT_CH_MAX        16u   /* 站40 容量账:boot 7 服务器(4+3)+demo 瞬时余量 */
#define RT_CONN_MAX      24u   /* 站40:壳 3+子 stdio 3×2+loader/枚举瞬时 */
#define RT_MSGSLOT_MAX   8u
#define RT_NAME_MAX      32u   /* 站40 容量账:boot 15 名(8+busybox/inittab/beat/initmark/proc/random/slog)+demo 瞬时 */
#define RT_MSG_BYTES     64u    /* 短消息面(参考实现 1024B 收编;长消息
                                 * 映射=跨址空间窗,v0 内核槽中继,VM_SET
                                 * 单拷窗=跨 VM 站后议) */
#define RT_NAME_PATH     48u

/* RECV 出参(用户/内核同构;syscall 侧整结构拷出) */
struct rt_recvinfo {
    u64 rcvid;                  /* 0=脉冲(QNX/参考实现镜像) */
    u32 len;
    u32 pad;
    s64 code;                   /* 脉冲码(系统负/用户≥0) */
    u64 value;
};

struct rt_msg {
    u32 used;
    u32 state;                  /* 0=QUEUED 1=RECEIVED(服务器持有) */
    u32 is_pulse;               /* 单向 8B,无回执 */
    u32 aborted;                /* 发送者超时先走:REPLY 静默收尾 */
    int sender_pid;
    rt_task_t *sender;          /* NULL=脉冲 */
    u32 sprio;                  /* 到达优先级序+提权依据 */
    s64 code;                   /* 脉冲码(系统负/用户>=0) */
    u64 value;                  /* 脉冲值 */
    u32 len;
    char data[RT_MSG_BYTES];
    u32 long_idx;               /* 站37:长槽号+1(0=短内联) */
    rt_sem_t reply_sem;         /* 发送者等回执(1 槽) */
    u32 rlen;
    char rdata[RT_MSG_BYTES];
    long status;                /* REPLY 携带,即发送者返回值 */
};

struct rt_chan {
    u32 used;
    u32 flags;
    int owner_pid;
    rt_sem_t msgs;              /* 到达面:RECV 阻塞/计数(站 17 机器) */
    struct rt_msg *q[RT_MSGSLOT_MAX];  /* 到达队列(优先级序) */
    u32 nq;
};

struct rt_conn {                /* 六差距①:coid 实体+引用计数 */
    u32 used;
    u32 refcnt;
    int chid;
    int owner_pid;
};

struct rt_name {                /* 命名登记(层级路径;平表+分段比较,
                                 * 真层级树=站 31 命名空间) */
    u32 used;
    char path[RT_NAME_PATH];
    int chid;
};

int  rt_msg_ch_create(u32 flags);
int  rt_msg_ch_destroy(int chid);
int  rt_msg_conn_attach(int chid);
int  rt_msg_conn_detach(int coid);
int  rt_msg_name_attach(const char *path, int chid);
int  rt_msg_name_detach(const char *path);
int  rt_msg_name_open(const char *path);
long rt_msg_send(int coid, const void *sbuf, u32 slen,
                 void *rbuf, u32 rlen, u64 ticks);
long rt_msg_recv(int chid, void *buf, u32 buflen, u64 ticks,
                struct rt_recvinfo *info);
long rt_msg_read(int chid, u64 rcvid, u64 off,
                 void *buf, u32 len);   /* 站37:MsgRead 镜像 */
/* 站37:mqueue(rt_mq.c)——命名优先级队列,阻塞=站 17 pend 机器 */
long rt_mq_open(const char *path);       /* ->mqd 或负 errno */
long rt_mq_send(long mqd, u64 val, u32 prio);
long rt_mq_receive(long mqd, u64 *val, u64 ticks);
long rt_mq_close(long mqd);
long rt_mq_unlink(const char *path);
/* 站37:pipe(rt_pipe.c)——pipesrv(站 31 resmgr 形) */
long rt_pipe_create(struct rt_proc *p);  /* ->0(两 fd 出参 FR 面) */
void rt_pipe_boot(void);                 /* 开机:服务器起立+挂名 */
/* 站39:blkdev(rt_blk.c)——第四个 resmgr 服务器+参数化 virtio 面 */
void rt_blk_boot(void);                  /* 开机:探针相(锚序原位) */
void rt_blk_srv_boot(void);              /* 开机:IRQ 登记+服务器起立 */
int  rt_blk_rw(u32 type, u64 sector, void *buf512, u32 ticks);
/* 站40:服务收编三服务器(random/procfs/slog,rt_rng/rt_procfs/rt_slog.c) */
void rt_rng_boot(void);                  /* 开机:第五服务器 /dev/random */
void rt_rng_fill(char *buf, u32 len);    /* 混出器直面(fd READ 与 getrandom 同源) */
void rt_procfs_boot(void);               /* 开机:第六服务器 /proc */
void rt_slog_boot(void);                 /* 开机:第七服务器 /dev/slog */
u64  rt_time_mono_ns(void);              /* cntvct/freq(rt_timer.c) */
u64  rt_time_real_ns(void);              /* BOOT_EPOCH+mono(RTC=后弧) */
/* fd 层站39 扩(rt_fsrv.c) */
long rt_fd_create(struct rt_proc *p, const char *kpath);
long rt_fd_unlink(struct rt_proc *p, const char *kpath);
long rt_fd_dir(struct rt_proc *p, int index, char *kpath, u32 len);
int  rt_fd_seek_whence(struct rt_proc *p, int fd, u64 off, u32 whence);
long rt_msg_reply(int chid, u64 rcvid, const void *rbuf, u32 rlen,
                  long status);
long rt_msg_pulse(int coid, s64 code, u64 value);
/* demo 服务器/干扰任务(sys_demo 蹦床 spawn;体在 rt_msg.c) */
void rt_msg_demo_server(void *arg);
void rt_msg_demo_spinner(void *arg);
void rt_msg_pulse_server(void *arg);
void rt_msg_long_server(void *arg);   /* 站37:长消息+MsgRead 镜像 */
void rt_msg_abort_server(void *arg);
int  rt_msg_conn_ref(int coid);   /* 站31:coid 引用+1(fork 继承面) */

/* ---- 站三十一:fd 层+resmgr 服务器(rt_fsrv.c) ----
 * fd=通道三元组:open=查名->连接+服务器立 ocb;读写经三段消息
 * 往返到 resmgr 服务面(tty 服务器/ramfs 服务器);fork=fdtable
 * 组拷+每继承 coid 引用+1(参考实现 COPY_UCONTEXT 同款);exit=逐 fd
 * CLOSE(服务器 ocb 引用减)+断连——POCB 引用计数的客户端镜像。
 * v0 服务器=内核任务直调 IPC 层("用户态 server"二进制=站 33
 * musl 弧;开机链=固定序列 tty->ramfs->pm,参考实现 F3-18 僵化审计
 * 记档+协商位留后)。 */
int  rt_fd_open(struct rt_proc *p, const char *kpath);
int  rt_fd_close(struct rt_proc *p, int fd);
long rt_fd_seek(struct rt_proc *p, int fd, u32 off);  /* 站33:SEEK_SET 面 */
long rt_fd_read(struct rt_proc *p, int fd, char *kbuf, u32 len);
long rt_fd_write(struct rt_proc *p, int fd, const char *kbuf, u32 len);
void rt_fd_fork(struct rt_proc *pp, struct rt_proc *cp);
void rt_fd_close_all(struct rt_proc *p);
/* 站40:busybox/POSIX 薄面(fd 层新四门) */
long rt_fd_getdents64(struct rt_proc *p, int fd, char *ubuf, u32 len); /* 每调一名 */
long rt_fd_fstatat(struct rt_proc *p, const char *kpath, u32 *mode, u32 *size);
long rt_fd_dup3(struct rt_proc *p, int oldfd, int newfd);
long rt_proc_child_alive(struct rt_proc *p, int pid);   /* WNOHANG 判活 */
u32  rt_proc_child_count(struct rt_proc *p);  /* 站41:rlimit fork 面 */
int  rt_ptimer_in_pool(void *tm);     /* 站42:tp 守卫第二驻地谓词 */
void rt_srv_boot(rt_task_t *shell);   /* 固定序列 spawn(boot 链面) */

/* ---- 站三十二:signal+futex(rt_sig.c;参考实现 §10 站内收编) ----
 * futex=musl 锁原语底座:WAIT=值校验+站 21 单窗阻塞+四路分诊
 * (超时/被唤醒/被信号打断/值动);WAKE=桶内 key 匹配摘链(不读
 * 用户字,纯队列操作)。signal=位图 pending+syscall 出口投递
 * (帧手术:sig_saved 整帧存+elr=handler/x0=sig)+rt_sigreturn
 * 恢复。跨进程信号打断 futex 等待=两原语互为判据(spec §0)。 */
long rt_futex(struct rt_proc *p, u64 uaddr, u32 op, u32 val, u64 ticks);
long rt_futex_wake_key(struct rt_proc *p, u64 va, u32 n);
int  rt_sig_action(struct rt_proc *p, u32 sig, u64 handler, u64 restorer);
int  rt_sig_kill(int pid, u32 sig);
long rt_sig_queueinfo(long pid_or_tid, u32 sig, u64 value);  /* 站38 */
long rt_sig_queueinfo_ctx(long pid_or_tid, u32 sig, u64 value); /* 站42:tick 语境禁调度 */
long rt_sig_timedwait(u32 sig, u64 ticks);   /* 站38:v0 轮询面 */
int  rt_sig_deliver(u64 *frame);       /* syscall 出口(disp 尾);
                                         * 返 1=默认动作终局(dispatcher 走 exit) */
long rt_sig_return(u64 *frame);        /* sig_saved 回拷+x0 同值回写 */
void rt_sig_fork(struct rt_proc *pp, struct rt_proc *cp);
struct rt_proc *rt_proc_lookup(int pid);  /* 活进程查(死=ESRCH 面) */
void rt_futex_poke(struct rt_proc *p, u64 va, u64 delay);  /* 扑克(sys_demo 面) */
void rt_fp_trap_cur(void);   /* 站33:FP 陷阱体(EL0/EL1 共用) */

/* ---- 站三十五:线程(rt_thr.c;musl aarch64 clone 契约) ----
 * 子=帧整拷+x0=0+SP=传入栈(musl 父把 func/arg 压上);线程退场
 * =纯任务死(leader 退场才动 proc);PARENT_SETTID=父语境写
 * *ptid;CHILD_CLEARTID=线程退出 *ctid=0+futex 唤醒(musl 的
 * thread_list 锁释放面);SETTLS=新任务 tpidr 初值(簿记存还)。 */
#define RT_CLONE_VM            0x100u
#define RT_CLONE_FS            0x200u
#define RT_CLONE_FILES         0x400u
#define RT_CLONE_SIGHAND       0x800u
#define RT_CLONE_THREAD        0x10000u
#define RT_CLONE_SYSVSEM       0x40000u
#define RT_CLONE_SETTLS        0x80000u
#define RT_CLONE_PARENT_SETTID 0x100000u
#define RT_CLONE_CHILD_CLEARTID 0x200000u
#define RT_CLONE_DETACHED      0x400000u
#define RT_CLONE_NEEDS  (RT_CLONE_VM | RT_CLONE_THREAD)
#define RT_CLONE_ALLOW (RT_CLONE_NEEDS | RT_CLONE_FS | RT_CLONE_FILES | \
                        RT_CLONE_SIGHAND | RT_CLONE_SYSVSEM | RT_CLONE_SETTLS | \
                        RT_CLONE_PARENT_SETTID | RT_CLONE_CHILD_CLEARTID | \
                        RT_CLONE_DETACHED)
long sys_clone_impl(u64 *frame, u32 flags, u64 stack, u64 ptid,
                    u64 tls, u64 ctid);
void rt_thr_exit(void);              /* 线程退场:noreturn 纯任务死 */
void rt_thr_group_kill(struct rt_proc *p);  /* leader 退场清扫线程群 */
void rt_fp_trap_cur(void);   /* 站33:FP 陷阱体(EL0/EL1 共用) */
struct rt_proc *rt_proc_of(rt_task_t *t);  /* 任务->进程(NULL=内核) */
int  rt_proc_create(rt_task_t *leader, struct rt_proc **out); /* CREATE 挂起态 */
u64  rt_proc_ttbr(struct rt_proc *p);     /* 切换值 asid<<48|L0 */
void rt_as_declare(struct rt_proc *p, u64 start, u64 end);   /* VMA 申报 */
int  rt_pfault(u64 va, u64 esr);           /* 缺页补页;0=已补,-1=VMA 外 */
void rt_proc_exit(struct rt_proc *p, int code);  /* 退场+页池回收+TLB 冲 */
int  rt_proc_fork_as(struct rt_proc *parent,
                     struct rt_proc *child);     /* eager 全拷 */
long sys_exec_setup_impl(u64 *frame, u64 entry, u64 sp,
                         u64 a0, u64 a1, u64 a2, u64 a3);
void rt_proc_boot_shell(rt_task_t *shell);  /* 进程 1:镜像/栈申报+预映 */
u64  rt_as_mmap(struct rt_proc *p, u64 hint, u64 len);  /* 站33:hint 落位,0=满/败 */
int  rt_as_munmap(struct rt_proc *p, u64 start, u64 len);
/* 站36:shm 段注册表(rt_shm.c)——共享段三面+两钩(fork 继承
 * 同物理零拷贝=隐式 attach refs++;退场逐 SHM VMA 卸引用) */
long rt_shm_create(u32 len_bytes);          /* ->段号 或负 errno */
long rt_shm_attach(struct rt_proc *p, long seg, u64 va);  /* ->实际 VA */
long rt_shm_detach(struct rt_proc *p, long seg);
long rt_shm_destroy(long seg);
int  rt_shm_pfault(struct rt_proc *p, u64 va, u32 shm_id); /* 健全面 */
void rt_shm_fork(struct rt_proc *child);     /* fork:继承 SHM VMA 挂同物理 */
void rt_shm_exit(struct rt_proc *p);         /* 退场:逐 SHM VMA refs-- */
/* 站36:rt_shm.c 消费的 as 暴露面(段页取/还/映/清+SHM VMA 申报) */
u64  rt_as_page_alloc(void);
void rt_as_page_free(u64 pa);
int  rt_as_map_user(struct rt_proc *p, u64 va, u64 pa);
int  rt_as_declare_shm(struct rt_proc *p, u64 start, u64 end, u32 shm_id);
void rt_as_shm_clear(struct rt_proc *p, u64 start, u64 end);
struct rt_proc *rt_proc_zombie_find(int pid, int *exit_code);
void rt_proc_reap(struct rt_proc *p);          /* ZOMBIE 槽全回收 */
rt_task_t *rt_ps_first(unsigned int *idx);
rt_task_t *rt_ps_next(unsigned int *idx);
rt_task_t *rt_idle_task(void);
rt_task_t *rt_idle1_task(void);      /* 站二十:次核 idle(池外伪任务) */
void rtos_secondary_main(u32 cpu);   /* 站二十:次核世界(asm 入口落点) */
void rtos_secondary_entry(void);      /* 站二十:asm 入口(vec.S .global) */
extern char __cpu1_stack_top[];       /* 站二十:链接器次核引导栈页顶 */

/* rt_smp 面(站二十;落在 rt_sched.c) */
u64   rt_spin_lock(unsigned long *lk); /* DAIF 屏 + 跨核自旋(WFE),返回
                                        * 屏蔽前 DAIF;同核递归=死锁 */
void  rt_spin_unlock(unsigned long *lk, u64 d); /* SEV 广播 + DAIF 还原 */
void  rt_ipi(u32 cpu);                 /* 跨核重调度:ICC_SGI1R_EL1 写
                                         * (EL2 陷阱转发+poke,19D 机制) */
void  rt_resched_ipi(void);            /* 收 IPI 的重调度判(SGI handler) */
unsigned long rt_ipi_rx(u32 cpu);      /* 每核收到的 IPI 拍数(demo 判据) */
extern volatile unsigned int rt_ipi_echo; /* 站二十:ipidemo 回合窗(核 1
                                           * handler 见 1 回发核 0) */

/* rt_sched.c */
void rt_sched(void);             /* 让出点:C 选,asm 换 */
rt_task_t *rt_pick_ready(void);  /* 就绪摘头;空则 NULL */
void rt_ready_push(rt_task_t *t);
void rt_ready_requeue(rt_task_t *t); /* 自拣回流令牌:只回队不碰状态(站19) */
void rt_preempt_requeue(rt_task_t *t); /* 抢占回队恒单份:先清残令牌(站19) */
void rdy_remove(rt_task_t *t);   /* 站十八B:PI 降级摘除;站19 起兼清自拣令牌 */
void rt_wake_rx(void);
void rt_tick_bookkeep(void);     /* tick ISR:唤醒/轮转/切判 */
int  rt_need_switch(void);

/* rt_uart.c:自有 RX 环(miniuart.h 的 static 环 per-TU,多 TU 会各持
 * 一份串不成一环——本环是唯一真环,ISR 与壳都走这里) */
void rt_puts(const char *s);     /* 行级临界区(DAIF 屏蔽)打印 */
void rt_puts_n(const char *s, u32 len); /* 站28:长度式行级打印(sys_write 面) */
void rt_puts_u64(const char *pfx, u64 v, const char *sfx);
void rt_puts_hex(const char *pfx, u64 v, const char *sfx); /* 站21B:十六进制
                                   * 裸打(不取 puts_lk)——panic/诊断面,
                                   * 持锁语境取锁=同核自死锁 */
void rt_uart_drain_isr(void);    /* IRQ 上下文:排空 DR 入 RTOS 环 */
int  rt_getc_nb(void);           /* 空环返回 -1,不阻塞 */
int  rt_rx_pending(void);        /* 环非空即 1:挂起前终判([41] 修复判据) */

/* rt_shell.c */
void rt_shell_task(void *arg);

/* rt_fp.c */
void rt_fp_task(void *arg);
void rt_fp_dual_task(void *arg);    /* 站十八C:Σ(i×0.5) 半值闭式 */
void rt_fp_smp_a_task(void *arg);   /* 站二十:fpsmp 核 0 面(整段精确) */
void rt_fp_smp_b_task(void *arg);   /* 站二十:fpsmp 核 1 面(半值精确) */
void rt_fp_dual_a_task(void *arg);  /* 站十八C:Σi 复用,fpdual 独立针 */
void rt_sync_fault(void);           /* 站19C:EC 0x07 FPEN 陷阱,轮转持有权 */
extern rt_task_t *rt_fp_owner_arr[RT_NCPU]; /* 站20:per-CPU FP 持有者
                                             * (每核各持各的 FP 硬件,
                                             * vec.S 簿记符号转数组) */
extern u32 rt_fp_stat_arr[RT_NCPU];  /* 站20:per-CPU 切换计数(asm RMW
                                      * 双核互踩根除;显示走求和) */
extern u32 rt_fp_saves;              /* 站19C:持有权轮转数(fpstat 分子;
                                      * 唤醒源单点,暂全局) */
#define rt_fp_owner (rt_fp_owner_arr[rt_cpu_id()])
#define rt_fp_stat  (rt_fp_stat_arr[rt_cpu_id()])
u32 rt_fp_stat_total(void);         /* 站20:两核求和(fpstat 显示面) */

/* rt_main.c */
rt_task_t *rt_irq_dispatch(void);  /* vec.S bl 目标:返回 0=续跑,否则=新任务 */
void rt_idle_loop(void) __attribute__((noreturn));

/* vec.S:续跑路径经 .Lcoop_ret -> rt_sched 调用点后的正常收尾返回
 * ——绝不能标 noreturn(GCC 会在调用点后排环回头/死代码,续跑即踩;
 * 本站实测:明雷=空指针炸,暗雷=落进 puts_uart 无限打印)。 */
void rt_switch_to(rt_task_t *next);
void rt_frame_init(rt_task_t *t, void *stack_top, void (*entry)(void *), void *arg);

/* rt_sched.c:DAIF 短临界段(第十七站)。save/restore 而非 set/clr——
 * ISR 语境(I=1)restore 原样归还,不泄掩码。 */
u64 rt_daif_save(void);
void rt_daif_load(u64 d);


typedef struct rt_mutex {
    rt_task_t *owner;             /* 持锁者(递归判据) */
    u32 nest;                     /* 同 owner 重入深度 */
    rt_task_t *wq_head;
    rt_task_t *wq_tail;
} rt_mutex_t;

void rt_sem_init(rt_sem_t *s, long init);
long rt_sem_take(rt_sem_t *s);    /* 资源不足则挂等(RT_WAIT_FOREVER 薄壳) */
long rt_sem_give(rt_sem_t *s);    /* count++ + 唤醒队头 */
void rt_mutex_init(rt_mutex_t *m);
long rt_mutex_take(rt_mutex_t *m);/* 空则占;owner 重入 nest++;否则挂等 */
long rt_mutex_give(rt_mutex_t *m);/* 非拥有者 -1;nest 归零让渡+唤醒 */

/* 站二十一:超时族(参考RTOS 公开语义:0=no-wait 单次复核即判负;
 * RT_WAIT_FOREVER=永不超时;其他=绝对截止,take 环被醒后复核,
 * 条件仍不满足按剩余恒以首绝对 deadline 重挂——锚点=调用点) */
#define RT_WAIT_FOREVER (~0UL)
#define RT_TIMEOUT      (-2L)
long rt_sem_take_to(rt_sem_t *s, u64 ticks);        /* 0=取到 RT_TIMEOUT=超时 */
long rt_mutex_take_to(rt_mutex_t *m, u64 ticks);
void rt_timeout_unpend(rt_task_t *t);               /* 扫描侧:摘链+owner
                                                    * PI 重算(等待集变化
                                                    * 不变量,与 give 同律) */
void rt_pend_disarm(rt_task_t *t);                  /* 成功出口缴械挂等
                                                    * 登记(评审核:stale
                                                    * 武装穿透免疫承诺) */

/* 站21B:panic 观测面。扫描面无寄存器行(ESR/ELR 此刻是残值,印了
 * 冒充现场);陷阱面 ESR/ELR/FAR 架构有效才印。皆 noreturn。 */
void rt_panic(const char *why, unsigned int slot) __attribute__((noreturn));
void rt_panic_fault(const char *why) __attribute__((noreturn));

/* 站21C:栈限位导出(pool/task_stacks 私有 static,demo 经此取址)。
 * NULL=非池任务(idle/引导语境),demo 自守。 */
void *rt_cur_stack_limit(void);    /* 可用区底(哨兵上方) */
void *rt_cur_stack_floor(void);    /* 垫内深限(越哨兵受控止点) */
u32   rt_cur_stack_margin(unsigned int slot);  /* stackcheck/ps 用;0=槽空 */
int   rt_stack_slot_ok(unsigned int slot);    /* 1=哨兵完好 0=已坏
                                               * -1=槽未创建 */

/* 站19B 起导出(原 static):rt_ipc.c 队列同机器;站21A 起挂等并入
 * take 族单窗形态,rt_pend 退役,wq_push 随 _to 族一并导出 */
void rt_wq_push(rt_task_t **head, rt_task_t **tail, rt_task_t *t);
void rt_handoff(rt_task_t **head, rt_task_t **tail);

/* rt_ipc.c(第十九站B):事件组+消息队列。事件组=每事件条件注册表
 * (任务链挂不下 want/and_mode),set 扫表只摘满足者;队列=站 17
 * 原样 FIFO 等待队列机器(满发挂 tx 队,空收取 rx 队)。 */
#define RT_QCAP 8u                 /* 环容量上限(demo 尺度) */

typedef struct rt_event_waiter {
    rt_task_t *t;
    u32 want;
    int and_mode;
} rt_event_waiter_t;

typedef struct rt_event {
    u32 flags;
    unsigned int nw;                           /* 注册表在用数 */
    rt_event_waiter_t wait[RT_POOL_MAX];       /* 条件注册表(池即容量) */
} rt_event_t;

typedef struct rt_queue {
    u64 buf[RT_QCAP];
    u32 head, count, cap;                      /* 尾=(head+count)%cap,免冗余 */
    rt_task_t *rx_h, *rx_t;                    /* 空取等待队列 */
    rt_task_t *tx_h, *tx_t;                    /* 满发等待队列 */
} rt_queue_t;

void rt_event_init(rt_event_t *e);
int rt_event_wait(rt_event_t *e, u32 want, int and_mode, u32 *got); /* 命中即耗 */
void rt_event_set(rt_event_t *e, u32 bits);    /* |= 且扫表醒满足者 */
void rt_queue_init(rt_queue_t *q, u32 cap);    /* cap<=RT_QCAP,0 拒 */
int rt_queue_send(rt_queue_t *q, u64 msg);     /* 满则挂发队,醒后复核 */
int rt_queue_recv(rt_queue_t *q, u64 *msg);    /* 空则挂取队,醒后复核 */
int rt_queue_send_to(rt_queue_t *q, u64 msg, u64 ticks);   /* 站21A(超时族,
                                   * 定义在 rtos.h 同步节:rt_queue_t 此处
                                   * 才完成定义) */
int rt_queue_recv_to(rt_queue_t *q, u64 *msg, u64 ticks);

/* rt_demo.c(第十七站):演示即判据 */
void rt_demo_sem(void);
void rt_demo_mutex(void);
void rt_demo_pi(void);           /* 站十八B:PI 饥饿反转 */
void rt_demo_fpdual(void);       /* 站十八C:per-task FP 并发自证 */
void rt_demo_event(void);
void rt_demo_smp(void);              /* 站二十:双核计数交错 */
void rt_demo_ipi(void);              /* 站二十:SGI 跨核唤醒全链 */
void rt_demo_fpsmp(void);            /* 站二十:per-CPU FP 双精确 */
void rt_demo_queue(void);        /* 站十九B:16 条收发+满环阻塞 */
void rt_demo_tmo(void);              /* 站21A:超时面全弧(no-wait/超时/
                                     * 在时醒/PI boost-回收) */
void rt_demo_stk(void);              /* 站21C:受控深潜(水位判据) */
void rt_demo_overflow(void);         /* 站21C:受控越哨→panic([74]) */

/* rt_timer.c(站二十二B):软定时器。==== 回调语境契约(评审核折入,
 * 参考RTOS 反转警示:ktimer 回调跑在 timer 任务语境可阻塞,本设计是
 * ISR 语境,直译即灾难)====:回调=核 0 tick-ISR(I=1),rt_cur 是
 * 被打断任务不属于回调。允许:rt_puts/rt_puts_u64(持锁即屏本核)/
 * rt_ready_push(限 SLEEP 态唤醒)/rt_timer_stop(含自停)。禁止:
 * 一切挂起 rt_cur 的调用(sleep/take 族——ISR 帧内 rt_sched 换帧
 * =EOI 永不执行=核 0 中断面死);rt_sem_give/rt_event_set(尾段
 * rt_sched 同病);rt_timer_start(同环重入)。
 * ==== 登记契约:==== start/stop 仅核 0 语境(同核序贯免锁;跨核
 * 需池自旋锁,记残余);结构体须常驻存储且活过回调本体(单发回调
 * 执行时 deadline 已清 0);重复 start=改期不双登;stop 幂等。
 * start 返 0 成功/-1 池满或 ticks==0(可观测拒绝)。 */
#define RT_TMAX 8u

typedef struct rt_timer {
    u64 deadline;                     /* 绝对截止;0=停(0=哨兵同 wake_tick) */
    u64 period;                       /* 0=单发;>0=周期,零漂移重臂 */
    void (*fn)(void *);
    void *arg;
} rt_timer_t;

void rt_timer_init(rt_timer_t *t, void (*fn)(void *), void *arg);
int  rt_timer_start(rt_timer_t *t, u64 ticks, int periodic);
void rt_timer_stop(rt_timer_t *t);
void rt_timer_tick(void);            /* 核 0 单写者扫描:到期即回调 */

/* 站42:posix timer v0(全局池 8 槽;骑站 20 软定时器)。回调语境=
 * 禁调度——投递走 sig 位图+无 rt_sched 唤醒版;到期=SIGEV_THREAD_ID
 * 任务级 sigevent(v0 面单形);itimer(ITIMER_REAL)同池骑坐。 */
struct rt_ptimer {
    rt_timer_t tm;              /* 站 20 机器(armed 由 tm 自证) */
    struct rt_proc *owner;
    int slot;
    int armed;
    u64 interval_ns;            /* it_interval(0=单发) */
    int signo;                  /* sigev_signo */
    u64 value;                  /* sigev_value */
    u64 target_tid;             /* SIGEV_THREAD_ID 目标(0=leader) */
    u32 is_itimer;              /* 1=ITIMER_REAL 骑坐(到 leader,SIGALRM) */
    u32 overrun;
    u32 firing;                  /* 回调在役:pengding 未收即过期++ */
};


void rt_demo_swt(void);              /* 站22B:swtdemo 判据 */

/* rt_main.c(站23A):irq handler 注册表。替换式登记(一 intid 一槽,
 * 重复登记=替换,返回旧 isr 供链式包装/恢复);IAR→查表→isr 先行,
 * 掩码后醒(rt_task_wake_all(mask)——RT_RX_BIT 泛化至此);无登记
 * intid 一行报(默认拒绝面诚实化)。登记窗口期(替换瞬间)同核
 * 序贯,IRQ 走新旧槽皆可,语义无害。 */
#define RT_IRQ_MAX 8
void (*rt_irq_register(u32 intid, void (*isr)(void), u32 wake_mask))(void);
void (*rt_irq_swap_uart(void (*isr)(void)))(void);  /* 站23A:33 号槽
                                   * 换钩助手(demo 面;掩码恒 RT_RX_BIT) */

/* 站23B:水位守卫预算(评审 disassembly 定谳账目)。守卫链=vec.S
 * 异常帧 272+dispatch 48+isr_vtimer 0(尾调)+bookkeep 64=384
 * ——恰 RT_ISR_BUDGET;sp(真值)低于可用底+384 时 ISR 链写到
 * magic 区,下一拍哨兵扫描即 panic(**任务真 sp 系**危险线)。
 * 守卫告警线=危险线+读点差 D≈350→734 取整 640——**守卫读点系**
 * (读点在真 sp 下方 D);打印链(组串后单 rt_puts)实测 ~160B
 * 自读点向下,640−384=256 即其+帧漂移余量。两线分属两坐标系,
 * 勿混比(评审余量算术定谳:旧注"打印链 ~100B"实测 160,已正)。 */
#define RT_ISR_BUDGET  384u
#define RT_GUARD_LINE  640u

void rt_demo_irq(void);              /* 站23A:irqdemo(注册表活证) */
void rt_demo_low(void);              /* 站23B:lowdemo(贴线守卫) */

#endif /* RTOS_H */
