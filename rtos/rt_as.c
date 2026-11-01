/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "rtos.h"

/* 站二十九:进程+地址空间(机制参照参考实现 §7/§8 白描:PGD 高低半区/
 * ASID 位图/CREATE-COPY_UCONTEXT-EXEC_SETUP 三态/帧改写式 exec 内核
 * 不读镜像;零代码拷贝)。
 *
 * VA 分区(l1 槽,2MB 粒度;站 28 两区式的换装):
 *   [0]   内核块 0x40000000 EL1-only —— 恒驻每 mm(含内核 PGD)
 *   [1]   .user 段 0x40200000 —— per-process 表(EL0+EL1 页)
 *   [2..5]页池 0x40400000-0x40C00000(8MB)—— **所有 mm 里 EL1-only
 *         恒等块**:fork 拷贝/mmap 零填的后备,"内核高半区"——内核
 *         任何语境可写,用户永不可见(参考实现高低半区的服务面形状)
 *   [6]   用户 mmap 窗 0x40C00000-0x40E00000 —— per-process 表
 *   [7]   DTB 块 0x40E00000 EL1-only —— 恒驻每 mm(内核 fdt 读)
 * 内核 PGD(mm0):[0]+[2..5]+[7]+设备窗,[1]/[6] 无效——EL0 永不
 * 在内核 PGD 上执行;页池/mmap 的物理隔离由 per-process 表承担。
 *
 * backing 两型:RT_VMA_ID(镜像/栈,VA→同址恒等)、RT_VMA_POOL
 * (mmap ANON:VA(块 6)→页池 PA(块 2..5),lazy 零填)。fork=eager
 * 全拷:父已映页逐页拷入页池,子 VMA 全 POOL 型,物理隔离即本站
 * 存在意义。VMA 外野访原样落 rt_el0_fault 诚实终态面(v0 边界)。 */

#define AS_BLOCK    (2UL << 20)
#define AS_PAGE     (1UL << 12)

#define AS_BLK_USER      1UL
#define AS_BLK_POOL_LO   2UL
#define AS_BLK_POOL_HI   5UL
#define AS_BLK_MMAP      6UL
#define AS_BLK_DTB       7UL
#define AS_BLK_XSPACE    8UL          /* 站36:固定共享窗 0x41000000/2MB */
#define AS_BLK_COUNT     9UL
#define AS_POOL_BASE     0x40400000UL
#define AS_POOL_PAGES    2048UL
#define AS_MMAP_BASE     0x40C00000UL
#define AS_MMAP_END      0x40E00000UL

/* 站36:8 proc×(user+mmap+xspace)=24 槽上限(此前 16=8×2) */
#define AS_L2_SLOTS      24UL
/* RT_AS_VMA_MAX:站 35 起唯一定义驻 rtos.h(32=musl pthread 栈
 * mmap+TLS 图量)。本文件的本地 6UL 从站 29 挂到站 34=存量
 * 编译警告的真身:静默压住 rtos.h 的扩容,pt 第 4 线程栈
 * mmap 在继承 2+loader 2+stdio 1+线 1=6 槽满后即败
 * (mmap-fail+EAGAIN 同脸,内核探针自证定谳)。*/

/* 描述符(站 28 mm0 同款编码;页级描述符低两位 0b11) */
#define AS_ATTRIDX_NORM 0UL
#define AS_AP_BOTH      (1UL << 6)
#define AS_AP_EL1       (0UL)
#define AS_SH_INNER     (3UL << 8)
#define AS_AF           (1UL << 10)
#define AS_DESC_VALID   (1UL << 0)
#define AS_PAGE_DESC(pa) \
    ((pa) | 0x3UL | (AS_ATTRIDX_NORM << 2) | AS_AP_BOTH | \
     AS_SH_INNER | AS_AF)
#define AS_BLOCK_EL1_DESC(pa) \
    ((pa) | AS_DESC_VALID | (AS_ATTRIDX_NORM << 2) | AS_AP_EL1 | \
     AS_SH_INNER | AS_AF)
#define AS_BLOCK_EL1_RO(pa) \
    ((pa) | AS_DESC_VALID | (AS_ATTRIDX_NORM << 2) | \
     ((1UL << 7) /* AP[2] 只读 */ | AS_AP_EL1) | AS_SH_INNER | AS_AF)
#define AS_TABLE_DESC(pa) ((pa) | 0x3UL)

/* freestanding 无 libc:本地原语(字节循环,对齐不管——量小) */
static void as_tlb_flush(void);             /* 站36:shm_clear 先用 */
static void as_zero(void *dst, u64 n)
{
    u8 *d = dst;

    while (n--)
        *d++ = 0;
}
static void as_copy(void *dst, const void *src, u64 n)
{
    const u8 *s = src;
    u8 *d = dst;

    while (n--)
        *d++ = *s++;
}

static struct rt_proc procs[RT_PROC_MAX];
static u32 asid_bitmap;                     /* 位 0 保留=内核 */
static u64 l0_pool[RT_PROC_MAX][512] __attribute__((aligned(4096)));
static u64 l1_pool[RT_PROC_MAX][512] __attribute__((aligned(4096)));
static u64 l2_pool[AS_L2_SLOTS][512] __attribute__((aligned(4096)));
static u32 l2_used;                          /* 16 槽位图 */
static int  l2_owner[AS_L2_SLOTS];           /* 归属 pid(0=free) */
static u64 page_pool[AS_POOL_PAGES / 64];    /* 2048 位位图 */

extern u64 *rt_mm0_l1_dev;                   /* rt_mm0.c:共享设备表 */
extern char __user_start[], __user_end[];    /* 链接器:.user 段界 */

static u32 as_alloc_asid(void)
{
    u32 i;

    for (i = 1; i < 16; i++)
        if (!(asid_bitmap & (1u << i))) {
            asid_bitmap |= 1u << i;
            return i;
        }
    return 0;
}

/* 站40 定谳:exit 从不还 ASID——站 29 注释谎称"已尽释",11 站无雷
 * 只因电池从未超 15 次进程创建;respawn 每命令一拍=第一个高频
 * 循环者,创建第 16 次 proc 即 EAGAIN 连环。还位在 exit(ZOMBIE 期
 * 无人引用;exit 已 as_tlb_flush 全冲,复用无旧标签别名)。 */
static void as_free_asid(u32 asid)
{
    if (asid && asid < 16)
        asid_bitmap &= ~(1u << asid);
}

static u64 *as_l2_alloc(int pid)
{
    u32 i;

    for (i = 0; i < AS_L2_SLOTS; i++) {
        if (!(l2_used & (1u << i))) {
            l2_used |= 1u << i;
            l2_owner[i] = pid;
            as_zero(l2_pool[i], sizeof(l2_pool[i]));
            return l2_pool[i];
        }
    }
    return NULL;
}

static void as_l2_free(struct rt_proc *p)
{
    u32 i, bi;

    for (i = 0; i < AS_L2_SLOTS; i++) {
        if ((l2_used & (1u << i)) && l2_owner[i] == p->pid) {
            l2_used &= ~(1u << i);
            l2_owner[i] = 0;
        }
    }
    for (bi = 0; bi < AS_BLK_COUNT; bi++)
        p->l2[bi] = NULL;
}

static u64 as_pool_alloc(void)
{
    u32 i, j;

    for (i = 0; i < AS_POOL_PAGES / 64; i++) {
        if (page_pool[i] != ~0UL)
            for (j = 0; j < 64; j++)
                if (!(page_pool[i] & (1UL << j))) {
                    page_pool[i] |= 1UL << j;
                    return AS_POOL_BASE +
                           ((u64)i * 64 + j) * AS_PAGE;
                }
    }
    return 0;
}

static int as_pool_owned(u64 pa)
{
    return pa >= AS_POOL_BASE &&
           pa < AS_POOL_BASE + AS_POOL_PAGES * AS_PAGE;
}

static void as_pool_free_if_owned(u64 desc)
{
    u64 pa = desc & ~0xFFFUL;

    if (as_pool_owned(pa)) {
        u64 idx = (pa - AS_POOL_BASE) / AS_PAGE;

        page_pool[idx / 64] &= ~(1UL << (idx % 64));
    }
}

/* 映一页:VA→PA。许可块=1(.user)/6(mmap)/8(xspace 共享窗,
 * 站36);页池/DTB/内核块非 per-process 表管辖(页池=恒驻
 * EL1 块,内核直接写,不经此路)。 */
static int as_map_page(struct rt_proc *p, u64 va, u64 pa)
{
    u32 bi = (u32)((va >> 21) & 511);
    u32 pi = (u32)((va >> 12) & 511);

    if (bi != AS_BLK_USER && bi != AS_BLK_MMAP && bi != AS_BLK_XSPACE)
        return -1;
    if (!p->l2[bi]) {
        p->l2[bi] = as_l2_alloc(p->pid);
        if (!p->l2[bi])
            return -1;
        p->l1[bi] = AS_TABLE_DESC((u64)p->l2[bi]);
    }
    p->l2[bi][pi] = AS_PAGE_DESC(pa);
    return 0;
}

/* ---- 站36 暴露面:rt_shm.c 消费(段页取/还/映/清+SHM VMA 申报) ----
 * 页归属铁律:段页自页池取出后归段注册表所有,唯一还池入口=
 * rt_as_page_free(由 rt_shm_destroy 调);fork/munmap/退场三路
 * 页回收皆不触块 8(munmap 拒 SHM VMA;退场页回收只走块 1/6)。 */
u64 rt_as_page_alloc(void)
{
    return as_pool_alloc();
}

/* 站46:用户 VA 映射查(crash 用户帧链的逐读映射门——坏栈不得
 * 把取证变成 EL1 二次事故;只认 user/mmap 两块=xspage 不可
 * 走访面同律)。 */
int rt_as_user_mapped(struct rt_proc *p, u64 va)
{
    u32 bi = (u32)((va >> 21) & 511);
    u32 pi = (u32)((va >> 12) & 511);

    if (!p)
        return 0;
    if (bi != AS_BLK_USER && bi != AS_BLK_MMAP)
        return 0;
    return p->l2[bi] && (p->l2[bi][pi] & 1UL) != 0;
}

void rt_as_page_free(u64 pa)
{
    as_pool_free_if_owned(pa & ~0xFFFUL);
}

int rt_as_map_user(struct rt_proc *p, u64 va, u64 pa)
{
    return as_map_page(p, va, pa);
}

int rt_as_declare_shm(struct rt_proc *p, u64 start, u64 end, u32 shm_id)
{
    if (p->nvma >= RT_AS_VMA_MAX || start >= end)
        return -1;
    p->vma[p->nvma].start = start;
    p->vma[p->nvma].end = end;
    p->vma[p->nvma].pool = 2;
    p->vma[p->nvma].shm_id = shm_id;
    p->nvma++;
    return 0;
}

/* SHM 段页清映(不还池!)+整段 TLB 冲(段页属主=段注册表) */
void rt_as_shm_clear(struct rt_proc *p, u64 start, u64 end)
{
    u32 bi = (u32)((start >> 21) & 511);
    u32 pi, pi_end = (u32)((end >> 12) & 511);

    if (!p->l2[bi])
        return;
    for (pi = (u32)((start >> 12) & 511); pi < pi_end; pi++)
        p->l2[bi][pi] = 0;
    as_tlb_flush();
}

static void as_tlb_flush(void)
{
    __asm__ volatile("dsb nshst\n\t"
                     "tlbi vmalle1\n\t"
                     "dsb nsh\n\t"
                     "isb" ::: "memory");
}

/* 建 L0/L1 半区:设备窗+内核块+页池块+DTB 块恒驻;.user/mmap 槽空表 */
static void as_build_tables(struct rt_proc *p)
{
    u64 b;

    as_zero(p->l0, 512 * 8);
    as_zero(p->l1, 512 * 8);
    p->l0[0] = AS_TABLE_DESC((u64)rt_mm0_l1_dev);
    p->l0[1] = AS_TABLE_DESC((u64)p->l1);
    p->l1[0] = AS_BLOCK_EL1_DESC(0x40000000UL + 0 * AS_BLOCK);
    for (b = AS_BLK_POOL_LO; b <= AS_BLK_POOL_HI; b++)
        p->l1[b] = AS_BLOCK_EL1_DESC(0x40000000UL + b * AS_BLOCK);
    p->l1[AS_BLK_DTB] = AS_BLOCK_EL1_RO(0x40000000UL +
                                        AS_BLK_DTB * AS_BLOCK);
}

int rt_proc_create(rt_task_t *leader, struct rt_proc **out)
{
    u32 slot;
    struct rt_proc *p;

    for (slot = 0; slot < RT_PROC_MAX; slot++)
        if (!procs[slot].used)
            break;
    if (slot == RT_PROC_MAX)
        return -1;
    p = &procs[slot];
    as_zero(p, sizeof(*p));
    p->used = 1;
    p->state = RT_PROC_CREATE;              /* 挂起:待申报/待 exec */
    p->pid = (int)slot + 1;
    p->asid = as_alloc_asid();
    if (!p->asid) {
        p->used = 0;
        return -1;
    }
    p->l0 = l0_pool[slot];
    p->l1 = l1_pool[slot];
    /* 站41:CAPR 生命域——create=零能力(fail-closed 由此起);
     * uuid=随机身份(站 40 混出器同源,非密码级记档);rlimit
     * 默认满额(自降单向)。 */
    p->caps = 0;
    {
        char ub[8];

        rt_rng_fill(ub, sizeof(ub));
        p->uuid = ((u64)ub[0] << 56) | ((u64)ub[1] << 48) |
                  ((u64)ub[2] << 40) | ((u64)ub[3] << 32) |
                  ((u64)ub[4] << 24) | ((u64)ub[5] << 16) |
                  ((u64)ub[6] << 8) | (u64)ub[7];
        if (!p->uuid)
            p->uuid = (u64)p->pid << 32 | 0xA41;   /* 零=换挡 */
    }
    p->max_fd = RT_FD_MAX;
    p->max_child = RT_PROC_MAX - 1;
    p->pgid = p->pid;               /* 站42:leader 自成组(setsid 前) */
    p->sid = p->pid;                /* 站42:会话同律 */
    p->mpart = -1;                  /* 站45:MPRT 未入组(复用槽必清
                                     * ——站38 律第六案:残值 0=幻影
                                     * 分区 0,fork 儿全被 max=0 拒,
                                     * 冒烟四脸连炸定谳) */
    p->mpart_pages = 0;             /* 声明页账由 declare 累计 */
    p->robust_head = 0;             /* 站42:robust 链未注册 */
    p->robust_len = 0;
    p->leader = leader;
    p->mmap_next = AS_MMAP_BASE;
    as_build_tables(p);
    *out = p;
    return 0;
}

u64 rt_proc_ttbr(struct rt_proc *p)
{
    return ((u64)p->asid << 48) | (u64)p->l0;
}

struct rt_proc *rt_proc_of(rt_task_t *t)
{
    return t ? t->proc : NULL;
}

void rt_as_declare(struct rt_proc *p, u64 start, u64 end)
{
    if (p->nvma < RT_AS_VMA_MAX && start < end) {
        p->vma[p->nvma].start = start;
        p->vma[p->nvma].end = end;
        p->vma[p->nvma].pool = 0;           /* ID 恒等(镜像/栈) */
        p->nvma++;
    }
}

static int as_declare_pool(struct rt_proc *p, u64 start, u64 end)
{
    if (p->nvma >= RT_AS_VMA_MAX || start >= end)
        return -1;
    p->vma[p->nvma].start = start;
    p->vma[p->nvma].end = end;
    p->vma[p->nvma].pool = 1;
    p->nvma++;
    /* 站45:MPRT 声明窗记账——mmap 与 fork 同走此咽喉,一钩双面;
     * 超上限回滚 VMA,调用侧落 -ENOMEM(fork=内存隔离带孙面)。 */
    if (rt_mpart_charge(p, (u32)((end - start) / AS_PAGE)) < 0) {
        p->nvma--;
        return -1;
    }
    return 0;
}

static struct rt_vma *as_vma_find(struct rt_proc *p, u64 va)
{
    u32 i;

    for (i = 0; i < p->nvma; i++)
        if (va >= p->vma[i].start && va < p->vma[i].end)
            return &p->vma[i];
    return NULL;
}

/* 缺页补页:VMA 命中=映射(ID 恒等/页池零填),VMA 外=-1(终态面) */
int rt_pfault(u64 va, u64 esr)
{
    struct rt_proc *p = rt_proc_of(rt_cur);
    struct rt_vma *v;

    (void)esr;
    if (!p)
        return -1;
    v = as_vma_find(p, va);
    if (!v)
        return -1;
    va &= ~(AS_PAGE - 1);
    if (!v->pool)
        return as_map_page(p, va, va);      /* identity:VA 即后备 */
    if (v->pool == 2)                       /* 站36:SHM 按段页索引补映 */
        return rt_shm_pfault(p, va, v->shm_id);
    {
        u64 pg = as_pool_alloc();

        if (!pg)
            return -1;
        as_zero((void *)pg, AS_PAGE);       /* ANON 零填;页池恒驻 */
        return as_map_page(p, va, pg);
    }
}

/* boot:进程 1=壳(identity 申报+eager 预映——首指令前无缺页悬念) */
void rt_proc_boot_shell(rt_task_t *shell)
{
    struct rt_proc *p;
    u64 va, end = ((u64)__user_end + AS_PAGE - 1) & ~(AS_PAGE - 1);

    if (rt_proc_create(shell, &p) < 0)
        rt_panic("proc: no slot for shell", 0);
    p->caps = CAP_ALL;             /* 站41:壳=全权 boot 域 */
    p->state = RT_PROC_RUN;
    rt_as_declare(p, (u64)__user_start & ~(AS_PAGE - 1), end);
    for (va = (u64)__user_start & ~(AS_PAGE - 1); va < end; va += AS_PAGE)
        if (as_map_page(p, va, va) < 0)
            rt_panic("proc: shell map fail", 0);
    shell->proc = p;
    shell->ttbr = rt_proc_ttbr(p);
    shell->tid = (u64)p->pid;         /* 站35:leader tid=pid */
}

/* fork 的 AS 侧:eager 全拷——父块 1/6 已映页逐页拷入页池,子 VMA
 * 全 POOL 型(拷贝驻页池=物理隔离;父 l2 空洞留给子将来零填)。
 * 拷贝语境=父 mm(父页经其表可读;页池块恒驻 EL1 可写)。
 * 站36:SHM VMA(pool=2)子表申报继承+同物理直挂零拷贝(共享=
 * 隔离的对立面,两立分型;段页永不入下方拷贝环——块 8 不在
 * 块 1/6 走查内)。 */
int rt_proc_fork_as(struct rt_proc *parent, struct rt_proc *child)
{
    u32 i, bi, pi;

    child->mmap_next = parent->mmap_next;
    child->mpart = parent->mpart;   /* 站45:入组承继(参考实现 mprt_fork
                                     * 白描);mpart_pages 从 0 起,
                                     * 由下方 declare 累计 */
    /* 站41:能力/上限承继(fail-closed 世系:无能力子不因
     * fork 得能力);uuid=新身份(资源承继,身份不承)。 */
    child->caps = parent->caps;
    child->max_fd = parent->max_fd;
    child->max_child = parent->max_child;
    child->pgid = parent->pgid;      /* 站42:进程组承继(参考实现白描:
                                      * fork 入组;setsid 面另立) */
    child->sid = parent->sid;
    child->robust_head = 0;          /* 站42:robust 链不承继(Linux 律:
                                      * 每线程自注册,musl 启动即 set) */
    {
        char ub[8];

        rt_rng_fill(ub, sizeof(ub));
        child->uuid = ((u64)ub[0] << 56) | ((u64)ub[1] << 48) |
                      ((u64)ub[2] << 40) | ((u64)ub[3] << 32) |
                      ((u64)ub[4] << 24) | ((u64)ub[5] << 16) |
                      ((u64)ub[6] << 8) | (u64)ub[7];
        if (!child->uuid)
            child->uuid = (u64)child->pid << 32 | 0xA41;
    }
    for (i = 0; i < parent->nvma; i++) {
        if (parent->vma[i].pool == 2) {
            if (rt_as_declare_shm(child, parent->vma[i].start,
                                  parent->vma[i].end,
                                  parent->vma[i].shm_id) < 0)
                return -1;
            continue;
        }
        if (parent->vma[i].pool == 0) {
            /* 站45:identity VMA 走专口——零记账(VMA 本分=VA 即
             * 后备,非匿名窗;旧码一股脑 as_declare_pool 在记
             * 账钩子下会把 .user 块 14 页当匿名预算收) */
            rt_as_declare(child, parent->vma[i].start,
                          parent->vma[i].end);
            continue;
        }
        if (as_declare_pool(child, parent->vma[i].start,
                            parent->vma[i].end) < 0)
            return -1;
    }
    rt_shm_fork(child);                     /* 挂同物理+refs++ */
    for (bi = 0; bi < 8; bi++) {
        if (bi != AS_BLK_USER && bi != AS_BLK_MMAP)
            continue;
        if (!parent->l2[bi])
            continue;
        for (pi = 0; pi < 512; pi++) {
            u64 desc = parent->l2[bi][pi];
            u64 sva, pg;

            if (!(desc & 1UL))
                continue;
            sva = ((u64)bi << 21) + ((u64)pi << 12);
            pg = as_pool_alloc();
            if (!pg)
                return -1;
            as_copy((void *)pg, (void *)(desc & ~0xFFFUL), AS_PAGE);
            if (as_map_page(child, sva, pg) < 0)
                return -1;
        }
    }
    return 0;
}

/* munmap:整段 VMA 摘除+映射页回收+l2 槽清+TLB(v0 语义:须与
 * mmap 返回的段首/长精确匹配——部分解映射不做,诚实拒绝)。 */
int rt_as_munmap(struct rt_proc *p, u64 start, u64 len)
{
    u32 i, bi, pi;
    u64 end = start + len;

    for (i = 0; i < p->nvma; i++) {
        if (p->vma[i].pool == 2)            /* 站36:SHM VMA 不走 munmap
                                             * (段页归段所有,还池唯
                                             * destroy 一口;路由
                                             * shm_detach) */
            continue;
        if (p->vma[i].start == start && p->vma[i].end == end) {
            for (bi = 0; bi < 8; bi++) {
                if (bi != AS_BLK_USER && bi != AS_BLK_MMAP)
                    continue;
                if (!p->l2[bi])
                    continue;
                for (pi = 0; pi < 512; pi++) {
                    u64 va = ((u64)bi << 21) + ((u64)pi << 12);

                    if (va >= start && va < end &&
                        (p->l2[bi][pi] & 1UL)) {
                        as_pool_free_if_owned(p->l2[bi][pi]);
                        p->l2[bi][pi] = 0;
                    }
                }
            }
            /* VMA 摘除(尾移法,nvma 减一)。站45:pool==1 声明窗
             * 退账(identity VMA 不记)。 */
            if (p->vma[i].pool == 1)
                rt_mpart_uncharge(p,
                    (u32)((p->vma[i].end - p->vma[i].start) / AS_PAGE));
            for (; i + 1 < p->nvma; i++)
                p->vma[i] = p->vma[i + 1];
            p->nvma--;
            as_tlb_flush();
            return 0;
        }
    }
    return -1;
}

/* mmap(ANON):VA(块 6)自游标派段,VMA 登记,页 lazy——首访缺页
 * 零填(prot/flags v0 不分档:RW anon)。返回段首,满窗 -ENOMEM
 * 由调用侧转 errno。 */
/* 站33:hint 撞既有 VMA 判(loader 段定位的防御面) */
static int as_vma_hits(struct rt_proc *p, u64 s, u64 e)
{
    u32 i;

    for (i = 0; i < p->nvma; i++)
        if (p->vma[i].start < e && s < p->vma[i].end)
            return 1;
    return 0;
}

u64 rt_as_mmap(struct rt_proc *p, u64 hint, u64 len)
{
    u64 va, end;

    len = (len + AS_PAGE - 1) & ~(AS_PAGE - 1);
    if (!len)
        return 0;
    /* 站33:hint 尊重(loader 段定位契约)——窗内+页对齐+不撞
     * 既有 VMA 即落 hint;否则游标分配(musl mmap(NULL,...) 与
     * 老调用面零感)。游标恒越过 hint 段尾防回头重叠。 */
    if (hint >= AS_MMAP_BASE && (hint & (AS_PAGE - 1)) == 0 &&
        hint + len <= AS_MMAP_END && !as_vma_hits(p, hint, hint + len)) {
        va = hint;
        end = va + len;
    } else {
        if (p->mmap_next + len > AS_MMAP_END)
            return 0;
        va = p->mmap_next;
        end = va + len;
    }
    if (as_declare_pool(p, va, end) < 0)
        return 0;
    if (p->mmap_next < end)
        p->mmap_next = end;
    return va;
}

/* wait4 后端:按 pid 找 ZOMBIE(带 exit_code 出参);收尸=槽全回收
 * (exit 时页/L2/ASID 已尽释,此处只清登记)。 */
struct rt_proc *rt_proc_zombie_find(int pid, int *exit_code)
{
    u32 i;

    for (i = 0; i < RT_PROC_MAX; i++) {
        if (procs[i].used && procs[i].state == RT_PROC_ZOMBIE &&
            procs[i].pid == pid) {
            *exit_code = procs[i].exit_code;
            return &procs[i];
        }
    }
    return NULL;
}

void rt_proc_reap(struct rt_proc *p)
{
    p->used = 0;
}

/* 站41:rlimit 面——本进程在册子数(ZOMBIE 计,fork 满额判) */
u32 rt_proc_child_count(struct rt_proc *p)
{
    u32 i, n = 0;

    if (!p)
        return 0;
    for (i = 0; i < RT_PROC_MAX; i++)
        if (procs[i].used && procs[i].parent == p->pid)
            n++;
    return n;
}

/* 站40:wait4 WNOHANG 判活(1=活;0=非此子——zombie 侦收在前,
 * 此面只答"还不值得等"与"等不到") */
long rt_proc_child_alive(struct rt_proc *p, int pid)
{
    u32 i;

    (void)p;
    for (i = 0; i < RT_PROC_MAX; i++)
        if (procs[i].used && procs[i].pid == pid &&
            procs[i].state != RT_PROC_ZOMBIE)
            return 1;
    return 0;
}

/* 站三十二:活进程查(kill 面;ZOMBIE=ESRCH——死者不收信号) */
struct rt_proc *rt_proc_lookup(int pid)
{
    u32 i;

    for (i = 0; i < RT_PROC_MAX; i++)
        if (procs[i].used && procs[i].state != RT_PROC_ZOMBIE &&
            procs[i].pid == pid)
            return &procs[i];
    return NULL;
}

/* 退场:自有页池页回收+L2 清归池;TLB 全冲(销毁面 v0 从简)。
 * 站36:SHM 引用先卸(逐 VMA refs--;块 8 段页不进下方回收=
 * 归属铁律;L2 槽由 as_l2_free 归池,陈 PTE 随再分配清零)。 */
void rt_proc_exit(struct rt_proc *p, int code)
{
    u32 bi, pi;

    p->exit_code = code;
    p->state = RT_PROC_ZOMBIE;
    rt_mpart_exit_all(p);         /* 站45:声明窗全退(fork 承继同律) */
    as_free_asid(p->asid);
    p->asid = 0;
    rt_shm_exit(p);
    for (bi = 0; bi < 8; bi++) {
        if (bi != AS_BLK_USER && bi != AS_BLK_MMAP)
            continue;
        if (!p->l2[bi])
            continue;
        for (pi = 0; pi < 512; pi++) {
            if (p->l2[bi][pi] & 1UL)
                as_pool_free_if_owned(p->l2[bi][pi]);
            p->l2[bi][pi] = 0;
        }
        p->l2[bi] = NULL;
        p->l1[bi] = 0;
    }
    as_l2_free(p);
    as_tlb_flush();
}

/* ---- syscall 面(rt_syscall.c 调) ---- */

long sys_exec_setup_impl(u64 *frame, u64 entry, u64 sp,
                         u64 a0, u64 a1, u64 a2, u64 a3)
{
    /* 7-参帧改写(参考实现 EXEC_SETUP 形状):pc/sp/x0..x3 直写保存帧,
     * psr=EL0t 未掩。内核不读任何镜像字节——by construction。 */
    frame[0] = entry;                       /* elr 槽 */
    frame[1] = 0;                           /* psr=EL0t(DAIF 开) */
    frame[2] = a0;                          /* x0@16 */
    frame[3] = 0;                           /* x0 槽的 8B hole */
    frame[4] = a1;                          /* x1@24+8*1=32 */
    frame[5] = a2;
    frame[6] = a3;
    /* x4..x30 槽清零:壳 wrapper 只递 3 参(x0-x2),余槽残值不许
     * 进新帧(帧改写=整帧卫生,曾只写前三槽的残值面入档)。 */
    for (u32 i = 7; i < 34; i++)
        frame[i] = 0;
    /* 用户 SP:任务 usp 更新(eret 面 vec.S 自 SP_EL0 装还) */
    rt_cur->usp = sp;
    /* 返回值=a0:分发器的 FR(0)=返回值回写发生在本 impl 之后——
     * 曾返 0 把 frame[2] 的 arg 覆成 0(phase2 arg=0 实测自雷)。
     * exec 语义里"返回值"本就是新语境的 x0,返 a0 = 分发器替
     * 本面完成最后一笔帧改写,一字两用。 */
    return (long)a0;
}
