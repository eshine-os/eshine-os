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
#define AS_POOL_BASE     0x40400000UL
#define AS_POOL_PAGES    2048UL
#define AS_MMAP_BASE     0x40C00000UL
#define AS_MMAP_END      0x40E00000UL

#define AS_L2_SLOTS      16UL
#define RT_AS_VMA_MAX       6UL

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
    for (bi = 0; bi < 8; bi++)
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

/* 映一页:VA→PA。许可块=1(.user)/6(mmap);页池/DTB/内核块非
 * per-process 表管辖(页池=恒驻 EL1 块,内核直接写,不经此路)。 */
static int as_map_page(struct rt_proc *p, u64 va, u64 pa)
{
    u32 bi = (u32)((va >> 21) & 511);
    u32 pi = (u32)((va >> 12) & 511);

    if (bi != AS_BLK_USER && bi != AS_BLK_MMAP)
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
    p->state = RT_PROC_RUN;
    rt_as_declare(p, (u64)__user_start & ~(AS_PAGE - 1), end);
    for (va = (u64)__user_start & ~(AS_PAGE - 1); va < end; va += AS_PAGE)
        if (as_map_page(p, va, va) < 0)
            rt_panic("proc: shell map fail", 0);
    shell->proc = p;
    shell->ttbr = rt_proc_ttbr(p);
}

/* fork 的 AS 侧:eager 全拷——父块 1/6 已映页逐页拷入页池,子 VMA
 * 全 POOL 型(拷贝驻页池=物理隔离;父 l2 空洞留给子将来零填)。
 * 拷贝语境=父 mm(父页经其表可读;页池块恒驻 EL1 可写)。 */
int rt_proc_fork_as(struct rt_proc *parent, struct rt_proc *child)
{
    u32 i, bi, pi;

    child->mmap_next = parent->mmap_next;
    for (i = 0; i < parent->nvma; i++)
        if (as_declare_pool(child, parent->vma[i].start,
                            parent->vma[i].end) < 0)
            return -1;
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
            /* VMA 摘除(尾移法,nvma 减一) */
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
u64 rt_as_mmap(struct rt_proc *p, u64 len)
{
    u64 va, end;

    len = (len + AS_PAGE - 1) & ~(AS_PAGE - 1);
    if (!len || p->mmap_next + len > AS_MMAP_END)
        return 0;
    va = p->mmap_next;
    end = va + len;
    if (as_declare_pool(p, va, end) < 0)
        return 0;
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

/* 退场:自有页池页回收+L2 清归池;TLB 全冲(销毁面 v0 从简) */
void rt_proc_exit(struct rt_proc *p, int code)
{
    u32 bi, pi;

    p->exit_code = code;
    p->state = RT_PROC_ZOMBIE;
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
