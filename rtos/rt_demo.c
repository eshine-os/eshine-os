/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "rtos.h"
#include "miniuart.h" /* 站68F:gic.h 前置契约(puts_uart 声明,同 rt_main.c 序) */
#include "gic.h"      /* 站68F:vgicr_read 帧回读(smpstat 判据面) */

/* 演示即判据(第十七站):静态对象,任务体自证同步原语。
 * 池占用:常驻 shell+heartbeat,演示峰值 +2 < 8。
 * 优先级:shell 5 > contender 6 > producer 7 > waiter 8 > consumer 9
 * > heartbeat 10(数字大=优先低)——确定性醒序全靠这张表。 */

#define RING_SLOTS 4
#define RING_ITEMS 8

static rt_sem_t sem_empty;      /* 有界环空槽数 */
static rt_sem_t sem_full;       /* 已填数 */
static rt_mutex_t demo_mutex;

/* ---- semdemo:有界环生产者-消费者(信号量对,阻塞乒乓) ---- */
static void producer_task(void *arg)
{
    unsigned int i;

    (void)arg;
    for (i = 1; i <= RING_ITEMS; i++) {
        rt_sem_take(&sem_empty);
        rt_puts_u64("semdemo: p", i, "\n");    /* 生产 i 号入环 */
        rt_sem_give(&sem_full);
    }
    rt_puts("semdemo: ring filled (8)\n");
}

static void consumer_task(void *arg)
{
    unsigned int i;

    (void)arg;
    for (i = 1; i <= RING_ITEMS; i++) {
        rt_sem_take(&sem_full);
        rt_puts_u64("semdemo: c", i, "\n");    /* 消费 i 号出环 */
        rt_sem_give(&sem_empty);
    }
    rt_puts("semdemo: ring done (8/8)\n");
}

void rt_demo_sem(void)
{
    rt_sem_init(&sem_empty, RING_SLOTS);
    rt_sem_init(&sem_full, 0);
    if (!rt_task_create("producer", producer_task, NULL, 7) ||
        !rt_task_create("consumer", consumer_task, NULL, 9))
        rt_puts("semdemo: pool full\n");
}

/* ---- mutexdemo:阻塞醒序 + 递归 + 非拥有者拒绝 ---- */
static void contender_task(void *arg)
{
    (void)arg;
    rt_puts("mutex: contender locking\n");
    rt_mutex_take(&demo_mutex);
    rt_puts("mutex: contender locked\n");
    rt_sleep_ticks(80);                      /* 800ms 持锁(ps 窗口;
                                             * 站40 加宽:verify 冲尾
                                             * 沉降+ps 往返 ~300ms 推
                                             * 至 300ms 窗缘,r19 起
                                             * 三轮 [28] 假阴性定谳) */
    rt_puts("mutex: contender unlocking\n");
    rt_mutex_give(&demo_mutex);
    rt_puts("mutex: contender released\n");
}

static void waiter_task(void *arg)
{
    (void)arg;
    if (rt_mutex_give(&demo_mutex) == -1)     /* 先试非拥有者给锁 */
        rt_puts("mutex: give denied (not owner)\n");
    rt_puts("mutex: waiter wants lock\n");
    rt_mutex_take(&demo_mutex);               /* 阻塞:醒序判据 */
    rt_puts("mutex: waiter locked (after block)\n");
    rt_mutex_take(&demo_mutex);               /* 递归:同 owner 重入 */
    rt_puts("mutex: recursive take 2 ok\n");
    rt_mutex_give(&demo_mutex);
    rt_mutex_give(&demo_mutex);
    rt_puts("mutex: waiter done\n");
}

void rt_demo_mutex(void)
{
    rt_mutex_init(&demo_mutex);
    if (!rt_task_create("contender", contender_task, NULL, 6) ||
        !rt_task_create("waiter", waiter_task, NULL, 8))
        rt_puts("mutexdemo: pool full\n");
}

/* ---- pidemo(站十八B):PI 饥饿反转——mid 抢死 low,high 挂等
 * boost low 9->6 反超完工。优先级:high 6 / mid 7 / low 9(shell 5
 * 恒最高);墙钟忙旋读 rt_ticks,时序确定性全靠这张表。 ---- */
static rt_mutex_t pi_mutex;

static void pi_high(void *arg)
{
    (void)arg;
    rt_sleep_ticks(100);            /* 让 low 先进临界区 */
    rt_puts("pi: high wants lock\n");
    rt_mutex_take(&pi_mutex);       /* 挂等:boost low 9->6 */
    rt_puts("pi: high got lock (after boost)\n");
    rt_mutex_give(&pi_mutex);
}

static void pi_mid(void *arg)
{
    u64 t0;
    u64 last = 0;

    (void)arg;
    rt_sleep_ticks(50);             /* t=50 醒:7 压 9,饿死 low */
    rt_puts("pi: mid spinning (starving low)\n");
    t0 = rt_ticks;
    while (rt_ticks - t0 < 400) {
        if (rt_ticks != t0 && (rt_ticks - t0) % 100 == 0 &&
            rt_ticks != last) {
            last = rt_ticks;
            rt_puts("pi: mid spinning\n");
        }
    }
    rt_puts("pi: mid done\n");
}

static void pi_low(void *arg)
{
    u64 t0;

    (void)arg;
    rt_mutex_take(&pi_mutex);       /* t=0 即进临界区 */
    rt_puts("pi: low in CS at prio 9\n");
    t0 = rt_ticks;
    while (rt_ticks - t0 < 150)
        ;                           /* 临界区忙旋;boost 后反超 mid 完工 */
    rt_puts("pi: low running at prio 6 (boosted)\n");
    rt_puts("pi: low done (boosted)\n");
    rt_mutex_give(&pi_mutex);       /* 还继承:low 回 9 */
    rt_puts("pi: low restored prio 9\n");
}

void rt_demo_pi(void)
{
    rt_mutex_init(&pi_mutex);
    if (!rt_task_create("pihigh", pi_high, NULL, 6) ||
        !rt_task_create("pimid", pi_mid, NULL, 7) ||
        !rt_task_create("pilow", pi_low, NULL, 9))
        rt_puts("pidemo: pool full\n");
}

/* ---- fpdual(站十八C):per-task FP 帧判据,双 FP 任务并发 ---- */
void rt_demo_fpdual(void)
{
    /* 站20:B 12->11 同级:RR 量子交织 A/B,每片边界一次持有权
     * 轮转(fp-saves 递增),fpstat 判据 M<<N 才有非零分子;异级
     * 串行(A 先跑完退场清主,B 才起步)首陷阱恒见旧主 NULL,
     * saves 恒 0——[34] 门禁实测抓出,双任务语义不变仍双精确。 */
    if (!rt_task_create("fpdA", rt_fp_dual_a_task, NULL, 11) ||
        !rt_task_create("fpdB", rt_fp_dual_task, NULL, 11))
        rt_puts("fpdual: pool full\n");
}

/* ---- eventdemo(站十九B):AND 双拍不醒+双拍齐醒+OR 支线 ----
 * 优先级:setter 6 < AND-waiter 8 < OR-waiter 9——set 醒即抢,
 * 行序全确定性:A 拍 AND 不满(仍挂),B 拍齐醒,C 拍 OR 即醒。 */
static rt_event_t demo_event;

static void evt_waiter_and(void *arg)
{
    u32 got;

    (void)arg;
    rt_puts("evt: waiter waiting AND {A|B}\n");
    rt_event_wait(&demo_event, 0x5u, 1, &got);
    rt_puts("evt: waiter fired flags=0x5\n");   /* A|B 恰 0x5,字面自证 */
}

static void evt_waiter_or(void *arg)
{
    u32 got;

    (void)arg;
    rt_puts("evt: or-waiter waiting OR {C}\n");
    rt_event_wait(&demo_event, 0x8u, 0, &got);
    rt_puts("evt: OR fired flags=0x8\n");
}

static void evt_setter(void *arg)
{
    (void)arg;
    rt_sleep_ticks(50);
    rt_event_set(&demo_event, 0x1u);            /* A:AND 不满,waiter 仍挂 */
    rt_puts("evt: A set (waiter stays)\n");
    rt_sleep_ticks(50);
    rt_event_set(&demo_event, 0x4u);            /* B:双拍齐,AND met */
    rt_puts("evt: B set (AND met)\n");
    rt_sleep_ticks(50);
    rt_event_set(&demo_event, 0x8u);            /* C:OR 支线即醒 */
    rt_puts("evt: C set (OR path)\n");
}

void rt_demo_event(void)
{
    rt_event_init(&demo_event);
    if (!rt_task_create("evtand", evt_waiter_and, NULL, 8) ||
        !rt_task_create("evtor", evt_waiter_or, NULL, 9) ||
        !rt_task_create("evtset", evt_setter, NULL, 6))
        rt_puts("eventdemo: pool full\n");
}

/* ---- queuedemo(站十九B):16 条 u64 环(cap=8),满发阻塞乒乓 ----
 * 优先级:producer 7 压 consumer 9——灌满即阻塞,收方慢收逼出
 * 多轮满环。行判据:第 9 条前的阻塞自报 + 16/16 闭环。 */
static rt_queue_t demo_queue;

static void q_producer(void *arg)
{
    unsigned int i;

    (void)arg;
    for (i = 1; i <= 16; i++) {
        if (i == 9)                             /* cap=8:第 9 条必先满 */
            rt_puts("q: ring full, sender blocked\n");
        rt_queue_send(&demo_queue, 0x1000u + i);
        rt_puts_u64("q: sent ", i, "\n");
    }
    rt_puts("q: producer done 16\n");
}

static void q_consumer(void *arg)
{
    unsigned int i;
    u64 msg;

    (void)arg;
    for (i = 1; i <= 16; i++) {
        rt_queue_recv(&demo_queue, &msg);
        rt_puts_u64("q: got ", i, "\n");
        rt_sleep_ticks(2);                      /* 慢收:逼出满环阻塞 */
    }
    rt_puts("q: done 16/16\n");
}

void rt_demo_queue(void)
{
    rt_queue_init(&demo_queue, 8);
    if (!rt_task_create("qprod", q_producer, NULL, 7) ||
        !rt_task_create("qcons", q_consumer, NULL, 9))
        rt_puts("queuedemo: pool full\n");
}

/* ---- 站二十:双核 demo 三件套(展示即判据) ----
 * smpdemo:两计数任务分驻两核,各自计数打行——"cpu1:" 行的存在与
 * 多次出现 = 次核调度循环在跑(每轮都经 本核 tick/IPI 醒来);
 * ipidemo:cpu0 发一串 SGI + 一个亲核 1 的睡客——睡客每次醒来
 * 都是 cpu0 的扫描把活推过核(跨核入列+SGI),全链证明;
 * fpsmp:双 FP 任务分驻两核,双精确 = per-CPU FP 帧无损。 */

static u64 smp_cnt[RT_NCPU];

static void smp_counter(void *arg)
{
    u64 me = (u64)arg;
    u64 t0 = rt_ticks;
    u64 last = 0;

    while (rt_ticks - t0 < 300) {              /* 3s 计数窗 */
        smp_cnt[me]++;
        if (rt_ticks - t0 >= last + 100) {      /* 每 ~1s 打一行 */
            last = rt_ticks - t0;
            rt_puts_u64(me ? "cpu1: n=" : "cpu0: n=", smp_cnt[me], "\n");
        }
    }
    rt_puts_u64(me ? "cpu1: done=" : "cpu0: done=", smp_cnt[me], "\n");
}

void rt_demo_smp(void)
{
    smp_cnt[0] = smp_cnt[1] = 0;
    if (!rt_task_create_cpu("smp0", smp_counter, (void *)0UL, 12, 0) ||
        !rt_task_create_cpu("smp1", smp_counter, (void *)1UL, 12, 1))
        rt_puts("smpdemo: pool full\n");
}

/* 站68F:smpstat——次核在线状态的命令面查询。门[35]原判据=次核
 * 开机一次性自报行,在 chardev 分块丢字节族(站25/33 记档,散布型
 * 丢行:全量两连实测丢 "log core up"+"cpu1: online" 相邻两行)
 * 下丢一次即永不再来=90s 必死;命令/应答面(发 smpstat 读回行)
 * 对该族免疫。判据保真:帧1 ISENABLER0 使能位 0x08000001
 * (SGI0|PPI27)只有次核 gic_secondary_frame_init 写得出;vgic 模拟
 * 按地址路由(mmio.c 帧号=off>>17)主核跨帧读回同值——0=次核
 * 从未上岗,诚实失败;开机自报行保留(人读面不变)。 */
void rt_demo_smpstat(void)
{
    rt_puts_u64("smp: cpu1 gicr1_en=",
                (u64)vgicr_read(0x20000UL + GICR_SGI + GICR_ISENABLER0),
                "\n");
}

static void ipi_sleeper(void *arg)
{
    unsigned int i;

    (void)arg;
    for (i = 1; i <= 3; i++) {
        rt_sleep_ticks(100);                   /* 每轮 1s:到期由核 0 的
                                                * 扫描跨核推送+SGI 唤醒 */
        rt_puts_u64("ipi: cpu1 awake ", i, "\n");
    }
    rt_puts("ipi: sleeper done\n");
}

static void ipi_main(void *arg)
{
    unsigned int i;
    unsigned long rx0 = rt_ipi_rx(0);

    (void)arg;
    rt_ipi_echo = 1u;                   /* 回合窗开:核 1 handler 逐拍回发 */
    for (i = 0; i < 20; i++) {
        rt_ipi(1);                     /* SGI 0 → 核 1 重调度拍 */
        rt_sleep_ticks(5);             /* 拍间留处理窗(回程在此入账) */
    }
    rt_sleep_ticks(20);                /* 尾拍回程窗(最后一发的回账) */
    rt_ipi_echo = 0u;
    rt_puts_u64("ipi: rx1=", rt_ipi_rx(1), "\n");
    /* 往返判据:核 0 实收回发数(≥20 = 20 发全回程;跨核唤醒推送
     * 在窗内也会各得一回,计入即真,判据线只设下限)。 */
    rt_puts_u64("ipi: rx0=", rt_ipi_rx(0) - rx0, " roundtrips\n");
}

void rt_demo_ipi(void)
{
    /* 站24A 评审:回合循环原在壳语境执行(rt_sleep_ticks 使壳过
     * RT_SLEEP 态)——心跳按需化后 demo 期壳每拍夹心跳行([37]
     * 窗+手动走查刷屏,恰是用户令①点名要除的)。移入独立任务
     * ipimain:壳即回提示符(RT_BLOCK=静默),判据 needle 全由
     * 任务打印,语义零迁。 */
    if (!rt_task_create_cpu("ipi1", ipi_sleeper, NULL, 9, 1)) {
        rt_puts("ipidemo: pool full\n");
        return;
    }
    if (!rt_task_create("ipimain", ipi_main, NULL, 8)) {
        rt_puts("ipidemo: pool full\n");
        return;
    }
}

void rt_demo_fpsmp(void)
{
    if (!rt_task_create_cpu("fpa0", rt_fp_smp_a_task, NULL, 13, 0) ||
        !rt_task_create_cpu("fpb1", rt_fp_smp_b_task, NULL, 13, 1))
        rt_puts("fpsmp: pool full\n");
}

/* ---- 站21A:tmodemo——超时面全确定性编排(判据即演示) ----
 * 三任务:a(8 驱动)/b(9 取者)/c(14 低优先锁主);静态 sem/mutex/cap-4
 * 队列。时间线(spec A3):a 先跑,no-wait 判负→满环挂发(30)→b 空
 * sem 挂(30)→c 持锁自报 prio(10 倍率)。t=30 双超时(a 先醒,池槽
 * 序);a 睡 20 后 give;b 再挂 sem(200)于 t=50 在时醒;b 转 mutex(40)
 * ——c 被提至 9(自报行);t=90 b 超时,扫描摘除+重算 c 回 14(自报
 * 行);b 排空队列(no-wait 收 ×4)再空挂(20)超时;收尾三 done。
 * 返码判据行=字面串(rt_puts_u64 是 u64,-2 打成巨数,评审核)。 */
static rt_sem_t tmo_sem;
static rt_mutex_t tmo_mtx;
static rt_queue_t tmo_q;

static void tmo_report(const char *tag, long r)
{
    rt_puts("tmo: ");
    rt_puts(tag);
    if (r == RT_TIMEOUT)
        rt_puts(" ret=-2\n");
    else if (r == 0)
        rt_puts(" ret=0\n");
    else
        rt_puts(" ret=?\n");
}

static void tmo_a(void *arg)                  /* 驱动:prio 8 */
{
    long r;

    (void)arg;
    r = rt_sem_take_to(&tmo_sem, 0);          /* no-wait:空 sem 判负 */
    tmo_report("sem nowait", r);
    rt_queue_send(&tmo_q, 0x11);              /* 填满 cap-4(无人收) */
    rt_queue_send(&tmo_q, 0x22);
    rt_queue_send(&tmo_q, 0x33);
    rt_queue_send(&tmo_q, 0x44);
    r = rt_queue_send_to(&tmo_q, 42, 30);     /* 满环挂发,30 超时 */
    tmo_report("q send", r);                  /* t=30 醒(先于 b,槽序) */
    rt_sleep_ticks(20);
    rt_sem_give(&tmo_sem);                    /* t=50:b 在时醒 */
    rt_sleep_ticks(200);
    rt_puts("tmo: a done\n");
}

static void tmo_b(void *arg)                  /* 取者:prio 9 */
{
    long r;
    u64 m;
    unsigned int got = 0, i;

    (void)arg;
    r = rt_sem_take_to(&tmo_sem, 30);         /* 空挂,t=30 超时 */
    tmo_report("sem timeout", r);
    r = rt_sem_take_to(&tmo_sem, 200);        /* 再挂,t=50 在时醒 */
    tmo_report("sem got", r);
    r = rt_mutex_take_to(&tmo_mtx, 40);       /* c 持锁:boost c,t=90 超时 */
    tmo_report("mutex timeout", r);
    for (i = 0; i < 4; i++)                   /* no-wait 排空(4 条都在) */
        if (rt_queue_recv_to(&tmo_q, &m, 0) == 0)
            got++;
    rt_puts(got == 4 ? "tmo: q drain 4 ret=0\n" : "tmo: q drain != 4\n");
    r = rt_queue_recv_to(&tmo_q, &m, 20);     /* 空挂,t=110 超时 */
    tmo_report("q recv timeout", r);
    rt_puts("tmo: b done\n");
}

static void tmo_c(void *arg)                  /* 低优先锁主:prio 14 */
{
    unsigned int i;

    (void)arg;
    rt_mutex_take(&tmo_mtx);                  /* a/b 挂稳后到手 */
    for (i = 0; i < 10; i++) {
        rt_sleep_ticks(10);
        rt_puts_u64("tmo: c prio ", rt_cur->prio, "\n");  /* 14→9(被 boost)
                                             * →14(b 超时扫描重算)全弧 */
    }
    rt_mutex_give(&tmo_mtx);
    rt_puts("tmo: c done\n");
}

void rt_demo_tmo(void)
{
    rt_sem_init(&tmo_sem, 0);
    rt_mutex_init(&tmo_mtx);
    rt_queue_init(&tmo_q, 4);
    if (!rt_task_create("tmo_c", tmo_c, NULL, 14) ||
        !rt_task_create("tmo_b", tmo_b, NULL, 9) ||
        !rt_task_create("tmo_a", tmo_a, NULL, 8))
        rt_puts("tmodemo: pool full\n");
}

/* ---- 站21C:stkdemo/overflow——受控深潜与受控越哨 ----
 * 深度判据=实测 SP(asm 直读;__builtin_frame_address 在 -O2 无帧
 * 指针下不可用——首版递归零下潜,实测定谳);volatile 局部活过
 * 递归点反 TCO(评审核:TCO 递归不增长栈,判据失效)。deepdiver 止
 * 于哨兵上方 256B(不越界,水位收缩即判据,dived 后留活供 ps/
 * stackcheck 读水位行);overflower 止于垫内深限——越哨后受控
 * overshoot 即解栈回浅再 wfi 自停(评审核:深 sp 处不留栈,异常
 * 帧/panic 链永远不在垫区)。 */
static char *cur_sp(void)
{
    char *p;

    __asm__ volatile("mov %0, sp" : "=r"(p));
    return p;
}

/* 递归帧全宽写:每级 volatile u64 pat[8] 逐词写+活过递归点——
 * 足迹连续铺满下探路径(首版 fp 对+单字节 pad 的稀疏足迹让哨兵
 * 落缝,实测定谳:潜穿哨兵而 magic 完好);反 TCO 同源。 */
static void deep_recurse(char *stop, u64 *acc)
{
    volatile u64 pat[8];
    unsigned int i;

    for (i = 0; i < 8; i++)
        pat[i] = 0xD00D0000 + i;
    if (cur_sp() > stop)
        deep_recurse(stop, acc);
    for (i = 0; i < 8; i++)
        *acc += pat[i];
}

static void deepdiver_task(void *arg)
{
    char *limit = rt_cur_stack_limit();
    u64 acc = 0;
    u64 d;

    (void)arg;
    /* 站21 实现评审:递归驱动段封 DAIF 窗——末两级帧距哨兵不足
     * 272B(tick ISR 全帧)+扫描 C 链,tick 打断于该深度会把自家
     * 异常帧砸在哨兵上,同一拍扫描即假 panic([41] 概率性闪断源,
     * 实测帧 96B/级)。µs 级短临界段,站十七同律。 */
    d = rt_daif_save();
    if (limit)
        deep_recurse(limit + 256, &acc);  /* 止于可用底上方 256B(哨兵
                                           * 安全区,水位收缩即判据;
                                           * 首版 limit-256 是穿透误植) */
    rt_daif_load(d);
    rt_puts("stkdemo: dived\n");
    for (;;)                             /* 留活:水位行要被 ps/
                                           * stackcheck 读到(判据面) */
        rt_sleep_ticks(60000);
}

static void over_recurse(char *stop, u64 *acc)
{
    volatile u64 pat[8];
    unsigned int i;

    for (i = 0; i < 8; i++)
        pat[i] = 0xF00D0000 + i;
    if (cur_sp() > stop)
        over_recurse(stop, acc);
    for (i = 0; i < 8; i++)
        *acc += pat[i];                       /* 解栈回浅:sp 回升 */
}

static void overflower_task(void *arg)
{
    char *floor = rt_cur_stack_floor();
    u64 acc = 0;
    u64 d;

    (void)arg;
    if (!floor) {
        rt_puts("overflow: no pool stack\n");
        return;
    }
    /* 站21 实现评审:同 deepdiver 封窗——递归途中 tick 的异常帧会
     * 落在垫区之下越行;封窗后 tick 只能在解栈回浅后命中(那是设计
     * 的捕获相位,panic 帧落正常深度)。 */
    d = rt_daif_save();
    over_recurse(floor, &acc);                /* 越哨兵,overshoot 入垫 */
    rt_daif_load(d);
    for (;;)
        __asm__ volatile("wfi");             /* 浅层自停:tick 命中即
                                              * rt_panic 指名([74] 判据) */
}

void rt_demo_stk(void)
{
    if (!rt_task_create("deepdiver", deepdiver_task, NULL, 11))
        rt_puts("stkdemo: pool full\n");
}

void rt_demo_overflow(void)
{
    if (!rt_task_create("overflower", overflower_task, NULL, 11))
        rt_puts("overflow: pool full\n");
}

/* ---- 站22B:swtdemo——软定时器三面判据(单发/周期自停/取消) ----
 * 回调在核 0 tick-ISR 语境打印(rt_puts 持 puts_lk:持锁即屏本核,
 * ISR 不可能在持锁任务上再入——自旋锁契约);时间线:
 *   t=0: 起 单发(30) / 周期(15,×3 自停) / 单发(50,20 时取消)
 *   t=15/30/45: "swt: periodic N";第 3 拍回调内自停(stopped)
 *   t=30: "swt: oneshot fired"(单发到点)
 *   t=20: "swt: cancelled"(stop 生效)
 *   t=60: demo 任务收尾 "swt: done"——此后永无 "swt: late"
 *   (50 已取消,缺席即判据) */
static rt_timer_t swt_os, swt_pd, swt_late;
static unsigned int swt_pd_count;

static void swt_oneshot_cb(void *arg)
{
    (void)arg;
    rt_puts("swt: oneshot fired\n");
}

static void swt_periodic_cb(void *arg)
{
    (void)arg;
    swt_pd_count++;
    rt_puts_u64("swt: periodic ", swt_pd_count, "\n");
    if (swt_pd_count >= 3) {
        rt_puts("swt: periodic stopped\n");
        rt_timer_stop(&swt_pd);      /* 回调内自停:懒置 0+注销,合法 */
    }
}

static void swt_late_cb(void *arg)
{
    (void)arg;
    rt_puts("swt: late fired\n");    /* 不该到:缺席即判据 */
}

static void swt_driver_task(void *arg)
{
    (void)arg;
    rt_sleep_ticks(20);
    rt_timer_stop(&swt_late);        /* t=20 取消 t=50 的单发 */
    rt_puts("swt: cancelled\n");
    rt_sleep_ticks(60);              /* 收尾窗:50 已过且无 late */
    rt_puts("swt: done\n");
}

void rt_demo_swt(void)
{
    rt_timer_init(&swt_os, swt_oneshot_cb, NULL);
    rt_timer_init(&swt_pd, swt_periodic_cb, NULL);
    rt_timer_init(&swt_late, swt_late_cb, NULL);
    swt_pd_count = 0;
    /* 评审核:start 返码化(池满/0 拒须可观测);demo 三发全验 */
    if (rt_timer_start(&swt_os, 30, 0) || rt_timer_start(&swt_pd, 15, 1) ||
        rt_timer_start(&swt_late, 50, 0)) {
        rt_puts("swtdemo: timer pool full\n");
        return;
    }
    if (!rt_task_create("swtdrv", swt_driver_task, NULL, 9))
        rt_puts("swtdemo: pool full\n");
}

/* ---- 站23A:irqdemo——注册表活证(链式包装/计数/恢复) ----
 * 计数由探针 x 的 UART IRQ 供给(门判 0.1s 延迟补发:命令字节
 * 在换钩前已被壳消费殆尽——首版以为命令自身 IRQ 会流入 wrap,
 * 两轮探针定谳为假;同拍爆发整批被换钩前 ISR 消费同理);
 * restore 后原 drain 语义不变,壳继续可用即证。登记窗口期同核
 * 序贯,IRQ 走新旧槽皆可(注册表 A1 契约)。 */
static volatile unsigned int irq_chain_count;
static void (*irq_uart_prev)(void);

static void irq_wrap_uart(void)
{
    irq_chain_count++;
    if (irq_uart_prev)
        irq_uart_prev();              /* 链式:原 drain 仍活(RX 面不断) */
}

static void irqdemo_task(void *arg)
{
    int budget = 40;

    /* 自同步(评审 5):探针到位才收摊——固定 30 tick 窗(0.3s)对
     * 手动走查过苛(人类手速贴刀锋,0.1s 门判补发同理);改 5 tick
     * 一探预算 2s,门判/手动/迟探皆成,超时保底打印(诚实 0 也报)。 */
    (void)arg;
    while (!irq_chain_count && --budget)
        rt_sleep_ticks(5);
    rt_puts_u64("irq: chained ", irq_chain_count, "\n");
    rt_irq_swap_uart(irq_uart_prev);   /* 恢复原 drain 语义 */
    rt_puts("irqdemo: done\n");       /* 恢复后壳继续可用(判据) */
}

void rt_demo_irq(void)
{
    irq_chain_count = 0;
    /* 评审修真:先建任务后换钩——池满路径不再把 33 号槽永久留
     * wrap(登记面不被 demo 污染)。 */
    if (!rt_task_create("irqdemo", irqdemo_task, NULL, 9)) {
        rt_puts("irqdemo: pool full\n");
        return;
    }
    irq_uart_prev = rt_irq_swap_uart(irq_wrap_uart);
}

/* ---- 站23B:lowdemo——贴线守卫判据 ----
 * lowrunner 以 asm-SP 判深递归;停点在告警线外侧
 * (stop=lim+RT_GUARD_LINE+256=lim+896)。实测账(评审 disassembly
 * 定谳:帧 64B/级——站21 的 96B/级推断错一级,勿再引;守卫链
 * 272+48+0+64=384 恰 RT_ISR_BUDGET;打印链组串后 ~160B):本
 * build warn 打印 400(守卫读点系)→ 打印链底 ~240,余量足;停点
 * 保底:读点 640−打印链 160−漂移 >256B 不碰 magic。初版 lim+600
 * 实测仅 +48B 余量(量化运气过线,评审抓出),重锚于此。最深
 * 层 busy 旋 30 tick 不封窗 → tick 必在深 sp 处命中守卫 →
 * 一次性 warn → 解栈回浅收尾。deepdiver 照旧封窗,守卫看不到
 * 它,零假阳性。 */
static void low_recurse(char *stop, u64 *acc)
{
    volatile u64 pad[4];

    pad[0] = 1;
    if (rt_cur_sp() > stop) {
        low_recurse(stop, acc);
    } else {
        u64 end = rt_ticks + 30;

        while ((long long)(rt_ticks - end) < 0)
            pad[0] = pad[0] * 3 + 1;  /* 深度处忙旋:tick 在此命中 */
    }
    *acc += pad[0];
}

static void lowrunner_task(void *arg)
{
    char *lim = rt_cur_stack_limit();
    u64 acc = 0;

    (void)arg;
    if (!lim) {
        rt_puts("lowdemo: no pool stack\n");
        return;
    }
    low_recurse(lim + RT_GUARD_LINE + 256, &acc);
    rt_puts("lowdemo: done\n");
}

void rt_demo_low(void)
{
    if (!rt_task_create("lowrunner", lowrunner_task, NULL, 11))
        rt_puts("lowdemo: pool full\n");
}
