/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "rtos.h"

/* 位图调度器:32 级优先级,bit p = 就绪位;摘最低位(ctz)= 最高
 * 优先(0 最高)。就绪队列每级一条 FIFO 尾链(非环,摘除简单)。
 * 被 RR 轮转/抢占切走的任务排回本级队尾。
 *
 * 站二十:per-CPU 就绪队列(参考RTOS 真形态,用户四决策之二)——
 * 每核一套位图+32 级 FIFO,任务建时定亲和(t->cpu),本核只摘本队。
 * 跨核入列(核 0 唤醒亲核 1 的睡客)经队列自旋锁互斥 + 发 SGI 让
 * 目标核重调度;本核操作自队无争用(同核互斥仍靠 DAIF 屏,自旋锁
 * 顺带承担)。 */

static rt_task_t *rdy_head[RT_NCPU][RT_NPRIO];
static rt_task_t *rdy_tail[RT_NCPU][RT_NPRIO];
static u32 rdy_map[RT_NCPU];
static unsigned long rdy_lk[RT_NCPU];
static int need_switch[RT_NCPU];
static volatile unsigned long ipi_rx_count[RT_NCPU];
volatile unsigned int rt_ipi_echo;   /* 站二十:ipidemo 回合窗——核 1
                                      * handler 见 1 即回发核 0(spec C1
                                      * 往返拍;核 0 不回,免乒乓)。 */

/* DAIF 短临界段(第十七站):rdy 队是任务上下文与 ISR 出口共用的底座。
 * 唯一交错源是 tick 抢切——两个写者(任务侧入队/handoff 与 ISR 侧醒
 * 睡客/RR 重排)跨指令交错,尾链/位图会把任务挤下链永久丢失(站十六
 * 潜伏竞态,本站收敛)。ISR 语境自带 I=1,save/restore 原样归还。 */
u64 rt_daif_save(void)
{
    u64 d;

    __asm__ volatile("mrs %0, daif\n\t"
                     "msr daifset, #2" : "=r"(d) :: "memory");
    return d;
}

void rt_daif_load(u64 d)
{
    __asm__ volatile("msr daif, %0" :: "r"(d) : "memory");
}

/* 站二十:自旋锁 = DAIF 屏 + 跨核原子互斥(WFE/SEV 对)。同核递归
 * = 死锁(屏着 IRQ 自旋等自己,禁);跨核自旋有界——持锁临界段
 * O(1)/O(32) 且锁内不再取锁。返回屏蔽前 DAIF,unlock 原样归还
 * (ISR 出口语境天然全屏,防误解掩)。 */
u64 rt_spin_lock(unsigned long *lk)
{
    u64 d = rt_daif_save();

    for (;;) {
        unsigned long v, ok;

        __asm__ volatile("ldaxr %0, [%1]" : "=r"(v) : "r"(lk));
        if (!v) {
            __asm__ volatile("stxr %w0, %w2, [%1]"
                             : "=&r"(ok) : "r"(lk), "r"(1UL) : "memory");
            if (!ok)
                return d;
        }
        __asm__ volatile("wfe");
    }
}

void rt_spin_unlock(unsigned long *lk, u64 d)
{
    __asm__ volatile("stlr %0, [%1]" :: "r"(0UL), "r"(lk) : "memory");
    __asm__ volatile("sev");       /* 睡客广播:锁语义 + WFE 自旋唤醒 */
    rt_daif_load(d);
}

/* 跨核重调度:SGI 0 直写 ICC_SGI1R_EL1(EL1 无 vGIC 硬件面 → EL2
 * EC 0x18 陷阱 → 19D 软件挂起 → 按映射核 poke;TargetList[15:0]
 * 按帧号,Aff1=0 同簇,INTID[27:24]=0)。泵停泊复查的 SGI 判据
 * (站 19 修,不限 reason)兜住 TIMER 停泊中的次核醒。 */
void rt_ipi(u32 cpu)
{
    u64 val = 1ULL << cpu;

    if (cpu != rt_cpu_id())
        __asm__ volatile("msr icc_sgi1r_el1, %0" :: "r"(val) : "memory");
}

void rt_resched_ipi(void)              /* SGI handler 判据:醒来即重选 */
{
    u32 me = rt_cpu_id();

    ipi_rx_count[me]++;
    if (rt_ipi_echo && me == 1u)
        rt_ipi(0);                  /* 回合回程:每收一发即回核 0 一发 */
    need_switch[me] = 1;
}

unsigned long rt_ipi_rx(u32 cpu)
{
    return ipi_rx_count[cpu];
}

static void rdy_enqueue_core(rt_task_t *t, int set_ready)
{
    u32 c = t->cpu;
    u32 p = t->prio;
    u64 d = rt_spin_lock(&rdy_lk[c]);

    /* 站68 修真(58 挂脸族定谳):池外伪任务(idle/idle1=引导语境
     * 就地转正,不占池)永不得入队。案发链:rt_irq_dispatch 出口
     * 对 rt_cur=引导期 idle 照走 rt_preempt_requeue→idle 入
     * prio31 队(每靴 tick1-2 必现,守卫 2/2 实证)→空队 pick 恒
     * 回 idle→调度器把 idle 当真任务切进(eret 陈旧引导帧)、
     * idle->next 落自家 q[] FP 区→链写泼溅(rdy_tail 野化→
     * tail->next 写进 BSS:rt_cur_arr/rt_ticks/壳 sp 全中)→
     * 复合帧 x30=&pool[5] 劫持→guest_park 静默楔/rt_sync_fault
     * panic/孤儿 child/REPL 挂/洪水全家桶。硬门=单窒息点,现役
     * 两个调用面(IRQ 出口/sched 抢占)与未来调用面一并覆盖;
     * idle 上下文仍经 idle->sp 帧机器存还(切换簿记不动此径)。
     * 站68B:set_ready=状态写并入队同锁窗——旧形 rt_ready_push
     * 的 state=READY 在 enqueue 之外裸写,wake_all 自任务语境
     * (sys_exit 收尸广播等,IRQ 开)调用时,tick 恰落两句之间=
     * 守卫见"READY 而未入队"瞬态=自造孤儿假阳性且 panic 杀好
     * 靴(冒烟 0/3 定谳);原子化后内核不变量同固:任务永无
     * READY-未入队瞬态。令牌路(requeue)不碰状态,纪律仍旧。
     * 站68F(root-f):DEAD/FREE 入锁闸——唤醒路径的 check 与入队
     * 之间无锁窗(扫池谓词在锁外读 state),退出路径恰可插入:
     * 检查时 SLEEP→exit 置 DEAD+摘链→push 落后=DEAD 任务入队
     * (68C 守卫实测捕获:rt_cur=已退池任务 st 0,sp 毒 0x1;僵尸
     * 被 pick 后按陈旧帧复活=0xa5a5 野跳族)。陈旧推送落空=无害
     * (被推者已死,醒不醒无意义);合法路径全在 LIVE 态入队。 */
    if (!rt_task_in_pool(t) || t->state == RT_DEAD || t->state == RT_FREE) {
        rt_spin_unlock(&rdy_lk[c], d);
        return;
    }
    if (set_ready)
        t->state = RT_READY;
    t->next = NULL;
    if (!rdy_head[c][p])
        rdy_head[c][p] = t;
    else
        rdy_tail[c][p]->next = t;
    rdy_tail[c][p] = t;
    rdy_map[c] |= 1u << p;
    rt_spin_unlock(&rdy_lk[c], d);
}

static void rdy_enqueue(rt_task_t *t)
{
    rdy_enqueue_core(t, 0);
}

void rt_ready_push(rt_task_t *t)
{
    rdy_enqueue_core(t, 1);         /* 站68B:状态写与入队同锁窗 */
    if (t->cpu != rt_cpu_id())
        rt_ipi(t->cpu);             /* 跨核:目标核队里有了新活 */
}

rt_task_t *rt_pick_ready(void)
{
    u32 c = rt_cpu_id();
    u32 p;
    rt_task_t *t;
    u64 d;

    if (!rdy_map[c])
        return NULL;
    p = (u32)__builtin_ctz(rdy_map[c]);
    d = rt_spin_lock(&rdy_lk[c]);
    t = rdy_head[c][p];
    rdy_head[c][p] = t->next;
    t->next = NULL;
    if (!rdy_head[c][p]) {
        rdy_tail[c][p] = NULL;
        rdy_map[c] &= ~(1u << p);
    }
    /* 站68F(root-f):僵尸摘头闸——队头 DEAD/FREE = 队列不变量已破
     * (入队闸(root-f 前段)漏网的历史残链/收尸竞速)。若放行,
     * 68D 锁内置 RUN 会把僵尸状态洗白复活:按陈旧帧 eret=野跳族
     * (68C 守卫实测 rt_cur=已退池任务+sp 毒 0x1)。尸检纪律:解锁
     * 后停车指名,不消费尸帧。 */
    if (t->state == RT_DEAD || t->state == RT_FREE) {
        rt_spin_unlock(&rdy_lk[c], d);
        rt_panic("zombie picked (rdy corruption)", (unsigned)rt_task_slot(t));
    }
    /* 站68D:摘链+置 RUN 同锁窗(68B 入队侧 state+link 原子的对称
     * 补角)。旧码摘链在锁内而 state=RUN 由调用方在锁外后写,留
     * "已摘链/仍 READY"窗——tick 无锁扫描(rt_rdy_find)落窗即假
     * 孤儿(谓词③ noreturn panic 反杀健康靴;fork 子 prio=父+1
     * 入队即被 pick,实测每 ~3/8 靴预 governed 毒发)。锁内置 RUN
     * 后,出队任务恒 RUN,READY 不变量(READY⇔在队)恢复。 */
    t->state = RT_RUN;
    rt_spin_unlock(&rdy_lk[c], d);
    return t;
}

/* 站十八B:PI 降级摘除;站19 起改全层扫描——PI boost 改写 prio 后,
 * 残令牌可能躺在旧优先级层(按 t->prio 找会漏摘),全扫一律摘净。
 * 单级链表扫描,池 ≤8、层 ≤32,非热点;站二十:任务恒驻本核队列
 * (亲和不迁移,YAGNI),摘本核全层即摘净;跨核摘除不在用例集
 * (exit/block/sleep 全本核)。 */
void rdy_remove(rt_task_t *t)
{
    u32 c = t->cpu;
    u32 p;
    u64 d = rt_spin_lock(&rdy_lk[c]);

    for (p = 0; p < RT_NPRIO; p++) {
        rt_task_t *cur = rdy_head[c][p], *prev = NULL;

        while (cur) {
            if (cur == t) {
                if (prev)
                    prev->next = t->next;
                else
                    rdy_head[c][p] = t->next;
                if (rdy_tail[c][p] == t)
                    rdy_tail[c][p] = prev;
                t->next = NULL;
                if (rdy_head[c][p] == t)   /* 兜底:自环残迹(head=t),清空
                                            * 本层,免得 head 在而 tail 空 */
                    rdy_head[c][p] = NULL;
                if (!rdy_head[c][p])
                    rdy_map[c] &= ~(1u << p);
                rt_spin_unlock(&rdy_lk[c], d);
                return;
            }
            prev = cur;
            cur = cur->next;
        }
    }
    rt_spin_unlock(&rdy_lk[c], d);
}

/* 站68:队列可达性谓词(孤儿守卫的只读访问器)。READY 不变量:
 * 每个 READY 任务必可在其亲和层链上走达(队链丢失=READY 孤儿,
 * 调度器永不再选——beater 脸活体解剖定谳:child READY+陈 pd 挂
 * 在队外,idle 空转而内核 48 万拍全健康,全家桶无声楔的正身)。
 * 只读不锁(守卫在 tick 语境,同核序贯;violation 由守卫 panic
 * 单次收口,判据面允许瞬态模糊零容忍不适用——孤儿即孤儿)。 */
int rt_rdy_find(rt_task_t *t)
{
    rt_task_t *cur = rdy_head[t->cpu][t->prio];

    while (cur) {
        if (cur == t)
            return 1;
        cur = cur->next;
    }
    return 0;
}

/* 站68:队链全审计(第五仪器,root-b 孤儿案)。谓词族:
 * ① head/tail 必落池域或 NULL(泼溅源守卫,root-a 定谳日启用);
 * ② 链必在 RT_POOL_MAX 步内到 NULL(环即爆,含自环);
 * ③ 链实际末元必恰等于 rdy_tail(尾脱链=stale-tail:后续
 *    enqueue 会把新任务链接到已出链的旧尾上=孤儿制造机,
 *    守卫标本三号:child READY 而 find() 不可达,sp/ksp 全净);
 * ④ 链上每员必落池域且态 ∈ {READY, RUN}(RUN=回流令牌,
 *    站19 契约;SLEEP/BLOCK/FREE/DEAD 在链=幽灵)。
 * 违规返回证据四元(c/p/v1 违规值/v2 副证),0=净。 */
int rt_rdy_audit(int *c_out, int *p_out, unsigned long *v1,
                 unsigned long *v2)
{
    rt_task_t *pool_lo = rt_pool_base();
    rt_task_t *pool_hi = pool_lo + RT_POOL_MAX;
    u32 c, p;

    for (c = 0; c < RT_NCPU; c++)
        for (p = 0; p < RT_NPRIO; p++) {
            rt_task_t *h = rdy_head[c][p];
            rt_task_t *cur, *prev = NULL;
            u32 steps = 0;

            if (h && (h < pool_lo || h >= pool_hi)) {
                *c_out = (int)c; *p_out = (int)p;
                *v1 = (unsigned long)h; *v2 = 0;
                return 1;
            }
            if (!h) {
                if (rdy_tail[c][p]) {   /* 头空尾非空=残迹 */
                    *c_out = (int)c; *p_out = (int)p;
                    *v1 = (unsigned long)rdy_tail[c][p]; *v2 = 0;
                    return 2;
                }
                continue;
            }
            for (cur = h; cur; cur = cur->next) {
                if (cur < pool_lo || cur >= pool_hi) {
                    *c_out = (int)c; *p_out = (int)p;
                    *v1 = (unsigned long)cur; *v2 = (unsigned long)prev;
                    return 3;           /* 链走野 */
                }
                if (cur->state != RT_READY && cur->state != RT_RUN &&
                    cur->state != RT_DEAD) {   /* DEAD=exit 瞬态窗:
                                               * state=DEAD→rdy_remove 之间
                                               * tick 可落窗(合法),恒续
                                               * 则由 exit 本身收尾 */
                    *c_out = (int)c; *p_out = (int)p;
                    *v1 = (unsigned long)cur;
                    *v2 = (u64)cur->state;
                    return 4;           /* 幽灵在链 */
                }
                prev = cur;
                if (++steps > RT_POOL_MAX + 1) {
                    *c_out = (int)c; *p_out = (int)p;
                    *v1 = (unsigned long)cur; *v2 = steps;
                    return 5;           /* 环 */
                }
            }
            if (prev != rdy_tail[c][p]) {
                *c_out = (int)c; *p_out = (int)p;
                *v1 = (unsigned long)rdy_tail[c][p];
                *v2 = (unsigned long)prev;
                return 6;               /* 尾脱链(stale-tail) */
            }
        }
    return 0;
}

/* 回流令牌:令牌回队但不碰状态(运行中任务不得被改 READY——rt_ready_push
 * 的状态写只属于真 BLOCK/SLEEP 醒客)。唯一用途:IRQ 出口与 rt_sched 的
 * 自拣兑付(站19 [41]:唤醒打在 rt_cur 身上时的醒递通道,阻塞位点负责清) */
void rt_ready_requeue(rt_task_t *t)
{
    rdy_enqueue(t);
}

/* 被抢占者回队:先清残令牌再入队——运行中任务可能持着醒客令牌,不清就
 * 双份入队,tail->next 会写成自环;自环上的 rdy_remove 摘不掉,DEAD 任务
 * exit 时残环滞留队头,下一 pick 把 DEAD 复活进 UB(站19 pidemo 定谳) */
void rt_preempt_requeue(rt_task_t *t)
{
    rdy_remove(t);
    rt_ready_push(t);
}

/* 就绪最高位是否比 cur 高(数字更小即优先级更高)——本核队判据 */
static int higher_ready_than(rt_task_t *cur)
{
    u32 c = rt_cpu_id();

    if (!rdy_map[c])
        return 0;
    return (u32)__builtin_ctz(rdy_map[c]) < cur->prio;
}

void rt_sched(void)
{
    rt_task_t *next;
    u64 d;

    for (;;) {
        /* 站69(fd 读静默楔定谳):rt_pick_ready 已在队列锁内置 RUN 并
         * 摘链——从它返回到 rt_switch_to 之间若有 tick 插入,tick 出口
         * 的调度会切走别的任务,而已拣出的任务留在 RUN 且不在任何队/
         * 非任何核 rt_cur;发起切换者若是阻塞任务,须待其被唤醒才补完
         * 切换,唤醒永不到即永久失踪(gdb 任务池实捕,vec.S 自述的
         * "RUN 标记永失=无声楔")。整窗屏中断,调用前 DAIF 作为第二参
         * 传入 rt_switch_to 记进续跑 psr(语义与旧 mrs daif 现读等价,
         * 只是取调用方值而非屏蔽值)。 */
        d = rt_daif_save();
        next = rt_pick_ready();
        if (!next) {
            rt_daif_load(d);
            /* 空转语境:无一就绪——WFI 让位(泵判 WAIT)。不 WFI 的话
             * 壳 BLOCK 后会热自旋;醒来重扫,IRQ 出口可能已直接切走
             * 我们(冻结在本帧,无需回头)。
             * 站28 落点加固:宿主泵存在"停泊双重推进"陈疾(resume
             * 落 wfi+8 而非 +4;站24 [41] 家族,已路由专站),曾致
             * +8 直落重排队路径消费陈旧 caller-saved 寄存器(far=0x9
             * panic,冒烟 -d int 定谳)。双 nop 垫使 +4/+8 两落点皆
             * 为受控指令、滑回循环顶——陈疾未修前客人不为其陪葬。 */
            __asm__ volatile("wfi\n\t"
                             "nop\n\t"
                             "nop" ::: "memory");
            continue;
        }
        if (next == rt_cur) {       /* 摘了自己又选回自己:回流令牌兑付。
                                     * 只回队不碰状态——运行中改 READY 会
                                     * 污染 IRQ 出口/RR 的状态门,挂等者
                                     * 被令牌反复"唤醒"短路成忙自旋,
                                     * 等待队列随之双推自环(站19 [41] 定谳) */
            rdy_enqueue(next);
            rt_daif_load(d);
            return;
        }
        next->state = RT_RUN;
        if (rt_cur->state == RT_RUN)/* 被切走的排回队尾(RR 语义;
                                     * 恒单份:先清残令牌,站19) */
            rt_preempt_requeue(rt_cur);
        rt_switch_to(next, d);      /* noreturn:醒来已是新语境;d=调用
                                     * 前 DAIF,记进本任务续跑 psr */
        return;
    }
}

void rt_wake_rx(void)
{
    rt_task_wake_all(RT_RX_BIT);    /* 扫池唤醒:命中等待位的 BLOCK->READY */
}

/* 站68C-14:幽灵残值 tick 环采样——理论穷尽后上机械仪器:每 tick
 * 存 SP_EL1/spsel/tpidr 四元入 16 槽环(蹦床值首现的 tick=真凶时刻,
 * 出生检查开火时整环打印)。spsel 双 ferry 读 bank(直读 UNDEF,站28)。 */
u64 ghost_ring[16][4];
/* 站68G-3:毒 x30 断案槽探针(vec.S 绊线桩写,打印器读)。 */
u64 rt_x30_probe[3];
/* 站68G-5:EL1h 恢复环(vec.S .Lregs 入口写:sp/elr/psr/cpu,64 环)。 */
u64 rt_resume_ring[64][4];
u32 rt_resume_ring_idx;
u32 ghost_ring_idx;

void rt_tick_bookkeep(void)
{
    rt_task_t *t;
    u32 c = rt_cpu_id();
    {
        u64 sel, el1, tp;
        u32 slot;

        __asm__ volatile("mrs %0, spsel" : "=r"(sel));
        __asm__ volatile("msr spsel, #1");
        __asm__ volatile("mov %0, sp" : "=r"(el1));
        __asm__ volatile("msr spsel, %0" :: "r"(sel));
        __asm__ volatile("mrs %0, tpidr_el1" : "=r"(tp));
        slot = ghost_ring_idx++ & 15u;
        ghost_ring[slot][0] = el1;
        ghost_ring[slot][1] = sel;
        ghost_ring[slot][2] = tp;
        ghost_ring[slot][3] = rt_ticks;
    }

    need_switch[c] = 0;
    /* 站二十:睡客扫描单写者化——核 0 独扫全池(推入各自亲和队,
     * 跨核自动携 SGI),核 1 的 tick 只管本队轮转/抢占判。双扫会
     * 对同一睡客双推(rt_ready_push 状态门挡住第二次,但队链会
     * 双挂——单扫根除)。 */
    if (c == 0) {
        char *lim = rt_cur_stack_limit();
        char *sp = rt_cur_sp();

        /* 站23B:live-sp 守卫——历史水位(margin)量过去,守卫量
         * 此刻:tick 异常帧+扫描/回调链(RT_ISR_BUDGET=384)落点
         * 低于可用底即写进 magic 区,下一拍哨兵扫描必 panic;告警
         * 线再加打印链/帧漂移余量(RT_GUARD_LINE=640),低于即
         * 一次性 warn。deepdiver 的封窗深潜 tick 看不见(站21
         * DAIF 窗),零假阳性;idle 语境 lim=NULL 免。 */
        if (lim && sp < lim + RT_GUARD_LINE && !rt_cur->low_warned) {
            /* 评审修真:整行先组后一发——三段各自 rt_puts 是三次锁
             * 窗,同 VM 跨核打印可把 [44] needle 劈中间(本仓纪律:
             * rt_puts_u64 注释/smpdemo 定谳);顺带压 ~90B 低栈链深。
             * 数字自 line 尾倒序生成后前拼,栈账仅 line[96]。 */
            char line[96];
            u64 m = (u64)(sp - lim);
            unsigned int n = 0, i = sizeof(line) - 1, j;

            line[i] = '\0';
            do {
                line[--i] = (char)('0' + (m % 10));
                m /= 10;
            } while (m);
            while (n < i && "stk: lowmargin "[n]) {
                line[n] = "stk: lowmargin "[n];
                n++;
            }
            for (j = 0; rt_cur->name[j] && n < sizeof(line) - 2; j++)
                line[n++] = rt_cur->name[j];
            line[n++] = ' ';
            for (j = i; line[j] && n < sizeof(line) - 2; j++)
                line[n++] = line[j];
            line[n++] = '\n';
            line[n] = '\0';
            rt_cur->low_warned = 1;
            rt_puts(line);
        }
        rt_task_wake_sleepers();    /* 到期睡客 -> READY(入各自队) */
        rt_timer_tick();            /* 站22B:软定时器到点回调(同单写者环) */
    }
    t = rt_cur;
    if (t->state == RT_RUN && t->slice_left > 0) {
        t->slice_left--;
        if (t->slice_left == 0) {
            t->slice_left = RT_SLICE;
            if (rdy_head[c][t->prio]) { /* 同级有人:轮转让位。恒单份:
                                          * t 可能持着醒客令牌,直推双份
                                          * → tail 自环 → 摘除残环队列腐
                                          * 化(head 在 tail 空,NULL deref
                                          * 炸 VM——站19 pidemo 定谳) */
                rt_preempt_requeue(t);
                t->state = RT_READY;
                need_switch[c] = 1;
            }
        }
    }
    if (higher_ready_than(t))
        need_switch[c] = 1;
    if (rt_cpart_tick(t))            /* 站45:分区记账+超标节流
                                      * (两核同面;返 1=已节流,
                                      * 睡客机器接管,窗界自醒) */
        need_switch[c] = 1;
}

int rt_need_switch(void)
{
    return need_switch[rt_cpu_id()];
}
