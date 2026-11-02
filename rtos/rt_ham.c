/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "rtos.h"
#include "eshine_log.h"

/* 站五十二:ham 健康面 v0(参考实现 ham 白描的最小站内形,批次 A 第四站)。
 * 参考实现面:实体/条件(state 型+raise 型)/动作五型/心跳三态/自愈守护。
 * 站内 v0:心跳实体(名+周期+最近拍)——missed 条件(2 周期无拍=>
 * MISSED 态+log 动作[elog 入环+控制台])+恢复拍=>HEALTHY(动作=
 * 记录);动作五型取 log 型,command/signal/message/wait 型=后弧记档;
 * raise 型条件与自愈守护=后弧。与治理面(站 51)互为表里:治理面管
 * 怎么起,健康面管活着没。 */

LOG_MODULE_REGISTER("ham");

#define HAM_MAX 4u
#define HAM_NAME 12u

struct ham_ent {
    char name[HAM_NAME];
    u32 period;                    /* 期望拍距(tick) */
    u32 last_beat;                 /* 最近拍(tick) */
    u8 state;                      /* 0=HEALTHY 1=MISSED */
    u8 used;
};
static struct ham_ent ham_tab[HAM_MAX];
static unsigned long ham_lk;

static const char *ham_st(u8 s)
{
    return s ? "MISSED" : "HEALTHY";
}

static struct ham_ent *ham_find(const char *name, u32 len)
{
    u32 i;

    for (i = 0; i < HAM_MAX; i++) {
        u32 j = 0;

        if (!ham_tab[i].used)
            continue;
        while (j < len && ham_tab[i].name[j] && ham_tab[i].name[j] == name[j])
            j++;
        if (j == len && ham_tab[i].name[len] == '\0')
            return &ham_tab[i];
    }
    return NULL;
}

long k_ham_add(const char *uname, u32 period)
{
    char nm[HAM_NAME];
    u32 n = 0, i;
    struct ham_ent *e;
    u64 d;

    while (uname && uname[n] && n < sizeof(nm) - 1)
        n++;
    if (!n || !period)
        return -22;
    for (i = 0; i < n; i++)
        nm[i] = uname[i];
    nm[n] = '\0';
    d = rt_spin_lock(&ham_lk);
    e = ham_find(nm, n);           /* 重名=复用(重注册=重整周期+清拍) */
    if (!e) {
        for (i = 0; i < HAM_MAX; i++)
            if (!ham_tab[i].used)
                break;
        if (i == HAM_MAX) {
            rt_spin_unlock(&ham_lk, d);
            return -12;            /* 表满 */
        }
        e = &ham_tab[i];
    }
    for (i = 0; i < n; i++)
        e->name[i] = nm[i];
    e->name[n] = '\0';
    e->period = period;
    e->last_beat = (u32)rt_ticks;
    e->state = 0;
    e->used = 1;
    rt_spin_unlock(&ham_lk, d);
    return 0;
}

long k_ham_beat(const char *uname)
{
    char nm[HAM_NAME];
    u32 n = 0, i;
    struct ham_ent *e;
    u64 d;

    while (uname && uname[n] && n < sizeof(nm) - 1)
        n++;
    if (!n)
        return -22;
    for (i = 0; i < n; i++)
        nm[i] = uname[i];
    nm[n] = '\0';
    d = rt_spin_lock(&ham_lk);
    e = ham_find(nm, n);
    if (!e) {
        rt_spin_unlock(&ham_lk, d);
        return -2;
    }
    e->last_beat = (u32)rt_ticks;
    if (e->state) {
        e->state = 0;
        rt_puts("ham: ");
        rt_puts(e->name);
        rt_puts(" recovered\n");
        LOG_WRN("ham %s recovered\n", e->name);
    }
    rt_spin_unlock(&ham_lk, d);
    return 0;
}

void k_ham_ls(void)
{
    u64 d = rt_spin_lock(&ham_lk);
    u32 i;

    rt_puts("ham entities:\n");
    for (i = 0; i < HAM_MAX; i++) {
        if (!ham_tab[i].used)
            continue;
        rt_puts("  ");
        rt_puts(ham_tab[i].name);
        rt_puts(": ");
        rt_puts(ham_st(ham_tab[i].state));
        rt_puts_u64(" period=", ham_tab[i].period, "\n");
    }
    rt_spin_unlock(&ham_lk, d);
}

/* 1s 扫描:2 周期无拍 => MISSED+log 动作(elog 环+控制台) */
static void ham_task(void *arg)
{
    (void)arg;
    for (;;) {
        u64 d = rt_spin_lock(&ham_lk);
        u32 i;

        for (i = 0; i < HAM_MAX; i++) {
            struct ham_ent *e = &ham_tab[i];

            if (!e->used || e->state)
                continue;
            if ((u32)rt_ticks - e->last_beat > e->period * 2u) {
                e->state = 1;
                rt_spin_unlock(&ham_lk, d);
                rt_puts("ham: ");
                rt_puts(e->name);
                rt_puts(" MISSED\n");
                LOG_WRN("ham %s missed\n", e->name);
                d = rt_spin_lock(&ham_lk);
            }
        }
        rt_spin_unlock(&ham_lk, d);
        rt_sleep_ticks(100);        /* 1s 扫描拍 */
    }
}

void rt_ham_boot(void)
{
    if (!rt_task_create("hamsrv", ham_task, NULL, 9))
        rt_puts("srv: hamsrv spawn failed\n");
}
