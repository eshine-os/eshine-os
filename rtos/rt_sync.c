/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "rtos.h"

/* 同步原语(第十七站):每对象 FIFO 等待队列,参考RTOS 公开设计为基准的
 * 最小形态。契约:take 是重检环(醒后自己复核条件),give 让渡资源并
 * 唤醒队头——"唤醒=允许重试,非授予";被醒者优先级更高则立即让位。
 *
 * 临界段:count/owner/wq 的检查-修改与队列链接都在 DAIF 短临界段内
 * (rt_daif_save/rt_daif_load,ISR 语境 restore 不泄掩码)——tick 抢
 * 切能把另一个任务插进两条指令之间:check+dec 不封窗会被双取到负数;
 * 队列双写者交错会挤丢任务。rt_sched()/WFI 永不进临界段。 */

void rt_wq_push(rt_task_t **head, rt_task_t **tail, rt_task_t *t)
{
    t->wq_next = NULL;
    if (!*head)
        *head = t;
    else
        (*tail)->wq_next = t;
    *tail = t;
}

static rt_task_t *wq_pop(rt_task_t **head, rt_task_t **tail)
{
    rt_task_t *t = *head;

    if (!t)
        return NULL;
    *head = t->wq_next;
    if (!*head)
        *tail = NULL;
    t->wq_next = NULL;
    return t;
}

/* 挂等:BLOCK 入对象队列再让位;醒来在调用点后继续(take 重检环)。
 * 站19:挂等前清自拣令牌——否则 rt_sched 摘到自己即"醒",重检环被
 * 令牌无限短路,每次假醒再 wq_push 会把等待队列推成自环。
 * 站21 起 take 族全部走单窗形态(复核+登记+挂链+摘 rdy 同一 DAIF
 * 窗),本原语退役——语义已并入 rt_sem_take_to 等。 */

/* 唤醒队头;更高优先就位即让(统一帧:rt_sched 调用点即续跑点)。
 * 站19B 起导出:rt_ipc.c 队列同机器。
 * 站21A:弹头懒跳过——超时已离队(或任何非 BLOCK 的僵尸头)不醒,
 * 弹到第一个真等待者为止(纵深防御,主防线是扫描的自摘除)。 */
void rt_handoff(rt_task_t **head, rt_task_t **tail)
{
    rt_task_t *w;
    u64 d = rt_daif_save();

    do {
        w = wq_pop(head, tail);
    } while (w && w->state != RT_BLOCK);
    /* 站21 实现评审:弹头后、ready 前的无屏窗里 deadline 恰到期,
     * tick 扫描会按"BLOCK+武装"把已弹出的等待者再推一遍(双挂)。
     * ready 入队并入窗内(弹+推原子),rt_sched 仍在窗外(调度器
     * 契约:rt_sched 永不进临界段)。 */
    if (w)
        rt_ready_push(w);
    rt_daif_load(d);
    if (w && w->prio < rt_cur->prio)
        rt_sched();
}

/* 站21 实现评审收治:成功出口清挂等登记。在时醒成功路径五字段保持
 * 武装,任务随后经非 _to 路径阻塞(壳 RX 的 wait_mask 型/事件注册
 * 表型)时,扫描门 BLOCK+deadline!=0 会在 stale 截止那一拍误发
 * 幻影超时醒——恰好打穿"wait_mask/事件型一律免疫"的自述;事件
 * 等待者更重:伪醒后 READY 仍登记,rt_event_set 无条件再推=rdy
 * 双挂腐化(站19 pidemo 定谳级)。四个 _to 环 got 出口统一缴械;
 * 超时出口本就清;重挂路径全量重登记,缴械零语义代价。 */
void rt_pend_disarm(rt_task_t *t)
{
    t->pend_deadline = 0;
    t->pend_qh = NULL;
    t->pend_qt = NULL;
    t->pend_owner = NULL;
    t->timed_out = 0;
}

/* 站21A:扫描侧超时摘除+owner PI 重算。评审核翻案:v1"give 侧自然
 * 回收"不成立——boost 只写 owner->prio,base_prio 全树唯一回收点
 * 在 give;owner 永不放锁(超时面要防的病)即 boost 无界滞留。修订
 * 与 give 同不变量:等待集任一变化必须重算 owner(回 base 再按
 * 剩余等待者取最强,READY 者摘出重挂)。ISR 单写者语境(demo 全
 * 核 0 亲和;跨核 owner/give 落 §5 残窗)。
 * 残余记档:owner 退出时持有锁(泄漏锁,程序已错)且槽位复用后,
 * stale pend_owner 重算可误写无辜新任务优先级——无锁登记表不可
 * 得,泄漏锁本身已破坏同步面,demo 尺度接受。 */
void rt_timeout_unpend(rt_task_t *t)
{
    rt_task_t **head = t->pend_qh;
    rt_task_t *w = *head;
    rt_task_t *pred = NULL;

    while (w && w != t) {
        pred = w;
        w = w->wq_next;
    }
    if (!w)
        return;                     /* 不在队(give 已摘,迟来扫描无害) */
    if (pred)
        pred->wq_next = t->wq_next;
    else
        *head = t->wq_next;
    if (*t->pend_qt == t)
        *t->pend_qt = pred;          /* t 是尾:前驱即新尾(空队=NULL) */
    t->wq_next = NULL;
    if (t->pend_owner) {
        rt_task_t *o = t->pend_owner;
        u32 np = o->base_prio;

        w = *head;
        while (w) {
            if (w->prio < np)
                np = w->prio;
            w = w->wq_next;
        }
        if (np != o->prio) {
            if (o->state == RT_READY)
                rdy_remove(o);
            o->prio = np;
            if (o->state == RT_READY)
                rt_ready_push(o);
        }
    }
}

void rt_sem_init(rt_sem_t *s, long init)
{
    s->count = init;
    s->wq_head = NULL;
    s->wq_tail = NULL;
}

/* 站21A:超时 take 环。单窗形态(评审核:旧形态检查窗与 rt_pend 窗
 * 之间 gap 可被 tick 切走,窗内 give=丢醒→永久吊死/错报超时):
 * 复核失败→五字段登记→挂链→摘 rdy 一气呵成于同一 DAIF 窗;deadline
 * 由 API 入口一次算死(锚点=调用点,重挂恒用首绝对值,抢占不漂移)。
 * 参考RTOS 公开语义:ticks==0=no-wait 单次复核即判负;RT_WAIT_FOREVER
 * =永不超时(pend_deadline=0 哨兵,扫描门免疫)。 */
long rt_sem_take_to(rt_sem_t *s, u64 ticks)
{
    u64 deadline = (ticks == RT_WAIT_FOREVER) ? 0 : rt_ticks + ticks;

    for (;;) {
        int got = 0;
        u64 d = rt_daif_save();

        if (s->count > 0) {
            s->count--;
            got = 1;
        } else if (ticks != 0) {
            rt_cur->state = RT_BLOCK;
            rt_cur->wait_src = RT_WSRC_SEM;  /* 站43:归属列 */
            rt_cur->timed_out = 0;
            rt_cur->pend_deadline = deadline;
            rt_cur->pend_qh = &s->wq_head;
            rt_cur->pend_qt = &s->wq_tail;
            rt_cur->pend_owner = NULL;
            rt_wq_push(&s->wq_head, &s->wq_tail, rt_cur);
            rdy_remove(rt_cur);
        }
        rt_daif_load(d);
        if (got) {
            rt_pend_disarm(rt_cur);     /* 站21 评审:成功出口缴械 */
            return 0;
        }
        if (ticks == 0)
            return RT_TIMEOUT;
        rt_sched();                 /* 挂起了:醒在此处,回环顶重检 */
        if (rt_cur->timed_out) {
            rt_pend_disarm(rt_cur);
            return RT_TIMEOUT;
        }
    }
}

long rt_sem_take(rt_sem_t *s)
{
    return rt_sem_take_to(s, RT_WAIT_FOREVER);
}

long rt_sem_give(rt_sem_t *s)
{
    u64 d = rt_daif_save();

    s->count++;                     /* 无条件记数;被醒者重检环里自己取 */
    rt_daif_load(d);
    rt_handoff(&s->wq_head, &s->wq_tail);
    return 0;
}

void rt_mutex_init(rt_mutex_t *m)
{
    m->owner = NULL;
    m->nest = 0;
    m->wq_head = NULL;
    m->wq_tail = NULL;
}

/* 站21A:互斥超时环,单窗形态同 sem;boost 在同窗内(旧形态 boost
 * 窗与挂等窗之间同样有 gap)。pend_owner 登记锁主——扫描超时摘除
 * 后据此重算 owner(等待集变化不变量,评审核翻案,见
 * rt_timeout_unpend)。锁链不传递(参考RTOS 裁剪,记残余)。 */
long rt_mutex_take_to(rt_mutex_t *m, u64 ticks)
{
    u64 deadline = (ticks == RT_WAIT_FOREVER) ? 0 : rt_ticks + ticks;

    for (;;) {
        int got = 0;
        u64 d = rt_daif_save();

        if (m->owner == NULL) {
            m->owner = rt_cur;
            m->nest = 1;
            /* 站21 实现评审收治:获取即继承。give 只弹一个等待者,
             * 剩余等待者的 pend_owner 仍指旧主(移交后 stale)——
             * 其超时重算会误写已与锁无关的旧主;且新主从不继承剩余
             * 等待者的优先级(PI 在交接处断链)。获取分支顺扫等待
             * 队:pend_owner 重指新主(锁在持态下等待者恒指现任),
             * 自身按剩余等待者取最强自提(勿照抄 give 先弹后算形)。 */
            {
                rt_task_t *w = m->wq_head;
                u32 np = rt_cur->base_prio;

                while (w) {
                    w->pend_owner = rt_cur;
                    if (w->prio < np)
                        np = w->prio;
                    w = w->wq_next;
                }
                if (np != rt_cur->prio) {
                    if (rt_cur->state == RT_READY)
                        rdy_remove(rt_cur);
                    rt_cur->prio = np;
                    if (rt_cur->state == RT_READY)
                        rt_ready_push(rt_cur);
                }
            }
            got = 1;
        } else if (m->owner == rt_cur) {   /* 递归:同 owner 重入计数 */
            m->nest++;
            got = 1;
        } else if (ticks != 0) {
            /* 站十八B:单级 PI——挂等前把 owner 提到我的优先级
             * (取最强,重复 boost 不降级);READY 链上位图随 prio 摘挂 */
            if (m->owner->prio > rt_cur->prio) {
                rt_task_t *o = m->owner;

                if (o->state == RT_READY)
                    rdy_remove(o);
                o->prio = rt_cur->prio;
                if (o->state == RT_READY)
                    rt_ready_push(o);
            }
            rt_cur->state = RT_BLOCK;
            rt_cur->wait_src = RT_WSRC_MUT;  /* 站43:归属列 */
            rt_cur->timed_out = 0;
            rt_cur->pend_deadline = deadline;
            rt_cur->pend_qh = &m->wq_head;
            rt_cur->pend_qt = &m->wq_tail;
            rt_cur->pend_owner = m->owner;
            rt_wq_push(&m->wq_head, &m->wq_tail, rt_cur);
            rdy_remove(rt_cur);
        }
        rt_daif_load(d);
        if (got) {
            rt_pend_disarm(rt_cur);     /* 站21 评审:成功出口缴械 */
            return 0;
        }
        if (ticks == 0)
            return RT_TIMEOUT;
        rt_sched();
        if (rt_cur->timed_out) {
            rt_pend_disarm(rt_cur);
            return RT_TIMEOUT;
        }
    }
}

long rt_mutex_take(rt_mutex_t *m)
{
    return rt_mutex_take_to(m, RT_WAIT_FOREVER);
}

long rt_mutex_give(rt_mutex_t *m)
{
    rt_task_t *w;
    u64 d = rt_daif_save();

    if (m->owner != rt_cur) {
        rt_daif_load(d);
        return -1;                   /* 非拥有者:礼貌拒绝(判据用) */
    }
    if (--m->nest > 0) {
        rt_daif_load(d);
        return 0;
    }
    /* 站十八B:还继承——先回 base,再按剩余挂等者取最强重升(多等待
     * 者公平);本人 READY(被切走的 RR 语境)则摘出重挂。 */
    {
        rt_task_t *w = m->wq_head;
        u32 np = rt_cur->base_prio;
        u64 d2 = rt_daif_save();

        while (w) {
            if (w->prio < np)
                np = w->prio;
            w = w->wq_next;
        }
        if (np != rt_cur->prio) {
            if (rt_cur->state == RT_READY)
                rdy_remove(rt_cur);
            rt_cur->prio = np;
            if (rt_cur->state == RT_READY)
                rt_ready_push(rt_cur);
        }
        rt_daif_load(d2);
    }
    m->owner = NULL;                 /* 归零让渡;被醒者重检环里重占 */
    w = wq_pop(&m->wq_head, &m->wq_tail);
    if (w)
        rt_ready_push(w);             /* 站21 评审:弹+推原子(弹后 ready
                                      * 前的无屏窗可被 tick 扫描按
                                      * BLOCK+stale deadline 双挂) */
    rt_daif_load(d);
    if (w && w->prio < rt_cur->prio)
        rt_sched();
    return 0;
}
