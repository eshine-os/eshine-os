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

rt_task_t *rt_task_create_cpu(const char *name, void (*entry)(void *),
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
     * =复用槽首个挂等假超时;wait_mask/wq_next/wake_tick 旧三件沿用) */
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
    t->state = RT_READY;
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
    unsigned int slot = pool_slot_of(t);

    return slot < RT_POOL_MAX ? (u64)(slot + 1) : 0;
}

/* 站二十八:EL0 用户任务——内核帧/栈就绪后由 vec.S rt_frame_init_el0
 * 锻首帧(psr=EL0t,elr=entry 直达);el0 旗置位(tick 栈守卫对
 * 用户栈不适用——守卫读点在内核语境,lim/ksp 二坐标系混用会假阳,
 * 豁免并记档)。 */
rt_task_t *rt_task_create_el0(const char *name, void (*entry)(void *),
                              void *arg, u32 prio, void *ustack_top)
{
    rt_task_t *t = rt_task_create_cpu(name, entry, arg, prio, rt_cpu_id());

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
    rt_cur->state = RT_BLOCK;
    rdy_remove(rt_cur);              /* 站19:同上,真阻塞必清令牌 */
    rt_daif_load(d);                /* 窗闭合:此后 ISR 见 BLOCK 即正确唤醒 */
    rt_sched();
}

/* 站38 定谳:entry!=NULL 是站16 帧自铸时代的活判据,站29 起的
 * fork/clone 子恒 entry==NULL(帧改写形)——四条走查(唤醒全/睡醒/
 * 超时到账/栈哨兵)一直把它们排除在外。首个自然睡的 fork 子
 * (sigqdemo)睡死定谳。活判据=状态面(站35 RT_FREE 同族)。 */
#define TASK_LIVE(t) ((t)->state != RT_FREE && (t)->state != RT_DEAD)

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

void rt_task_wake_sleepers(void)
{
    unsigned int i;

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
