/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "rtos.h"
#include "miniuart.h"          /* 站21B:rt_panic 裸打面 */

/* 任务池(静态,无运行时分配器——YAGNI):8 槽 + 各 4KB 栈。
 * idle 是引导语境就地转正的伪任务,不占池。 */

rt_task_t *volatile rt_cur_arr[RT_NCPU];   /* 站二十:per-CPU(rtos.h 宏保旧名) */
volatile u64 rt_ticks;

static rt_task_t pool[RT_POOL_MAX];

/* 站35:素槽显式化——bss 零态与 RT_READY 同貌,槽判升维后须
 * 开机一遍清池置 RT_FREE(rt_syscall_init 首调,先于一切建池)。 */
void rt_task_pool_init(void)
{
    unsigned int i;

    for (i = 0; i < RT_POOL_MAX; i++)
        pool[i].state = RT_FREE;
}

/* 站28:tp[] 完整性守卫要判合法池域,池基出口
 * (只读访问,写仍限本文件) */
rt_task_t *rt_pool_base(void)
{
    return pool;
}

/* 站二十八:EL0 异常蹦床(每核 32B:SP_EL1 域 scratch 存取;boot/次核
 * 各置 SP_EL1=本核元素尾)+每池槽 4KB 内核栈(EL0 任务的帧栈/系统
 * 调用阻塞续跑栈;EL1 任务不入此面)。 */
u64 rt_el0_tramp[RT_NCPU][4] __attribute__((aligned(16)));
static u64 rt_kstacks[RT_POOL_MAX][512] __attribute__((aligned(16)));
/* 站21C:栈行=4096B 可用区 + 哨兵 + 牺牲垫(行距 4368=16×273,
 * 行内 16 对齐保持)。升址:[0..263]=垫(overflower 越哨兵的受控
 * overshoot 预算,~200B+余量;深 sp 处不留栈——异常帧永远不在垫区)
 * [264..271]=magic u64 [272..4367]=可用栈 4096B(顶=行尾,语义不变)。 */
static u8 task_stacks[RT_POOL_MAX][4368] __attribute__((aligned(16)));
#define STK_MAGIC_OFF 264u
#define STK_USABLE    272u
#define RT_STACK_MAGIC 0x544B434753544B52ULL   /* "TKGSTKR" 常量,免与
                                                * 0xA5 水位模式/代码字节撞车 */
static rt_task_t idle_task;

static rt_task_t *task_alloc_kernel(const char *name, void (*entry)(void *),
                             void *arg, u32 prio, u32 cpu)
{
    unsigned int i, slot;
    rt_task_t *t = NULL;

    for (i = 0; i < RT_POOL_MAX; i++) {
        /* 站35:槽复用判=FREE/DEAD 态判。旧判 entry==NULL 把活的
         * fork/clone 子(帧改写形状 entry 恒空)当空槽整 struct
         * 复用——pt leader 被后到 clone 覆写,无声蒸发(gdb 池像
         * 定谳:"thr"落 leader 槽 tid=0x101)。素槽由开机清池显式
         * 置 RT_FREE(bss 零态与 RT_READY 同貌,不可再当判据)。 */
        if (pool[i].state == RT_FREE || pool[i].state == RT_DEAD) {
            t = &pool[i];
            slot = i;              /* 栈下标=池槽位,与名字长度无关
                                     * (站 16 潜伏 bug:名字循环曾复用
                                     * i,frame_init 拿到的是名字长度当
                                     * 栈下标——shell 住 stack[5]、
                                     * heartbeat 住越界 stack[9],全靠
                                     * 名字长度不撞车;站 17 首对同长并
                                     * 发任务 producer/consumer 双双 8 字
                                     * 共享 0xd430 帧,当场对撞炸出) */
            break;
        }
    }
    if (!t)
        return NULL;                /* 池满:调用方自报(门禁不触发) */
    for (i = 0; i < sizeof(t->name); i++)
        t->name[i] = 0;               /* 站35:整域清零(旧值残留致
                                        * ps 出 "thr\0d" 类嵌合名) */
    for (i = 0; i < sizeof(t->name) - 1 && name[i]; i++)
        t->name[i] = name[i];
    /* 站21C:哨兵+水位先行,entry 发布殿后(同函数序贯=同核扫描
     * (ISR I=1)见 entry 即见完好 magic——半开槽不可见);水位
     * 填充先于 frame_init(frame_init 会写顶部帧,恰是首个非 0xA5) */
    *(u64 *)&task_stacks[slot][STK_MAGIC_OFF] = RT_STACK_MAGIC;
    {
        u64 *w = (u64 *)&task_stacks[slot][STK_USABLE];
        u64 *end = (u64 *)&task_stacks[slot][sizeof(task_stacks[slot])];

        while (w < end)
            *w++ = 0xA5A5A5A5A5A5A5A5ULL;
    }
    /* 站21A:复用槽必清清单(评审核 blocker:残留 stale deadline
     * =复用槽首个挂等假超时;wait_mask/wq_next/wake_tick 旧三件沿用)。
     * 站68F root-f:置 RT_CREATING——半创建态守卫族经 TASK_LIVE
     * 免检(同 FREE/DEAD 律),入队闸放行(新生首推合法;旧置
     * RT_FREE 会在 create 尾推时被僵尸闸在锁内置 READY 之前读
     * 出=丢推=initmark 子永不调度=引导停摆,实测定谳)。 */
    t->state = RT_CREATING;
    t->pend_deadline = 0;
    t->pend_qh = NULL;
    t->pend_qt = NULL;
    t->pend_owner = NULL;
    t->timed_out = 0;
    t->low_warned = 0;              /* 站23B:守卫一次性旗,复用槽必清 */
    t->ttbr = 0;                    /* 站29:0=内核 PGD;EL0 任务由
                                      * 进程创建路径(proc_boot_shell/
                                      * fork)另赋——复用槽必清,
                                      * 站16 帧复用教训同律 */
    t->proc = NULL;
    t->entry = entry;
    t->arg = arg;
    t->prio = prio < RT_NPRIO ? prio : RT_NPRIO - 1;
    t->base_prio = t->prio;          /* 站十八B:PI 基准 */
    /* 站68D:创建窗孤儿假阳性根除。旧码在此急置 RT_READY,而原子
     * 入队(rt_ready_push→rdy_enqueue_core 状态+入队同锁窗)远在
     * frame_init 之后(本函数末)——syscall 语境 DAIF 开(站28),
     * tick 扫描落"READY 已置/尚未入队"窗即见 READY 孤儿(谓词③),
     * noreturn panic 反杀健康靴(实测 fork 子 child slot5 每 3/8 靴
     * 预 governed 毒发,ticks 11-50)。修=创建窗内恒置 RT_FREE
     * (TASK_LIVE 假→扫描全谓词跳过;分配判 FREE/DEAD 故不双认领,
     * 创建实序贯无双认领面),READY 唯一由末尾 rt_ready_push 原子
     * 置+入队——状态可见即可达,孤儿窗彻底闭合(68B 同律上移创建面)。
     * 站68F root-f:窗口态改 RT_CREATING——半创建与退池尸在状态上
     * 分家:僵尸入队闸拒 DEAD/FREE,新生态放行(旧置 FREE 时新生
     * 首推被闸在锁内置 READY 之前读出=丢推,initmark 子永不调度=
     * 引导停摆实测)。守卫/扫描性质全承 RT_FREE 旧律:TASK_LIVE
     * 假(排除清单已加)、分配判 FREE/DEAD 不双认领。 */
    t->state = RT_CREATING;
    t->wait_mask = 0;
    t->wq_next = NULL;            /* 站十七:同步队列链,复用槽必清 */
    t->wake_tick = 0;
    t->slice_left = RT_SLICE;
    t->sig_pending = 0;             /* 站38:任务级信号面,复用槽必清
                                      * (站16 帧复用教训同律:新域
                                      * 入槽清单) */
    t->sig_val = 0;
    t->tid = 0;                     /* 站40 定谳([95] 十案):tid 域
                                      * 站35 入槽时漏清单——复用槽残
                                      * tid=亡 fork 子的 pid,活驻任
                                      * 务(deepdiver)顶名截走
                                      * sigqueueinfo 的 tid 定向
                                      * (电池序才撞:孤立跑任务位
                                      * 干净不现;站38 律第四现)。 */
    t->wait_src = RT_WSRC_NONE;     /* 站43:归属列,复用槽必清(站38 律) */
    t->cpart = 0;                   /* 站45:CPU 分区(0=system,复用槽
                                     * 必清——站38 律第五案预防位) */
    t->el0 = 0;                     /* 站40 定谳(12 站潜伏):el0 域
                                      * 站28 入槽时漏清单——boot 期
                                      * initmark/beat 子(首批早期
                                      * el0 亡者)残值 1 被 deepdiver
                                      * 复用,rt_cur_stack_limit 判
                                      * el0 返 NULL=受控深潜静默不
                                      * 潜,水位门红([41] 定谳面)。
                                      * 站38 律第三现:新域入槽清单。 */
    t->cpu = (cpu < RT_NCPU) ? cpu : rt_cpu_id();
                                   /* 站二十:建核即亲和(不迁移,YAGNI);
                                    * smpdemo/ipidemo/fpsmp 显式指定对核 */
    rt_frame_init(t, task_stacks[slot] + sizeof(task_stacks[slot]), entry, arg);
    return t;        /* 站68E:不入队——调用方完全设置后才 rt_ready_push */
}

/* 站68E:内核任务创建=分配+建帧(task_alloc_kernel,留 RT_FREE 不入队)
 * 后立即入队。EL0 任务走 rt_task_create_el0(先建 EL0 帧+置 el0 旗
 * 再入队)。旧 create_el0 先调 create_cpu(已入队 READY/内核帧/el0=0)
 * 再改 el0+重建 EL0 帧——留"已入队 READY 但 el0=1/sp 指内核栈/ksp=0"
 * 半创建窗:tick 扫描落窗即谓词① 假阳(实测 fork 子 child slot5
 * sp 越 rt_kstacks 域+ksp=0,ticks 2 预 governed 毒发)。入队移至
 * 全部设置之后,半创建态恒 RT_FREE(扫描不可见)→窗彻底闭合。 */
rt_task_t *rt_task_create_cpu(const char *name, void (*entry)(void *),
                              void *arg, u32 prio, u32 cpu)
{
    rt_task_t *t = task_alloc_kernel(name, entry, arg, prio, cpu);

    if (t)
        rt_ready_push(t);
    return t;
}

rt_task_t *rt_task_create(const char *name, void (*entry)(void *),
                          void *arg, u32 prio)
{
    return rt_task_create_cpu(name, entry, arg, prio, rt_cpu_id());
}

/* 站28:pid=池槽+1(id 面与参考实现 idr 编号同精神:小整数,稳定,
 * 供 getpid 与 panic/取证指名)。 */
static unsigned int pool_slot_of(const rt_task_t *t);

u64 rt_pid_of(rt_task_t *t)
{
    unsigned int slot;

    /* 站42 定谳(getpid 首个 fork 子受害者实抓):槽号≠pid——壳
     * 28 站全绿只因任务槽 0 恰等于进程 pid 1;真 pid 在 proc 面
     * (站 29 起),EL0 任务恒有 proc,内核任务(服务器)无 proc
     * 退回槽号面(终态标签/调试面语义不变)。 */
    if (t && t->proc)
        return (u64)t->proc->pid;
    slot = pool_slot_of(t);
    return slot < RT_POOL_MAX ? (u64)(slot + 1) : 0;
}

/* 站二十八:EL0 用户任务——内核帧/栈就绪后由 vec.S rt_frame_init_el0
 * 锻首帧(psr=EL0t,elr=entry 直达);el0 旗置位(tick 栈守卫对
 * 用户栈不适用——守卫读点在内核语境,lim/ksp 二坐标系混用会假阳,
 * 豁免并记档)。 */
rt_task_t *rt_task_create_el0(const char *name, void (*entry)(void *),
                              void *arg, u32 prio, void *ustack_top)
{
    rt_task_t *t = task_alloc_kernel(name, entry, arg, prio, rt_cpu_id());

    if (!t)
        return NULL;
    t->el0 = 1;
    {
        u32 slot;

        for (slot = 0; slot < RT_POOL_MAX; slot++)
            if (&pool[slot] == t)
                break;
        /* 帧锻调用契约:x0=task x1=kstack_top x2=ustack_top
         * x3=entry x4=arg(vec.S rt_frame_init_el0) */
        {
            register void *x0 __asm__("x0") = t;
            register void *x1 __asm__("x1") = rt_kstacks[slot] + 512;
            register void *x2 __asm__("x2") = ustack_top;
            register void (*x3)(void *) __asm__("x3") = entry;
            register void *x4 __asm__("x4") = arg;

            __asm__ volatile("bl rt_frame_init_el0"
                             :
                             : "r"(x0), "r"(x1), "r"(x2), "r"(x3),
                               "r"(x4)
                             : "x30", "memory");
        }
    }
    rt_ready_push(t);   /* 站68E:EL0 帧+el0 旗全就位后才入队(消灭
                         * 半创建窗:入队即可调度/可扫描,故必待帧成) */
    return t;
}

void rt_task_exit(void)             /* noreturn:DEAD 后必切走 */
{
    rt_task_t *next;

    rt_cur->state = RT_DEAD;
    rdy_remove(rt_cur);              /* 站19:清自拣残令牌——否则摘头会
                                      * 摘到自己,DEAD 被复活的假醒 */
    if (rt_fp_owner == rt_cur)       /* 站19C:持权者退场即弃权——槽位
                                      * 会被 create 复用,stale owner 指
                                      * 向复用后的活结构会错存帧 */
        rt_fp_owner = NULL;
    next = rt_pick_ready();
    if (!next)
        rt_idle_loop();             /* 理论兜底:全灭时回 idle 轮询 */
    next->state = RT_RUN;
    rt_switch_to(next);              /* 本任务 DEAD,永不续跑;堵返回路径 */
    __builtin_unreachable();
}

void rt_sleep_ticks(u64 n)
{
    rt_cur->wake_tick = rt_ticks + n;
    rt_cur->wait_src = RT_WSRC_SLEEP;  /* 站43:归属列 */
    rt_cur->state = RT_SLEEP;
    rdy_remove(rt_cur);              /* 站19:真挂睡前清自拣令牌——不清则
                                      * rt_sched 摘到自己即"醒",睡不
                                      * 落地变忙自旋,饿死低优先就绪者 */
    rt_sched();                      /* 让位;醒来从此处继续 */
}

void rt_block_rx(void)
{
    u64 d = rt_daif_save();

    /* 站20 [41] 闪断定谳:壳在 rt_getc_nb 判空与发布 BLOCK 之间存在
     * 无屏窗——UART ISR 恰落窗内:字节已入 rrx 环,而 wake_all 只认
     * BLOCK 态,RUN 态任务收到唤醒即吞,壳照旧挂起。末字节入环而壳
     * 永睡,vCPU 心跳照跳 = 门禁 [37] "token 单见" 的全息画像(原生
     * 唤醒源只有后续 IRQ,末字节无后继,永不自愈)。判空+发布+摘队
     * 同入 DAIF 屏蔽窗:窗内终判环已非空则不挂起,回调用者重查即得;
     * 窗后挂起已可见,ISR 唤醒走回流令牌兑付(rt_sched 自拣支)。 */
    if (rt_rx_pending()) {
        rt_daif_load(d);
        return;                      /* 迟到字节:不挂起,壳循环顶再取 */
    }
    rt_cur->wait_mask = RT_RX_BIT;
    rt_cur->wait_src = RT_WSRC_RX;  /* 站43:归属列 */
    rt_cur->state = RT_BLOCK;
    rdy_remove(rt_cur);              /* 站19:同上,真阻塞必清令牌 */
    rt_daif_load(d);                /* 窗闭合:此后 ISR 见 BLOCK 即正确唤醒 */
    rt_sched();
}

/* 站38 定谳:entry!=NULL 是站16 帧自铸时代的活判据,站29 起的
 * fork/clone 子恒 entry==NULL(帧改写形)——四条走查(唤醒全/睡醒/
 * 超时到账/栈哨兵)一直把它们排除在外。首个自然睡的 fork 子
 * (sigqdemo)睡死定谳。活判据=状态面(站35 RT_FREE 同族)。 */
#define TASK_LIVE(t) ((t)->state != RT_FREE && (t)->state != RT_DEAD && \
                      (t)->state != RT_CREATING)

void rt_task_wake_all(u32 mask)
{
    unsigned int i;

    for (i = 0; i < RT_POOL_MAX; i++) {
        rt_task_t *t = &pool[i];

        if (TASK_LIVE(t) && t->state == RT_BLOCK && (t->wait_mask & mask)) {
            t->wait_mask = 0;
            rt_ready_push(t);
        }
    }
}

void rt_task_wake_sleepers(void);
static void rt_stack_poison(unsigned int i, rt_task_t *t, u64 lo, u64 hi)
    __attribute__((noreturn));
static void rt_queue_poison(int c, int p, unsigned long v1,
                            unsigned long v2)
    __attribute__((noreturn));

void rt_task_wake_sleepers(void)
{
    unsigned int i;

    /* 站68:队链全审计先于槽检(每拍一次)——孤儿谓词(第③)只
     * 说"不可达",审计四元先开口:链野/幽灵/环/尾脱链各归其
     * 类+层号+双值(尾脱链的 v1=登记尾 v2=实走尾=两任尾主,
     * 写手侧写在值里)。 */
    {
        int gc, gp;
        unsigned long v1, v2;

        if (rt_rdy_audit(&gc, &gp, &v1, &v2))
            rt_queue_poison(gc, gp, v1, v2);
    }
    for (i = 0; i < RT_POOL_MAX; i++) {
        rt_task_t *t = &pool[i];

        if (TASK_LIVE(t) && t->state == RT_SLEEP &&
            ((long long)rt_ticks - (long long)t->wake_tick) >= 0)
            rt_ready_push(t);
        /* 站21A:超时挂等到账。哨兵门 pend_deadline!=0——wait_mask
         * 型(壳 RX)与事件注册表型(不挂 wq)的 BLOCK 一律免疫
         * (评审核:假醒热自旋/rdy 双份入队);有符号惯用法同上
         * (u64 直减下溢=永不超时)。摘链+owner PI 重算同在
         * rt_timeout_unpend(ISR 语境自带互斥,demo 全核 0 亲和)。 */
        if (TASK_LIVE(t) && t->state == RT_BLOCK && t->pend_deadline &&
            (long long)rt_ticks - (long long)t->pend_deadline >= 0) {
            rt_timeout_unpend(t);
            t->pend_deadline = 0;
            t->timed_out = 1;
            rt_ready_push(t);
        }
        /* 站21C:栈哨兵——只查已创建槽(评审核 blocker:未创建槽
         * magic 是 bss 垃圾,首 tick 必假 panic)。坏即 panic 指名
         * (扫描面:无寄存器行,ESR 此刻是残值)。 */
        if (TASK_LIVE(t) &&
            *(volatile u64 *)&task_stacks[i][STK_MAGIC_OFF] != RT_STACK_MAGIC)
            rt_panic("stack overflow", i);
        /* 站68:栈域双守卫(58 挂脸族全案取证的仪器面)。活体标本
         * 三脸解剖定谳:壳 slot 的 sp 字段落 idle 引导栈+蹦床 scratch
         * 域(交叉污染帧:x30 槽被 scratch 任务指针毒化,恢复即 ret
         * 进池数据执行——SP0 同步槽=guest_park 静默楔/SP_ELx 槽=
         * rt_sync_fault panic 两脸一dễ;fork/exec 子的 ksp 毒化则令
         * RT_EL0_ENTER 把 272B 帧推到蹦床域,scratch stp 再覆帧尾
         * x27/x29/x30 三槽),主核楔死=vtimer 停走=rt_ticks 冻结=
         * 一切 sleep_ticks 永睡=boot 无声死/交互死全族。守卫谓词:
         * ① sp 必落自身栈域(EL0=本槽内核栈 4KB;内核=本槽栈行)
         * ② EL0 子的 ksp 必恰等于本槽内核栈顶(创建时一次性写入,
         * 变值即毒)。案发 tick 级指认:受害者名/sp/ksp/期望域 +
         * 双核 cur——越域值落谁家即污染者线索。同栈哨兵语境:扫描
         * 面无寄存器行;panic 纪律(全掩+单次+裸 uart)沿用。 */
        if (TASK_LIVE(t)) {
            u64 lo = t->el0 ? (u64)&rt_kstacks[i][0]
                            : (u64)&task_stacks[i][0];
            u64 hi = lo + (t->el0 ? sizeof(rt_kstacks[i])
                                  : sizeof(task_stacks[i]));

            if (t->sp < lo || t->sp >= hi ||
                (t->el0 && t->ksp != (u64)&rt_kstacks[i][0] +
                                         sizeof(rt_kstacks[i])))
                rt_stack_poison(i, t, lo, hi);
            /* 站68 第三谓词:READY 必达其队(孤儿守卫)——beater 脸
             * 正身:child 被醒(READY)而队链丢失,调度永不再选,
             * 内核健康空转到地老天荒。孤儿=队操作在 fork/exec churn
             * 下丢链的直接证词。 */
            if (t->state == RT_READY && !rt_rdy_find(t)) {
                /* 站68D:跨核陈旧读复检——他核 pick 可能在初读 READY
                 * 之后摘链置 RUN(rt_pick_ready 锁内原子),find 失败仅
                 * 证明"走链瞬间不在队",不足以定孤儿;复读 state 仍
                 * READY 才是真孤儿(volatile 防编译器缓存初读)。 */
                if (*(volatile u32 *)&t->state == RT_READY)
                    rt_stack_poison(i, t, lo, hi);
            }
        }
    }
}

/* ---- 站21B:panic 观测面 ----
 * 入口先封全掩(陷阱路径 sync fault 不自带掩,打印中可再入 tick
 * 二次 panic);单次打印保证(panic_done):扫描路径每 tick 重入,
 * 无旗即无限复读 banner。裸 puts_uart——持锁任务被打断的 tick 语境
 * 取 puts_lk=同核自死锁(自旋锁契约)。两副面孔:扫描面不印寄存器
 * (ESR/ELR 此刻是残值,印了冒充现场);陷阱面 ESR/ELR/FAR 架构
 * 有效才印。皆 noreturn:当前核就地 wfi 终态(他核照跑,宿主可
 * vmkill 收场——[74] 判据即此路)。 */
static int panic_done;

static void panic_park(void) __attribute__((noreturn));
static void panic_park(void)
{
    /* 站28 落点加固(同 rt_sched 空转 wfi):宿主泵"停泊双重
     * 推进"陈疾可能以 +8 复活,+8 须落在受控 nop 上——裸 for(;;)
     * wfi 的 +8 落进相邻代码(冒烟实测定谳),尸身也保整 */
    for (;;)
        __asm__ volatile("wfi\n\t"
                         "nop\n\t"
                         "nop" ::: "memory");
}

void rt_panic(const char *why, unsigned int slot)
{
    if (!panic_done) {
        panic_done = 1;
        __asm__ volatile("msr daifset, #0xf");
        puts_uart("rtos panic: ");
        puts_uart(why);
        puts_uart("\ntask: ");
        if (slot < RT_POOL_MAX && pool[slot].entry)
            puts_uart(pool[slot].name);
        else
            puts_uart("?");
        puts_uart(" (cur ");
        puts_uart(rt_cur->name);
        rt_puts_hex(", cpu 0x", rt_cpu_id(), ")\ncontext: tick scan\n");
    }
    panic_park();
}

/* 站68:守卫 panic——证据面:受害者五元(slot/名/态/el0/sp/ksp/
 * 期望域)+双核 cur+ticks。毒值越谁的域=污染者在谁家语境,当场
 * 指认。纪律同 rt_panic:全掩+单次+裸 uart+park。 */
static void rt_stack_poison(unsigned int i, rt_task_t *t, u64 lo, u64 hi)
{
    if (!panic_done) {
        panic_done = 1;
        __asm__ volatile("msr daifset, #0xf");
        puts_uart("rtos panic: stack poison (station 68 guard)\n");
        puts_uart("task: ");
        puts_uart(t->name);
        rt_puts_hex(" slot ", i, "");
        rt_puts_hex(" st 0x", t->state, "");
        rt_puts_hex(" el0 ", t->el0, "\n");
        rt_puts_hex("sp 0x", t->sp, "");
        rt_puts_hex(" dom 0x", lo, "");
        rt_puts_hex("-0x", hi, "");
        rt_puts_hex(" ksp 0x", t->ksp, "\n");
        rt_puts_hex("cur0 0x", (u64)rt_cur_arr[0], "");
        rt_puts_hex(" cur1 0x", (u64)rt_cur_arr[1], "");
        rt_puts_hex(" ticks ", rt_ticks, "\n");
    }
    panic_park();
}

/* 站68:队列毒 panic——泼溅源头在场者(第四谓词的收口面):毒值
 * 落哪一层(c/p)、值本身=谁家地址(写手侧写)。纪律同上。 */
static void rt_queue_poison(int c, int p, unsigned long v1,
                            unsigned long v2)
{
    if (!panic_done) {
        panic_done = 1;
        __asm__ volatile("msr daifset, #0xf");
        puts_uart("rtos panic: queue poison (station 68 guard)\n");
        rt_puts_hex("cpu ", (u64)c, "");
        rt_puts_hex(" prio ", (u64)p, "");
        rt_puts_hex(" v1 0x", v1, "");
        rt_puts_hex(" v2 0x", v2, "");
        rt_puts_hex(" pool 0x", (u64)rt_pool_base(), "");
        rt_puts_hex("-0x", (u64)rt_pool_base() + RT_POOL_MAX, "\n");
        rt_puts_hex("cur0 0x", (u64)rt_cur_arr[0], "");
        rt_puts_hex(" cur1 0x", (u64)rt_cur_arr[1], "");
        rt_puts_hex(" ticks ", rt_ticks, "\n");
    }
    panic_park();
}

/* 站68C:簿记域检违例——交叉存/蹦床帧当场实锤。域检在
 * RT_SWITCH_BOOKKEEP 的 asm 内(str x3,[x2] 前查 x3∈[ksp-4096,ksp)),
 * 违例换引导栈调本打印器。site=案发展开点(adr 取自违例展开的
 * 合法续跑址,三展开各唯一;对 objdump/map 即指认 rt_switch_to /
 * guest_irq / guest_el0_irq 中哪一个在交叉存)。纪律同上:全掩+
 * 单次+裸 uart+park。 */
void rt_bookkeep_violation(rt_task_t *old, u64 poison_sp, u64 ksp, u64 site,
                           u64 elr, u64 spsr, u64 next, u64 sp_el1,
                           u64 sp_el0, u64 spsel)
{
    if (!panic_done) {
        panic_done = 1;
        __asm__ volatile("msr daifset, #0xf");
        puts_uart("rtos panic: bookkeep violation (station 68C guard)\n");
        puts_uart("old: ");
        puts_uart(old->name);
        rt_puts_hex(" st 0x", old->state, "");
        rt_puts_hex(" el0 ", old->el0, "\n");
        /* 站68G 第二轮(26.11-3w):true next 身份——rt_switch_to 入口
         * 绊线的裁决面(切向谁/池外值)。 */
        {
            rt_task_t *n = (rt_task_t *)(unsigned long)next;

            if (n >= pool && n < pool + RT_POOL_MAX &&
                ((const char *)n - (const char *)pool) % (long)sizeof(*n) == 0) {
                puts_uart("next: ");
                puts_uart(n->name);
                rt_puts_hex(" st 0x", n->state, "");
                rt_puts_hex(" sp 0x", n->sp, "\n");
            } else {
                rt_puts_hex("next(out-of-pool) 0x", next, "\n");
            }
        }
        rt_puts_hex("site 0x", site, "\n");
        rt_puts_hex(" sp 0x", poison_sp, "");
        rt_puts_hex(" dom 0x", ksp - 4096, "-0x");
        rt_puts_hex("", ksp, "\n");
        /* 站68C-2 全现场四元:被中断 PC/PSTATE+切换目标+案发库。
         * spsr[0]=被中断 SPSel,[3:2]=特权级(M=0 EL0/5 EL1h)——
         * "谁在 EL1+SPSel=1 且 SP_EL1=蹦床残值"当场读出;elr 对
         * map/objdump 即函数名=原罪语境直证。 */
        rt_puts_hex("elr 0x", elr, "");
        rt_puts_hex(" psr 0x", spsr, "");
        rt_puts_hex(" next 0x", next, "");
        rt_puts_hex(" sp_el1 0x", sp_el1, "");
        /* 站68C-5c:bank 三元组终局——SPSel 现值+双 bank 现值:判帧
         * 落何库(sp==sp_el0=SP0 槽;sp==sp_el1=SPx 槽)、另一库何值、
         * psr.SPSel 与现值相悖=换库窗案。 */
        rt_puts_hex(" sp_el0 0x", sp_el0, "");
        rt_puts_hex(" spsel 0x", spsel, "\n");
        /* 站68C-5 毒帧槽位解剖:帧基=毒 sp(x1),被中断上下文的
         * x30@264(链接=wfi 语境被谁调用=幽灵身份直证)、x29@256
         * (帧指针)+ 一级链([fp]=上级 fp,[fp+8]=上级返回址)——
         * rt_cur 失同步镜像下 old 名不可信,帧内调用链才是本人。
         * 站68F:sp 非映射(实测 sp=0x1,僵尸帧消费面)时解剖读
         * 自死=打印半途截断,cur0/cur1/tick 环全失——毒 sp 闸外
         * 跳过帧读,后续双核现场保全。 */
        if (poison_sp >= 0x40000000ULL && poison_sp < 0x80000000ULL) {
            u64 fx30 = *(volatile u64 *)(unsigned long)(poison_sp + 264);
            u64 fx29 = *(volatile u64 *)(unsigned long)(poison_sp + 256);
            u64 up = 0, upret = 0;

            if (fx29 >= 0x40000000ULL && fx29 < 0x40200000ULL) {
                up = *(volatile u64 *)(unsigned long)fx29;
                upret = *(volatile u64 *)(unsigned long)(fx29 + 8);
            }
            rt_puts_hex("fx30 0x", fx30, "");
            rt_puts_hex(" fx29 0x", fx29, "");
            rt_puts_hex(" up 0x", up, "");
            rt_puts_hex(" upret 0x", upret, "\n");
            /* 站68C-12:帧头三槽——初始帧签名(elr/psr/x0:fork 子
             * frame_init_el0 只写此三+零余槽;若 [0]=0/[1]=0 则毒帧
             * =fork 帧手术落蹦床,直指 ct->sp/frame_init 的 x1)。 */
            rt_puts_hex("f.elr 0x",
                        *(volatile u64 *)(unsigned long)poison_sp, "");
            rt_puts_hex(" f.psr 0x",
                        *(volatile u64 *)(unsigned long)(poison_sp + 8), "");
            rt_puts_hex(" f.x0 0x",
                        *(volatile u64 *)(unsigned long)(poison_sp + 16), "\n");
        } else {
            rt_puts("fx* skipped (poison sp unmapped)\n");
        }
        /* 站68C-5b:任务字段五元——old/next 的 sp/usp 现值判幽灵
         * 上栈路径:old->sp=蹦床=臂点已毒(sp 字段复活);old->sp
         * 净而 next->usp=蹦床=.Lpop EL0 路 SP_EL0:=usp 毒化路径
         * (eret→EL0 以蹦床为用户栈);两者皆净=第三路径待排除。 */
        rt_puts_hex("o_sp 0x", old->sp, "");
        rt_puts_hex(" o_usp 0x", old->usp, "");
        rt_puts_hex(" n_sp 0x", next ? *(u64 *)(void *)&((rt_task_t *)next)->sp : 0, "");
        rt_puts_hex(" n_usp 0x",
                    next ? *(u64 *)(void *)&((rt_task_t *)next)->usp : 0, "\n");
        rt_puts_hex("cur0 0x", (u64)rt_cur_arr[0], "");
        rt_puts_hex(" cur1 0x", (u64)rt_cur_arr[1], "");
        rt_puts_hex(" ticks ", rt_ticks, "\n");
        /* 站68G-3:断案槽探针(出生绊线桩写:epilogue 读的 x30 保存槽/
         * x29 槽/检查点 sp)——槽毒=序言保存后被覆写;槽净=x30 另源。 */
        rt_puts_hex("x30slot 0x", rt_x30_probe[0], "");
        rt_puts_hex(" x29slot 0x", rt_x30_probe[1], "");
        rt_puts_hex(" chk_sp 0x", rt_x30_probe[2], "\n");
        /* 站68C-14:tick 环四元历史(旧→新)——蹦床值首现 tick
         * =真凶时刻(采样器驻 rt_tick_bookkeep,spsel 双 ferry)。 */
        {
            extern u64 ghost_ring[16][4];
            extern u32 ghost_ring_idx;
            u32 k;

            for (k = 0; k < 16; k++) {
                u32 s = (ghost_ring_idx + k) & 15u;

                rt_puts_hex("R sp_el1 0x", ghost_ring[s][0], "");
                rt_puts_hex(" sel ", ghost_ring[s][1], "");
                rt_puts_hex(" tp ", ghost_ring[s][2], "");
                rt_puts_hex(" @", ghost_ring[s][3], "\n");
            }
        }
        /* 站68G 第二轮:IRQ 出口轨迹环回放(旧→新):每次 dispatch
         * 的 cur(切换前状态)/pick/是否真切——rt_cur 失同步的时间线。 */
        {
            u32 k;
            u32 n = rt_irq_trace_idx < RT_IRQ_TRACE_N ?
                    rt_irq_trace_idx : RT_IRQ_TRACE_N;

            for (k = n; k > 0; k--) {
                const struct rt_irq_trace_ent *e =
                    &rt_irq_trace[(rt_irq_trace_idx - k) &
                                  (RT_IRQ_TRACE_N - 1)];
                rt_task_t *c = (rt_task_t *)(unsigned long)e->cur;
                rt_task_t *x = (rt_task_t *)(unsigned long)e->next;

                rt_puts_hex("IRQ @", e->tick, "");
                rt_puts_hex(" iar ", e->iar, "");
                puts_uart(" cur ");
                puts_uart(c >= pool && c < pool + RT_POOL_MAX ? c->name : "?");
                rt_puts_hex(" st ", e->cur_state, "");
                puts_uart(" pick ");
                puts_uart(x >= pool && x < pool + RT_POOL_MAX ?
                          x->name : (e->next ? "?" : "null"));
                rt_puts_hex(" st ", e->next_state, "");
                rt_puts_hex(" sw ", e->switched, "\n");
            }
        }
    }
    panic_park();
}

/* 站43:FP 链回溯——崩溃自证的最小仪器(参考实现 calltrace 白描的站内形)。
 * 逐帧走 x29 链([fp]=上级 fp,[fp+8]=返回地址);出文本窗即收+16 帧
 * 保险丝(野帧环帧皆拒)。符号名=后弧(kallsyms);v0 打地址,
 * addr2line 离线译名——机器先能开口,再谈会说话。 */
void rt_calltrace(void)
{
    u64 fp;
    u32 n = 0;

    __asm__ volatile("mov %0, x29" : "=r"(fp));
    puts_uart("calltrace:\n");
    while (fp && n < 16) {
        u64 ret = *(u64 *)(void *)(fp + 8);
        u64 nxt = *(u64 *)(void *)fp;

        if (ret < 0x40000000ULL || ret >= 0x40200000ULL)
            break;                       /* 野帧:出内核文本窗即收 */
        rt_puts_hex("  #", n, " pc=0x");
        rt_puts_hex("", ret, "\n");
        if (nxt <= fp)
            break;                       /* 栈底/环 */
        fp = nxt;
        n++;
    }
    rt_puts_hex("  frames=", n, "\n");
}

void rt_panic_fault(const char *why)
{
    u64 esr, elr, far;

    if (!panic_done) {
        panic_done = 1;
        __asm__ volatile("msr daifset, #0xf");
        __asm__ volatile("mrs %0, esr_el1" : "=r"(esr));
        __asm__ volatile("mrs %0, elr_el1" : "=r"(elr));
        __asm__ volatile("mrs %0, far_el1" : "=r"(far));
        puts_uart("rtos panic: ");
        puts_uart(why);
        puts_uart("\ntask: ");
        puts_uart(rt_cur->name);
        rt_puts_hex(" (cpu 0x", rt_cpu_id(), ")\n");
        rt_puts_hex("esr=0x", esr, " ");
        rt_puts_hex("elr=0x", elr, " ");
        rt_puts_hex("far=0x", far, "\n");
        rt_calltrace();                /* 站43:panic 即回溯(帧机器自证) */
    }
    panic_park();
}

/* ---- 站21C:栈限位/水位导出(pool/task_stacks 私有,demo 经此) ---- */
static unsigned int pool_slot_of(const rt_task_t *t)
{
    if (t < pool || t >= pool + RT_POOL_MAX)
        return RT_POOL_MAX;                    /* idle/引导语境 */
    return (unsigned int)(t - pool);
}

void *rt_cur_stack_limit(void)                 /* 可用区底(哨兵上方) */
{
    unsigned int slot = pool_slot_of(rt_cur);

    /* 站二十八:EL0 任务豁免——其用户栈不在 task_stacks 坐标系,
     * tick 守卫读点在内核语境(sp=kstack),lim 取用户栈底=两坐标
     * 系混用必假阳(站23 教训),返回 NULL 即守卫跳过。EL0 任务
     * 的用户栈水位防护=30+ 站面(登记 spec §5)。 */
    if (rt_cur->el0)
        return NULL;
    return slot < RT_POOL_MAX ? &task_stacks[slot][STK_USABLE] : NULL;
}

void *rt_cur_stack_floor(void)                 /* 垫内深限(越哨兵止点) */
{
    unsigned int slot = pool_slot_of(rt_cur);

    /* 站21 评审:深限 64 覆盖不了一整级递归帧(实测 96B/级),
     * 终帧可越行底写邻槽栈顶。抬到 160=96B 帧+64B 余量,overshoot
     * 止于 [160,264) 垫内。 */
    return slot < RT_POOL_MAX ? &task_stacks[slot][160] : NULL;
}

u32 rt_cur_stack_margin(unsigned int slot)      /* 0=槽空;单位字节 */
{
    u64 *w, *end;

    if (slot >= RT_POOL_MAX || !pool[slot].entry)
        return 0;
    w = (u64 *)&task_stacks[slot][STK_USABLE];
    end = (u64 *)&task_stacks[slot][sizeof(task_stacks[slot])];
    while (w < end && *w == 0xA5A5A5A5A5A5A5A5ULL)
        w++;
    return (u32)((unsigned char *)w - &task_stacks[slot][STK_USABLE]);
}

int rt_stack_slot_ok(unsigned int slot)         /* stackcheck 用:
                                                 * 1=哨兵完好 0=已坏
                                                 * -1=槽未创建 */
{
    if (slot >= RT_POOL_MAX || !pool[slot].entry)
        return -1;
    return *(volatile u64 *)&task_stacks[slot][STK_MAGIC_OFF] == RT_STACK_MAGIC;
}

const char *rt_state_name(u32 s)
{
    switch (s) {
    case RT_READY: return "READY";
    case RT_RUN:   return "RUN";
    case RT_SLEEP: return "SLEEP";
    case RT_BLOCK: return "BLOCK";
    default:       return "DEAD";
    }
}

/* ps 表遍历:池 8 槽,随后 idle 虚拟行(rt_main 转正时已填名) */
rt_task_t *rt_ps_first(unsigned int *idx)
{
    *idx = 0;
    return &pool[0];
}

rt_task_t *rt_ps_next(unsigned int *idx)
{
    (*idx)++;
    if (*idx < RT_POOL_MAX)
        return &pool[*idx];
    if (*idx == RT_POOL_MAX)
        return &idle_task;
    return NULL;
}

/* 站45:池内判定(idle1=池外伪任务,免 CPU 分区记账/免入别人
 * 在跑扫描)。 */
int rt_task_in_pool(rt_task_t *t)
{
    return t >= pool && t < pool + RT_POOL_MAX;
}

int rt_task_slot(rt_task_t *t)
{
    return rt_task_in_pool(t) ? (int)(t - pool) : -1;
}

rt_task_t *rt_idle_task(void)
{
    return &idle_task;
}

/* 站二十:次核 idle(池外伪任务,同引导语境就地转正)。栈 =
 * 链接器 .cpu1stack(次核入口已在用,首个 rt_sched 切走即换任务帧,
 * idle1 转正后仍驻该页)。 */
static rt_task_t idle1_task;

rt_task_t *rt_idle1_task(void)
{
    return &idle1_task;
}
