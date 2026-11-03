/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
/* 站六十四:ELF core 离线解析件(binfmt_elf 形的 EL0 消费面)。
 * argv[1]=core 文件路径;解析 Ehdr(ET_CORE/EM_AARCH64)+Phdr
 * (PT_NOTE)+双 note:NT_PRSTATUS(pid/cursig/gregset)与
 * NT_ESHINE(esr/elr/far/tick/帧链)。
 * 与门禁宿主重组解析互为独立解析器(双解析器对照=binfmt 形的
 * 独立验证)。 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#define NT_PRSTATUS 1u
#define NT_ESHINE   0x50000064u

/* aarch64 gregset(Linux user_pt_regs:31 regs+sp+pc=272B) */
struct gregset {
    unsigned long long regs[31];
    unsigned long long sp, pc;
};

/* elf_prstatus 最小形(与内核 rt_crash.c 的 ce_prstatus 布局对齐) */
struct prstatus {
    unsigned int si_signo, si_code, si_errno;
    unsigned short cursig;
    unsigned short pad0;
    unsigned int pid, ppid, pgrp, sid;
    unsigned long long utime[2], stime[2], cutime[2], cstime[2];
    struct gregset g;
    unsigned int fpvalid, pad1;
};

/* NT_ESHINE 扩展面 */
struct eshine_ext {
    unsigned int code, tick;
    unsigned long long esr, elr, far;
    unsigned int nframes, pad2;
    unsigned long long frame_pc[4];
};

static unsigned int rd16(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

static unsigned int rd32(const unsigned char *p)
{
    return rd16(p) | (rd16(p + 2) << 16);
}

static unsigned long long rd64(const unsigned char *p)
{
    return (unsigned long long)rd32(p) |
           ((unsigned long long)rd32(p + 4) << 32);
}

int main(int argc, char **argv)
{
    unsigned char buf[4096];
    unsigned long long total = 0;
    unsigned int off, i;
    int fd;

    if (argc < 2) {
        printf("usage: %s <core-path>\n", argv[0]);
        return 2;
    }
    fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        printf("coredump: open %s failed\n", argv[1]);
        return 1;
    }
    for (;;) {
        long n = read(fd, buf + total, sizeof(buf) - total);

        if (n < 0) {
            printf("coredump: read failed\n");
            close(fd);
            return 1;
        }
        if (n == 0)
            break;                         /* EOF */
        total += (unsigned long long)n;
        if (total >= sizeof(buf))
            break;                         /* 一页 core 上限 */
    }
    close(fd);

    /* Ehdr 校验:ELF64/LSB/ET_CORE(4)/EM_AARCH64(183)/单 PT_NOTE */
    if (total < 64 + 56) {
        printf("coredump: too small (%llu bytes)\n", total);
        return 1;
    }
    if (buf[0] != 0x7f || buf[1] != 'E' || buf[2] != 'L' || buf[3] != 'F' ||
        buf[4] != 2 || buf[5] != 1) {
        printf("coredump: bad elf magic/class/data\n");
        return 1;
    }
    if (rd16(buf + 16) != 4 || rd16(buf + 18) != 183) {
        printf("coredump: not ET_CORE/EM_AARCH64\n");
        return 1;
    }
    if (rd64(buf + 24) != 64 || rd16(buf + 54) != 56 || rd16(buf + 56) != 1) {
        printf("coredump: bad phoff/phentsize/phnum\n");
        return 1;
    }
    /* Phdr[0]:PT_NOTE(4),p_offset=120=单 PT_NOTE 固定形 */
    if (rd32(buf + 64) != 4 || rd64(buf + 64 + 8) != 64 + 56) {
        printf("coredump: phdr[0] not PT_NOTE@120\n");
        return 1;
    }

    /* note 走查(namesz/descsz/type+name+desc;name 4 对齐) */
    {
        unsigned int pos = 64 + 56;
        unsigned int end = (unsigned int)total;
        int seen_pr = 0, seen_ext = 0;
        struct prstatus pr;
        struct eshine_ext ex;

        while (pos + 12 <= end) {
            unsigned int namesz = rd32(buf + pos);
            unsigned int descsz = rd32(buf + pos + 4);
            unsigned int ntype = rd32(buf + pos + 8);
            unsigned int dpos = pos + 12 + ((namesz + 3u) & ~3u);

            if (namesz == 5 && ntype == NT_PRSTATUS && descsz == sizeof(pr) &&
                !seen_pr && dpos + sizeof(pr) <= end) {
                memcpy(&pr, buf + dpos, sizeof(pr));
                seen_pr = 1;
            } else if (ntype == NT_ESHINE && descsz == sizeof(ex) &&
                       !seen_ext && dpos + sizeof(ex) <= end) {
                memcpy(&ex, buf + dpos, sizeof(ex));
                seen_ext = 1;
            }
            pos = dpos + ((descsz + 3u) & ~3u);
        }
        if (!seen_pr) {
            printf("coredump: no NT_PRSTATUS note\n");
            return 1;
        }
        printf("coredump: pid=%d cursig=%d pc=0x%llx sp=0x%llx\n",
               (int)pr.pid, (int)pr.cursig, pr.g.pc, pr.g.sp);
        if (!seen_ext) {
            printf("coredump: no NT_ESHINE note (ok, optional)\n");
            return 0;
        }
        printf("coredump: esr=0x%llx elr=0x%llx far=0x%llx tick=%u\n",
               ex.esr, ex.elr, ex.far, ex.tick);
        printf("coredump: frames=%u", ex.nframes > 4 ? 4 : ex.nframes);
        for (i = 0; i < ex.nframes && i < 4; i++)
            printf(" 0x%llx", ex.frame_pc[i]);
        printf("\n");
    }
    return 0;
}
