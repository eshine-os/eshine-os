/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "rtos.h"

/* 站三十六:shm+xspace——内核登记共享段。参考实现机制白描参照
 * (零代码红线):xspace=全局唯一共享 VA 窗(段 i 固定映
 * XSPACE_BASE+(i-1)*SPAN——同段在任何进程同 VA);VM_SET=单拷
 * 窗(载荷走窗=一拷,消息只带引用;免页表遍历在我们的机器里
 * 天然成立:页池恒驻+同物理直挂,不涉 walk)。
 *
 * 三根柱:段注册表(页自页池,create 取页清零,destroy 还);
 * VMA 第三型(pool=2=SHM,shm_id 驻 rsv 槽);fork 律(SHM VMA
 * 子表同物理直挂零拷贝=隐式 attach refs++,其余 VMA 维持站 29
 * eager 全拷——共享与隔离两立)。
 *
 * 页归属铁律:段页归段所有,只走 destroy 还页池;fork/munmap/
 * 进程退场三条页回收路径皆不触段页(munmap 拒 SHM VMA 路由
 * 此处;退场块 1/6 回收不涉块 8)。 */

struct rt_shm_seg {
    u32 used;
    u32 npages;
    u32 refs;
    u64 pa[RT_SHM_SEG_PAGES];
};

static struct rt_shm_seg shm_tab[RT_SHM_MAX];

static struct rt_shm_seg *seg_of(long seg)
{
    if (seg < 1 || seg > RT_SHM_MAX || !shm_tab[seg - 1].used)
        return NULL;
    return &shm_tab[seg - 1];
}

static void shm_zero_page(u64 pa)
{
    u64 *p = (u64 *)(unsigned long)pa;   /* 页池恒驻 EL1(恒等块) */
    u32 i;

    for (i = 0; i < 4096 / 8; i++)
        p[i] = 0;
}

/* create:len 字节->段号。页自页池即取即清(参考实现 PMAP 首映者
 * 分配的我们形状:内核登记即属主,免 VALLOC 占位阶段)。 */
long rt_shm_create(u32 len_bytes)
{
    u32 pages = (len_bytes + 4095u) / 4096u;
    struct rt_shm_seg *s = NULL;
    u32 i;

    if (!pages || pages > RT_SHM_SEG_PAGES)
        return -22;
    for (i = 0; i < RT_SHM_MAX; i++) {
        if (!shm_tab[i].used) {
            s = &shm_tab[i];
            break;
        }
    }
    if (!s)
        return -12;                      /* -ENOMEM:段表满 */
    for (i = 0; i < pages; i++) {
        u64 pa = rt_as_page_alloc();

        if (!pa) {
            while (i--)
                rt_as_page_free(s->pa[i]);
            return -12;
        }
        s->pa[i] = pa;
        shm_zero_page(pa);
    }
    s->used = 1;
    s->npages = pages;
    s->refs = 0;
    return (long)(s - shm_tab) + 1;
}

/* attach:(seg,va=0) -> 实际 VA。固定窗落位=同段任何进程同 VA;
 * 重复挂同段拒(一进程一挂,v0 诚实)。eager 全映+VMA 申报,
 * pfault 面只作健壮后备。 */
long rt_shm_attach(struct rt_proc *p, long seg, u64 va)
{
    struct rt_shm_seg *s = seg_of(seg);
    u64 base;
    u32 i;

    if (!s || !p)
        return -22;
    if (va)
        return -22;                      /* v0:仅固定窗落位 */
    base = RT_XSPACE_BASE + ((u64)(seg - 1) * RT_XSPACE_SPAN);
    {
        u32 j;

        for (j = 0; j < p->nvma; j++)
            if (p->vma[j].pool == 2 && p->vma[j].shm_id == (u32)seg)
                return -17;              /* -EEXIST:重复挂 */
    }
    if (rt_as_declare_shm(p, base, base + (u64)s->npages * 4096,
                          (u32)seg) < 0)
        return -12;
    for (i = 0; i < s->npages; i++)
        if (rt_as_map_user(p, base + (u64)i * 4096, s->pa[i]) < 0)
            return -12;                  /* L2 池满(申报已入,退场钩收) */
    s->refs++;
    return (long)base;
}

/* detach:摘挂(VMA 内页清映不还池+摘 VMA+refs--)。段页归段,
 * 唯 destroy 还池——munmap 对 SHM VMA 拒绝后路由至此。 */
long rt_shm_detach(struct rt_proc *p, long seg)
{
    struct rt_shm_seg *s = seg_of(seg);
    u32 i;

    if (!s || !p)
        return -22;
    for (i = 0; i < p->nvma; i++) {
        if (p->vma[i].pool == 2 && p->vma[i].shm_id == (u32)seg) {
            rt_as_shm_clear(p, p->vma[i].start, p->vma[i].end);
            for (; i + 1 < p->nvma; i++)
                p->vma[i] = p->vma[i + 1];
            p->nvma--;
            s->refs--;
            return 0;
        }
    }
    return -22;                          /* -EINVAL:未挂 */
}

/* destroy:refs 非零拒;零则页还池段表清。退场钩保证进程死
 * 不漏引用(挂着的进程死=detach 面,不触发段销毁)。 */
long rt_shm_destroy(long seg)
{
    struct rt_shm_seg *s = seg_of(seg);
    u32 i;

    if (!s)
        return -22;
    if (s->refs)
        return -16;                      /* -EBUSY:仍有挂者 */
    for (i = 0; i < s->npages; i++)
        rt_as_page_free(s->pa[i]);
    s->used = 0;
    return 0;
}

/* pfault 健全面:SHM VMA 内的缺页=按段页索引补映(eager 全映
 * 后理论上不再到这;挂/摘竞窗的兜底)。 */
int rt_shm_pfault(struct rt_proc *p, u64 va, u32 shm_id)
{
    struct rt_shm_seg *s = seg_of((long)shm_id);

    if (!s || !p)
        return -1;
    return rt_as_map_user(p, va, s->pa[(va - (RT_XSPACE_BASE +
           (u64)(shm_id - 1) * RT_XSPACE_SPAN)) >> 12]);
}

/* fork 钩:子 VMA 表里 pool=2 者(父表申报继承)逐段挂同物理
 * 页=零拷贝共享,refs++(隐式 attach)。 */
void rt_shm_fork(struct rt_proc *child)
{
    u32 i, k;

    for (i = 0; i < child->nvma; i++) {
        struct rt_shm_seg *s;

        if (child->vma[i].pool != 2)
            continue;
        s = seg_of((long)child->vma[i].shm_id);
        if (!s)
            continue;
        for (k = 0; k < s->npages; k++)
            rt_as_map_user(child, child->vma[i].start + (u64)k * 4096,
                           s->pa[k]);
        s->refs++;
    }
}

/* 退场钩:逐 SHM VMA 卸引用(进程死=所有挂面收回;段页不动,
 * L2 槽由 as_l2_free 归池)。 */
void rt_shm_exit(struct rt_proc *p)
{
    u32 i;

    for (i = 0; i < p->nvma; i++)
        if (p->vma[i].pool == 2) {
            struct rt_shm_seg *s = seg_of((long)p->vma[i].shm_id);

            if (s && s->refs)
                s->refs--;
        }
}
