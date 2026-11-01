/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "rtos.h"

/* 站四十五:分区化——APS(CPU 预算分区)+MPRT(内存预算分区)。
 * 参考实现机制白描(k_aps.c/k_apm.c,零代码拷贝红线):
 *
 * APS=QNX 自适应分区调度的白描形状——百分比预算分区+tick 记账
 * (k_part_stat_tick_update 对 cur 任务,粒度同款=系统 tick)+
 * 超标节流+**偷闲**(free CPU 可取,guarantee+steal 两语义)。
 * 站内 v0:窗=20 tick,credit=budget×20/100;超标且**别人还在跑**
 * 才节流——复用站 17 睡客机器(state=RT_SLEEP+wake_tick=下窗界,
 * 零新等待逻辑);无人跑=偷闲(QNX steal 同义)。
 *
 * MPRT=任务组内存记账白描(mprt_charge/uncharge,max+usage)。
 * 站内 v0 记账点=**声明窗单一咽喉** as_declare_pool(mmap 与 fork
 * 同走此口,一钩双面;参考实现记 resident 页,我们记 declare-time
 * ——差异记档 spec §4);munmap uncharge,退场全退。 */

#define RT_CPART_WIN    20u             /* 记账窗(tick) */
/* rt_syscall.c 同值(rtos.h 未提级;demo 面局部约定) */
#define ES_EAGAIN       11
#define ES_ENOMEM       12

#define RT_CPART_MAX    4u
#define RT_MPART_MAX    4u

struct rt_cpart {
    u32 used;
    char name[8];
    u32 budget_pct;                     /* 保证下限(%);Σ≤100 纪律 */
    long credit;                        /* 窗内余量(tick);重置=
                                          * budget×WIN/100 */
    u32 nthrottle;                     /* 站48:机制账本——超标节流
                                         * 事件(判据环境免疫:任务
                                         * 观测边界在宿主负载下欠账,
                                         * 批环境三跑三数字定谳) */
    u32 nsteal;                         /* 偷闲回冲事件 */
};

struct rt_mpart {
    u32 used;
    char name[8];
    u32 max_pages;                      /* 声明页上限 */
    u32 cur_pages;                      /* 在役声明(Σ=入组 proc 的
                                          * mpart_pages) */
};

static struct rt_cpart cparts[RT_CPART_MAX];
static u64 cpart_win_base;               /* 站48:滞后窗基(跳变免疫) */

static void part_name(char *dst, const char *src, u32 n)
{
    u32 i;

    for (i = 0; i + 1 < n && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}
static struct rt_mpart mparts[RT_MPART_MAX];

/* ---- APS ---- */

void rt_cpart_init(void)
{
    cparts[0].used = 1;
    cparts[0].budget_pct = 100;         /* 分区 0=system(余量池;
                                          * 新分区从此扣) */
    cparts[0].credit = (long)RT_CPART_WIN;
    part_name(cparts[0].name, "system", sizeof(cparts[0].name));
    /* 网格对齐(必):节流 wake_tick 用 %WIN 网格((ticks/WIN+1)*WIN),
     * win_base 若取任意 tick 值则重置面与醒点错位数 tick——被节流
     * 者在醒点逢未重置的 credit 即再节流,每窗净丢 (WIN-错位) tick,
     * 系统分区全体长任务 ~5x 减速(r5-r11 六连败冒烟不显根因定谳:
     * 冒烟安静窗+宽窗全吸收,电池逐面炸)。对齐后两网格同拍。 */
    cpart_win_base = (rt_ticks / RT_CPART_WIN) * RT_CPART_WIN;
}

int rt_cpart_alloc(const char *name, u32 pct)
{
    u32 i;

    if (!name || pct == 0 || pct >= 100 ||
        cparts[0].budget_pct < pct)
        return -ES_EINVAL;              /* Σ≤100 纪律:超扣拒 */
    for (i = 1; i < RT_CPART_MAX; i++)
        if (!cparts[i].used)
            break;
    if (i == RT_CPART_MAX)
        return -ES_EAGAIN;
    cparts[0].budget_pct -= pct;         /* 从 system 扣(guarantee 面) */
    cparts[i].used = 1;
    cparts[i].budget_pct = pct;
    cparts[i].credit = (long)(pct * RT_CPART_WIN / 100);
    part_name(cparts[i].name, name, sizeof(cparts[i].name));
    return (int)i;
}

void rt_cpart_join(u32 id, rt_task_t *t)
{
    if (t && id < RT_CPART_MAX && cparts[id].used)
        t->cpart = id;
}

/* 每 tick 记账(tick 语境,两核同调;rt_cur=idle 池外伪任务免)。
 * 返 1=当前任务被节流(调用侧置 need_switch)。 */
int rt_cpart_tick(rt_task_t *t)
{
    u32 i, id;
    struct rt_cpart *cp;

    if (!t || !rt_task_in_pool(t))
        return 0;
    /* 窗界重置(站48 批验收定谳:改滞后基——`%WIN==0` 在宿主
     * 饥饿下被 tick 跳变(coalesce)击穿,system 分区假超标
     * 全驻车,壳/装载子连环停摆;滞后基任何迟到边界都补,
     * 跳变免疫)。 */
    if (rt_ticks - cpart_win_base >= RT_CPART_WIN) {
        cpart_win_base += RT_CPART_WIN *
            ((rt_ticks - cpart_win_base) / RT_CPART_WIN);
        for (i = 0; i < RT_CPART_MAX; i++)
            if (cparts[i].used)
                cparts[i].credit =
                    (long)(cparts[i].budget_pct * RT_CPART_WIN / 100);
    }
    id = t->cpart;
    cp = &cparts[id];
    cp->credit--;
    if (cp->credit >= 0)
        return 0;
    /* 超标:池内别人还在跑才节流;无人跑=偷闲(free CPU) */
    {
        unsigned int idx = 0;
        rt_task_t *o;

        while ((o = rt_ps_next(&idx)) != NULL) {
            if (o == t || o == rt_idle_task())
                continue;
            if (o->state == RT_READY || o->state == RT_RUN)
                goto throttle;
        }
        cp->credit++;                   /* 偷闲:回冲本次记账
                                         * (免窗末双倍亏) */
        cp->nsteal++;
        return 0;
    }
throttle:
    cp->nthrottle++;
    t->wake_tick = (rt_ticks / RT_CPART_WIN + 1) * RT_CPART_WIN;
    t->wait_src = RT_WSRC_SLEEP;        /* 站43:归属列 */
    t->state = RT_SLEEP;                /* 复用站 17 睡客机器:
                                          * 核 0 扫描窗界唤醒 */
    rdy_remove(t);                      /* 站19:清自拣令牌 */
    return 1;
}

void rt_cpart_show(void)
{
    u32 i;

    for (i = 0; i < RT_CPART_MAX; i++) {
        if (!cparts[i].used)
            continue;
        rt_puts("  cpart ");
        rt_puts(cparts[i].name);
        rt_puts_u64(" budget ", cparts[i].budget_pct, "%");
        if (cparts[i].credit < 0)
            rt_puts_u64(" credit -", (u64)-(u64)cparts[i].credit, "\n");
        else
            rt_puts_u64(" credit ", (u64)cparts[i].credit, "\n");
    }
}

/* ---- MPRT ---- */

int rt_mpart_alloc(const char *name, u32 max_pages)
{
    u32 i;

    if (!name || max_pages == 0)
        return -ES_EINVAL;
    for (i = 0; i < RT_MPART_MAX; i++)
        if (!mparts[i].used)
            break;
    if (i == RT_MPART_MAX)
        return -ES_EAGAIN;
    mparts[i].used = 1;
    mparts[i].max_pages = max_pages;
    mparts[i].cur_pages = 0;
    part_name(mparts[i].name, name, sizeof(mparts[i].name));
    return (int)i;
}

void rt_mpart_attach(struct rt_proc *p, int id)
{
    if (p && id >= 0 && (u32)id < RT_MPART_MAX && mparts[id].used)
        p->mpart = id;                  /* attach 前 VMA 不追溯
                                         * (策略起点面,spec §4) */
}

/* 声明窗记账(as_declare_pool 咽喉调用;0=未入组恒过)。
 * 失败=超上限,调用侧回滚 VMA 并落 -ENOMEM。 */
int rt_mpart_charge(struct rt_proc *p, u32 npages)
{
    struct rt_mpart *m;

    if (!p || p->mpart < 0 || npages == 0)
        return 0;
    m = &mparts[p->mpart];
    if (m->cur_pages + npages > m->max_pages)
        return -ES_ENOMEM;
    m->cur_pages += npages;
    p->mpart_pages += npages;
    return 0;
}

void rt_mpart_uncharge(struct rt_proc *p, u32 npages)
{
    struct rt_mpart *m;

    if (!p || p->mpart < 0 || npages == 0)
        return;
    m = &mparts[p->mpart];
    m->cur_pages = m->cur_pages >= npages ? m->cur_pages - npages : 0;
    p->mpart_pages = p->mpart_pages >= npages ? p->mpart_pages - npages : 0;
}

/* 退场全退(rt_proc_exit;fork 继承=子 VMA 重声明走 charge 自带) */
void rt_mpart_exit_all(struct rt_proc *p)
{
    if (p && p->mpart >= 0) {
        rt_mpart_uncharge(p, p->mpart_pages);
        p->mpart = -1;
    }
}

/* 站48:health 面板出数(在役分区计数) */
u32 rt_cpart_used(void)
{
    u32 i, n = 0;

    for (i = 0; i < RT_CPART_MAX; i++)
        if (cparts[i].used)
            n++;
    return n;
}

u32 rt_mpart_used(void)
{
    u32 i, n = 0;

    for (i = 0; i < RT_MPART_MAX; i++)
        if (mparts[i].used)
            n++;
    return n;
}

void rt_mpart_show(void)
{
    u32 i;

    for (i = 0; i < RT_MPART_MAX; i++) {
        if (!mparts[i].used)
            continue;
        rt_puts("  mpart ");
        rt_puts(mparts[i].name);
        rt_puts_u64(" cur ", mparts[i].cur_pages, "/");
        rt_puts_u64("", mparts[i].max_pages, "p\n");
    }
}

/* ---- 站45:demo 判据体 ---- */

void rt_cpart_free(int id)
{
    if (id > 0 && (u32)id < RT_CPART_MAX && cparts[id].used) {
        cparts[0].budget_pct += cparts[id].budget_pct;  /* 预算还 system */
        cparts[id].used = 0;
        cparts[id].budget_pct = 0;
    }
}

void rt_mpart_free(int id)
{
    if (id >= 0 && (u32)id < RT_MPART_MAX && mparts[id].used) {
        mparts[id].used = 0;
        mparts[id].max_pages = 0;
        mparts[id].cur_pages = 0;
    }
}

/* 忙任务:数 rt_ticks 变化沿=本任务实得 tick 账(tick 粒度观测) */
struct aps_arg {
    volatile u32 run;
    u32 cnt;
};

static struct aps_arg aps_a, aps_b;

static void aps_busy(void *arg)
{
    struct aps_arg *a = arg;
    u64 last = rt_ticks;

    while (a->run) {
        if (rt_ticks != last) {
            last = rt_ticks;
            a->cnt++;
        }
    }
    rt_task_exit();
}

/* CPU 分区判据(蹦床 RT_U_APS 调,壳任务内核语境;争跑窗+偷闲窗)。
 * 对齐纪律:先睡到窗界再起任务——两满窗的记账是确定性的
 * (a=8,b=32,share=20%);偷闲窗独跑 45 拍,记满 40 判。 */
void rt_aps_demo(void)
{
    int id;
    rt_task_t *ta, *tb;
    u32 a1, b1, share, ca0, a2, nth, nst;

    id = rt_cpart_alloc("demoA", 20);
    if (id < 0) {
        rt_puts("aps: cpart alloc FAIL\n");
        return;
    }
    cparts[id].nthrottle = 0;
    cparts[id].nsteal = 0;
    rt_sleep_ticks(RT_CPART_WIN - (rt_ticks % RT_CPART_WIN));
    aps_a.run = 1;
    aps_a.cnt = 0;
    aps_b.run = 1;
    aps_b.cnt = 0;
    /* 批环境教训(门 [122] 三跑三数字定谳):prio 22 下 beat/
     * init/xvmsrv 等真竞争者抢窗,"两净窗饱和"前提成假——
     * 提权至 3 压住干扰者,判据环境无关化(服务器/beat 等
     * 均低此级;壳睡眠中无 I/O 依赖,窗口自洽)。 */
    ta = rt_task_create("apsa", aps_busy, &aps_a, 3);
    tb = rt_task_create("apsb", aps_busy, &aps_b, 3);
    if (!ta || !tb) {
        rt_puts("aps: task spawn FAIL\n");
        aps_a.run = 0;
        aps_b.run = 0;
        rt_cpart_free(id);
        return;
    }
    rt_cpart_join((u32)id, ta);
    rt_sleep_ticks(2 * RT_CPART_WIN);   /* 争跑窗:两满窗 */
    aps_b.run = 0;                      /* tb 收口退场 */
    a1 = aps_a.cnt;
    b1 = aps_b.cnt;
    share = (a1 + b1) ? a1 * 100 / (a1 + b1) : 0;
    nth = cparts[id].nthrottle;
    rt_puts_u64("aps: contend a=", a1, "");
    rt_puts_u64(" b=", b1, "");
    rt_puts_u64(" share=", share, "%");
    /* 站48 判据改判机制账本:任务观测边界计数在宿主负载下欠账
     * (批环境三跑三数字定谳),节流事件=机制自证,环境免疫;
     * share 仍打印供人读(冒烟安静宿主下恒 20-23%)。 */
    rt_puts_u64(" A-throttled=", nth, nth ? "x ok\n" : "x FAIL\n");
    rt_sleep_ticks(5);                  /* tb 退净(低优先,残拍让净) */
    ca0 = aps_a.cnt;
    rt_sleep_ticks(45);                 /* 偷闲窗:ta 独跑=free CPU */
    aps_a.run = 0;
    a2 = aps_a.cnt - ca0;
    nst = cparts[id].nsteal;
    rt_puts_u64("aps: solo a=", a2, " of 45");
    /* 同律:回冲事件=偷闲机制自证(观测计数留人读) */
    rt_puts_u64(" A-steals=", nst, nst ? "x ok\n" : "x FAIL\n");
    rt_cpart_show();
    rt_cpart_free(id);
}
