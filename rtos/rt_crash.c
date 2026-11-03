/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "rtos.h"

/* 站四十六:崩溃取证链——参考实现 coredump 白描的站内形状(v0=内存
 * 记录+shell 倾倒)。站六十四翻案:ELF 核文件可为——ramfs 站 39
 * COW(页池页)后崩溃语境直写落盘(内核侧 rt_ramfs_core_store,
 * 零消息往返),binfmt_elf 形(ET_CORE+PT_NOTE+NT_PRSTATUS)。
 *
 * 记录点两处:EL0 野访终态(硬件三元全,站29"全系统 panic"留边
 * 翻案=死者本人 exit 链)/信号默认动作(esr/elr/far=0,code=
 * 128+sig)。用户帧链=死者帧 x29 起走,窗门+**映射门**(逐读前
 * l2 PTE present 查——坏栈不得把取证变成 EL1 二次事故)+单调
 * 门,4 帧保险丝。 */

#define RT_CRASH_MAX    2u
#define RT_CRASH_UWALK  4u

/* 用户窗门:identity 块 1 ∪ mmap 窗块 6(与 as_map_page 的
 * 可映射域一致;越窗即止=站43 内核窗门的用户镜像)。 */
#define CR_U_LO 0x40200000ULL
#define CR_U_HI 0x40400000ULL
#define CR_M_LO 0x40C00000ULL
#define CR_M_HI 0x40E00000ULL

static int cr_in_user_window(u64 va)
{
    return (va >= CR_U_LO && va < CR_U_HI) ||
           (va >= CR_M_LO && va < CR_M_HI);
}

struct rt_crash {
    u32 used;
    u32 tick;
    int pid;
    char name[12];
    u64 esr, elr, far;               /* 信号路径=0(无硬件三元) */
    u32 code;                        /* 128+sig;野访=139 */
    u64 x[7];                        /* x0-x5+x29 寄存器面
                                      * (x29=用户帧链根,诊断可见) */
    u64 pc[RT_CRASH_UWALK];          /* 用户 x29 链前 4 帧 */
    u32 nframes;
};

static struct rt_crash crash_ring[RT_CRASH_MAX];
static u32 crash_next;

static void crash_core_dump(struct rt_crash *c, u64 *frame);   /* 站64 */

/* 记录者语境=死者本人(页表在役,用户栈仍可读;sys_exit 在
 * 本函数之后才回收)。frame=272B 帧(FR 布局:x0@16,x_i@
 * 24+8i,x29=24+8*29)。 */
static void crash_user_walk(struct rt_proc *p, u64 *frame,
                            struct rt_crash *c)
{
    u64 fp = frame[3 + 29];          /* x29 槽:x_i=frame[3+i](i>=1,
                                     * 字节 24+8*29=帧基+256) */
    u32 n = 0;

    while (n < RT_CRASH_UWALK && cr_in_user_window(fp) &&
           rt_as_user_mapped(p, fp) && rt_as_user_mapped(p, fp + 8)) {
        u64 ret = *(u64 *)(void *)(fp + 8);
        u64 nxt = *(u64 *)(void *)fp;

        if (!cr_in_user_window(ret))
            break;                   /* 出用户窗:libc/向量帧止步 */
        c->pc[n++] = ret;
        if (nxt <= fp || nxt - fp > 0x10000)
            break;                   /* 栈底/环/坏栈保险丝 */
        fp = nxt;
    }
    c->nframes = n;
}

void rt_crash_record(struct rt_proc *p, u64 *frame,
                     u64 esr, u64 elr, u64 far, u32 code)
{
    struct rt_crash *c = &crash_ring[crash_next % RT_CRASH_MAX];
    u32 i;

    crash_next++;
    c->used = 1;
    c->tick = (u32)rt_ticks;
    c->pid = p ? p->pid : -1;
    c->esr = esr;
    c->elr = elr;
    c->far = far;
    c->code = code;
    c->x[0] = frame[2];               /* x0@16;x_i(i>=1)=frame[3+i] */
    for (i = 1; i < 6; i++)
        c->x[i] = frame[3 + i];
    c->x[6] = frame[3 + 29];          /* x29:帧链根 */
    for (i = 0; i < 12 && rt_cur->name[i]; i++)
        c->name[i] = rt_cur->name[i];
    c->name[i] = '\0';
    c->nframes = 0;
    crash_user_walk(p, frame, c);
    crash_core_dump(c, frame);         /* 站64:ELF core 落盘 */
}

void rt_crash_show(void)
{
    u32 i, j;

    rt_puts_u64("crash: ", crash_next, " record(s), ring ");
    rt_puts_u64("", (u64)RT_CRASH_MAX, "\n");
    for (i = 0; i < RT_CRASH_MAX; i++) {
        struct rt_crash *c = &crash_ring[i];

        if (!c->used)
            continue;
        rt_puts_u64("crash[", i, "]: pid ");
        rt_puts_u64("", (u64)(u32)c->pid, " ");
        rt_puts(c->name);
        rt_puts_u64(" code ", c->code, " tick ");
        rt_puts_u64("", c->tick, "\n");
        if (c->far || c->esr) {
            rt_puts_hex("  esr 0x", c->esr, "");
            rt_puts_hex(" elr 0x", c->elr, "");
            rt_puts_hex(" far 0x", c->far, "\n");
        } else {
            rt_puts("  (signal path, no hw triple)\n");
        }
        for (j = 0; j < c->nframes; j++) {
            rt_puts_hex("  #", j, " pc=0x");
            rt_puts_hex("", c->pc[j], "\n");
        }
        rt_puts_u64("  x0 0x", c->x[0], "");
        rt_puts_u64(" x1 0x", c->x[1], "");
        rt_puts_u64(" x2 0x", c->x[2], "");
        rt_puts_u64(" x29 0x", c->x[6], "\n");
    }
}

u32 rt_crash_count(void)                 /* 站48:health 面板出数 */
{
    return crash_next;                   /* 累计记录数(环 2 槽覆写) */
}

/* ---- 站64:ELF core 序列化+落盘(binfmt_elf 形) ----
 * aarch64 ELF64 core 最小形:Ehdr(ET_CORE)+Phdr(PT_NOTE)+双 note:
 * NT_PRSTATUS(标准 desc=elf_prstatus 最小形,gdb/readelf 语义可读)+
 * NT_ESHINE(自定扩展:code/tick/esr/elr/far+用户帧链)。离线消费双面:
 * EL0 coreparse 工具(tools/ramfs-coreparse.c)+门禁宿主重组解析
 * (双解析器对照=binfmt_elf 形的独立验证)。 */

#define CE_NT_PRSTATUS  1u
#define CE_NT_ESHINE    0x50000064u      /* 自定扩展 note(type 高区,记档) */

/* aarch64 user_pt_regs(Linux gregset:31 regs+sp+pc=272B) */
struct ce_gregset {
    u64 regs[31];
    u64 sp;
    u64 pc;
};

/* elf_prstatus 最小形(对齐 Linux aarch64 用户 core 的 NT_PRSTATUS
 * desc 布局:siginfo+cursig+pid 四元+time 四元+gregset+fpvalid;
 * time 四元=0 面,ppid/pgrp/sid=0 面=v0 记档) */
struct ce_prstatus {
    u32 si_signo, si_code, si_errno;
    u16 cursig;
    u16 pad0;
    u32 pid, ppid, pgrp, sid;
    u64 utime[2], stime[2], cutime[2], cstime[2];
    struct ce_gregset g;
    u32 fpvalid;
    u32 pad1;
};

/* NT_ESHINE 扩展面:站内取证三元+帧链(crash 环同源) */
struct ce_ext {
    u32 code;                            /* 139 / 128+sig */
    u32 tick;
    u64 esr, elr, far;
    u32 nframes;
    u32 pad2;
    u64 frame_pc[RT_CRASH_UWALK];
};

#define CE_EHDR_SZ   64u                 /* ELF64 Ehdr */
#define CE_PHDR_SZ   56u                 /* ELF64 Phdr */
#define CE_HDR_SZ    (CE_EHDR_SZ + CE_PHDR_SZ)
#define CE_NOTE0_HDR (CE_HDR_SZ + 12u + 8u)   /* note 头 12B+name 对齐 8B */
#define CE_NOTE1_OFF (CE_NOTE0_HDR + sizeof(struct ce_prstatus))
#define CE_TOTAL     (CE_NOTE1_OFF + 12u + 8u + sizeof(struct ce_ext))

/* 序列化死者语境为 ELF core(返回 static buf 指针,总长=*len)。
 * frame=272B 帧全寄存器面(x0=frame[2],x_i=frame[3+i],elr=frame[0]);
 * sp=rt_cur->usp(SP_EL0 硬件域,vec.S 存还)。 */
static const u8 *crash_core_serialize(const struct rt_crash *c, u64 *frame,
                                      u64 sp, u32 *len)
{
    static u8 buf[CE_TOTAL];
    struct ce_prstatus *pr;
    struct ce_ext *ex;
    u32 i;

    fd_zero(buf, sizeof(buf));

    /* Ehdr:ELF64/LSB/current,ET_CORE(4),EM_AARCH64(183) */
    buf[0] = 0x7f; buf[1] = 'E'; buf[2] = 'L'; buf[3] = 'F';
    buf[4] = 2;                           /* ELFCLASS64 */
    buf[5] = 1;                           /* ELFDATA2LSB */
    buf[6] = 1;                           /* EV_CURRENT */
    buf[7] = 0;                           /* ELFOSABI_NONE */
    *(u16 *)(void *)(buf + 16) = 4;       /* e_type=ET_CORE */
    *(u16 *)(void *)(buf + 18) = 183;     /* e_machine=EM_AARCH64 */
    *(u32 *)(void *)(buf + 20) = 1;       /* e_version */
    *(u64 *)(void *)(buf + 24) = CE_EHDR_SZ;   /* e_phoff */
    *(u32 *)(void *)(buf + 52) = CE_EHDR_SZ;   /* e_ehsize */
    *(u16 *)(void *)(buf + 54) = CE_PHDR_SZ;   /* e_phentsize */
    *(u16 *)(void *)(buf + 56) = 1;            /* e_phnum */

    /* Phdr[0]:PT_NOTE(4),数据区=双 note */
    *(u32 *)(void *)(buf + CE_EHDR_SZ) = 4;         /* p_type */
    *(u64 *)(void *)(buf + CE_EHDR_SZ + 8) = CE_HDR_SZ;   /* p_offset */
    *(u64 *)(void *)(buf + CE_EHDR_SZ + 32) =
        (u64)(CE_TOTAL - CE_HDR_SZ);                /* p_filesz */
    *(u64 *)(void *)(buf + CE_EHDR_SZ + 48) = 4;    /* p_align */

    /* note0:NT_PRSTATUS(name="CORE\0",desc=elf_prstatus) */
    *(u32 *)(void *)(buf + CE_HDR_SZ) = 5;          /* namesz */
    *(u32 *)(void *)(buf + CE_HDR_SZ + 4) =
        (u32)sizeof(struct ce_prstatus);             /* descsz */
    *(u32 *)(void *)(buf + CE_HDR_SZ + 8) = CE_NT_PRSTATUS;
    buf[CE_HDR_SZ + 12] = 'C'; buf[CE_HDR_SZ + 13] = 'O';
    buf[CE_HDR_SZ + 14] = 'R'; buf[CE_HDR_SZ + 15] = 'E';
    pr = (struct ce_prstatus *)(void *)(buf + CE_NOTE0_HDR);
    pr->si_signo = c->code & 127u;       /* 128+sig 面->si_signo(sig) */
    pr->cursig = (u16)(c->code & 127u);
    pr->pid = (u32)(u32)c->pid;
    /* gregset:死者帧全寄存器(x0-x30)+sp+pc */
    for (i = 0; i < 31; i++)
        pr->g.regs[i] = i ? frame[3 + i] : frame[2];
    pr->g.sp = sp;
    pr->g.pc = c->elr;

    /* note1:NT_ESHINE(扩展三元+帧链) */
    *(u32 *)(void *)(buf + CE_NOTE1_OFF) = 5;       /* namesz */
    *(u32 *)(void *)(buf + CE_NOTE1_OFF + 4) =
        (u32)sizeof(struct ce_ext);                  /* descsz */
    *(u32 *)(void *)(buf + CE_NOTE1_OFF + 8) = CE_NT_ESHINE;
    buf[CE_NOTE1_OFF + 12] = 'C'; buf[CE_NOTE1_OFF + 13] = 'O';
    buf[CE_NOTE1_OFF + 14] = 'R'; buf[CE_NOTE1_OFF + 15] = 'E';
    ex = (struct ce_ext *)(void *)(buf + CE_NOTE1_OFF + 12 + 8);
    ex->code = c->code;
    ex->tick = c->tick;
    ex->esr = c->esr;
    ex->elr = c->elr;
    ex->far = c->far;
    ex->nframes = c->nframes;
    for (i = 0; i < RT_CRASH_UWALK; i++)
        ex->frame_pc[i] = c->pc[i];

    if (len)
        *len = CE_TOTAL;
    return buf;
}

/* 落盘:/var/core/core.<pid>(参考实现白描形)。写者=crash 语境
 * (死者本人 EL1,单核独占);ramfs COW 页承载(站 39 前提,
 * 站 46 "不可为"的翻案注记)。 */
static void crash_core_dump(struct rt_crash *c, u64 *frame)
{
    char path[22];
    u32 n = 0;
    u32 pid = (u32)c->pid;
    char digits[12];
    u32 d = 0;
    const u8 *core;
    u32 len = 0;

    do {
        digits[d++] = (char)('0' + (pid % 10u));
        pid /= 10u;
    } while (pid);
    path[n++] = '/'; path[n++] = 'v'; path[n++] = 'a'; path[n++] = 'r';
    path[n++] = '/';
    path[n++] = 'c'; path[n++] = 'o'; path[n++] = 'r'; path[n++] = 'e';
    path[n++] = '/';
    path[n++] = 'c'; path[n++] = 'o'; path[n++] = 'r'; path[n++] = 'e';
    path[n++] = '.';
    while (d)
        path[n++] = digits[--d];
    path[n] = '\0';

    core = crash_core_serialize(c, frame, rt_cur->usp, &len);
    if (rt_ramfs_core_store(path, core, len) == 0) {
        rt_puts("crash: core dumped ");
        rt_puts(path);
        rt_puts("\n");
    }
}
