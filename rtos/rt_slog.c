/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "rtos.h"
#include "eshine_log.h"

/* 站四十:slogd——第七个 resmgr 服务器(服务收编段)。参考实现机制
 * 白描:系统日志服务=环形缓冲+级别策略+消费面。站内形状:fd 面
 * ——WRITE=入环(data[0]=级别,data[1..]=文本),READ=环倾倒
 * (ocb.off=环位,一次一行);策略:warn 及以上控制台回显
 * (tty 面 v0 走 rt_puts=内核语境串口)。
 *
 * 站五十:持久化+klog 双源+轮转+logctrl(参考实现 slog/log_file.c
 * 机制白描的站内形)。slogsrv 以 resmgr 客户端身份走消息面写
 * /var/log(服务器互为客户端=微内核正道,零内部 API 泄漏):
 * 双世代交替命名轮转(slog/slog.1,rotate=unlink 对侧+create
 * 对侧=零拷贝);klog=内核 elog 环(站 49)增量收割入卷,'S '前缀
 * =用户 slog 面(双源同卷);recv 1s 超时醒=周期 flush+用户写
 * piggyback flush(参考实现水位 flush 的站内形)。 */

#define SLOG_RING 16u
#define SLOG_LINE 44u
#define SLOG_WARN 2u
#define SLOG_TICKS 100u             /* 站50:客户端趟输窗(SRV_TICKS 同值) */

struct slog_slot {
    u32 len;                     /* 文本长(不含 NUL) */
    char level;                  /* 0=info 1=notice 2=warn 3=err */
    char text[SLOG_LINE];
};
static struct slog_slot slog_ring[SLOG_RING];
static u32 slog_head;            /* 下一写入位(环) */
static u32 slog_count;

#define SLOG_OCB_MAX 4u
struct slog_ocb {
    u32 used;
    u32 off;                     /* READ=环位(自 0 顺游) */
};
static struct slog_ocb slog_ocbs[SLOG_OCB_MAX];

static const char slog_ch[] = { '?', 'N', 'W', 'E' };  /* 级别字 */

/* ---- 站五十:持久化机器(消息面客户端,ramfs 后备) ---- */
#define SLOG_ROTATE_AT 3584u      /* 4KB 页上限前触发放余量 */
#define SLOG_GEN_NAMES 2u

static const char *slog_gen_path[SLOG_GEN_NAMES] = {
    "/var/log/slog", "/var/log/slog.1"
};
static int  slog_coid;            /* ramfs 元名连接(boot 一次) */
static u32  slog_ocb_cur;         /* 当前世代文件 ocb */
static u32  slog_fsz;             /* 当前世代尺寸(客户端影子) */
static u32  slog_gen;             /* 当前世代下标(rotate 交替) */
static u32  slog_kcursor;         /* klog 环收割游标(绝对索引) */
static u32  slog_echo = 1;        /* warn+ 控制台回显开关(logctrl) */

/* 请求清零(fd_zero 是 rt_fsrv.c 的 static,本地同律零填) */
static void slog_req_zero(struct rt_fdreq *req)
{
    u32 i;

    for (i = 0; i < sizeof(*req) / 4u; i++)
        ((u32 *)req)[i] = 0;
}

/* 单 op 往返(OPEN/CREATE/WRITE/SEEK/STAT/CLOSE/UNLINK 共一连接;
 * STAT 走 req.data=path 路径型,无需 ocb;SEEK 的 rsv=whence 槽) */
static long slog_req(u32 op, u32 a, u32 ocb, u32 rsv, const char *path,
                     const char *wdata, u32 wlen, char *rdata, u32 rlen)
{
    struct rt_fdreq req;
    long r;

    slog_req_zero(&req);
    req.op = op;
    req.a = a;
    req.ocb = ocb;
    req.rsv = rsv;
    if (path) {
        u32 i;

        for (i = 0; i + 1 < RT_FD_DATA && path[i]; i++)
            req.data[i] = path[i];
        req.data[i] = '\0';
    } else if (wdata) {
        u32 i;

        for (i = 0; i < wlen && i < RT_FD_DATA; i++)
            req.data[i] = wdata[i];
        req.a = wlen;
    }
    r = rt_msg_send(slog_coid, &req, sizeof(req), rdata, rlen, SLOG_TICKS);
    return r;
}

/* 文件追加(44B 窗分块;越限先轮转;fsz 影子同进) */
static void slog_file_append(const char *buf, u32 len)
{
    while (len) {
        u32 n = len > RT_FD_DATA ? RT_FD_DATA : len;
        long r;

        if (slog_fsz + n > SLOG_ROTATE_AT) {
            /* 轮转:关当前+unlink 对侧+create 对侧=新当前(对侧的
             * 对侧=当前自身留在盘上作前世,双世代零拷贝) */
            slog_req(RT_FDOP_CLOSE, 0, slog_ocb_cur, 0, NULL, NULL, 0,
                     NULL, 0);
            slog_req(RT_FDOP_UNLINK, 0, 0, 0, slog_gen_path[slog_gen ^ 1],
                     NULL, 0, NULL, 0);
            r = slog_req(RT_FDOP_CREATE, 0, 0, 0, slog_gen_path[slog_gen ^ 1],
                         NULL, 0, NULL, 0);
            if (r < 0)
                return;               /* 无后备可写:丢批(v0 诚实脸) */
            slog_ocb_cur = (u32)r;
            slog_gen ^= 1u;
            slog_fsz = 0;
        }
        r = slog_req(RT_FDOP_WRITE, n, slog_ocb_cur, 0, NULL, buf, n,
                     NULL, 0);
        if (r < 0)
            return;
        buf += n;
        len -= n;
        slog_fsz += n;
    }
}

/* 世代文件尺寸(logctrl stat 面;STAT=路径型一步:mode<<32|size) */
static u32 slog_gen_size(u32 gen)
{
    long st = slog_req(RT_FDOP_STAT, 0, 0, 0, slog_gen_path[gen],
                       NULL, 0, NULL, 0);

    return st > 0 ? (u32)st : 0;      /* 低 32 位=尺寸 */
}

/* 惰性开卷:ramfs 元名连接+当前世代文件开/建(既有内容续写)+
 * 尺寸影子;首调用者=首个客户端写或周期收割(boot 期往返撤除:
 * 与 pm stdio 绑定窗同拍打散 FIFO 序=间歇 bind 失败,实测定谳) */
static void slog_store_ready(void)
{
    long o;

    if (slog_coid)
        return;                      /* 已起 */
    slog_coid = rt_msg_name_open("/dev/ramfs");
    if (slog_coid <= 0) {
        slog_coid = 0;
        return;
    }
    o = slog_req(RT_FDOP_OPEN, 0, 0, 0, slog_gen_path[slog_gen],
                 NULL, 0, NULL, 0);
    if (o >= 0) {
        slog_ocb_cur = (u32)o;
        slog_req(RT_FDOP_SEEK, 0, slog_ocb_cur, 2u, NULL, NULL, 0,
                 NULL, 0);            /* rsv=2:SEEK END(续写) */
    } else {
        o = slog_req(RT_FDOP_CREATE, 0, 0, 0, slog_gen_path[slog_gen],
                     NULL, 0, NULL, 0);
        if (o >= 0)
            slog_ocb_cur = (u32)o;
    }
    if (o >= 0)
        slog_fsz = slog_gen_size(slog_gen);
}

/* klog 增量收割入卷(锁窗内读,与存环同锁;短读即止=排干整环——
 * 128B/次的单发漏斗太细,环积压绕圈丢史,排干律修之) */
static void slog_klog_flush(void)
{
    static char kbuf[128];
    u32 n;

    slog_store_ready();
    if (!slog_coid)
        return;
    do {
        log_ring_lock();
        n = eshine_log_ring_delta(kbuf, sizeof(kbuf), &slog_kcursor);
        log_ring_unlock();
        if (n)
            slog_file_append(kbuf, n);
    } while (n == sizeof(kbuf));
}



static void srv_slog(void *arg)
{
    struct rt_fdreq req;
    struct rt_recvinfo ri;
    char out[RT_FD_RDATA];
    int chid;
    long r;

    (void)arg;
    chid = rt_msg_ch_create(0);
    if (chid <= 0)
        return;
    if (rt_msg_name_attach("/dev/slog", chid) != 0) {
        rt_msg_ch_destroy(chid);
        return;
    }
    /* 站50:持久化客户端起——惰性化(boot 期往返撤除:boot 窗内的
     * slogsrv ramfs 往返与 pm stdio 绑定窗同拍=pm 间歇 bind 失败
     * 实测定谳;首个客户端写/周期收割时 slog_store_ready 按需开卷) */
    for (;;) {
        /* 站50:1s 超时醒=周期 flush(参考实现 10s 周期的站内形;
         * 超时返非 0 无 rcvid,continue 前先收割) */
        if (rt_msg_recv(chid, &req, sizeof(req), 100u, &ri) != 0) {
            if (slog_coid > 0)
                slog_klog_flush();
            continue;
        }
        if (ri.rcvid == 0)
            continue;
        r = -22;
        switch (req.op) {
        case RT_FDOP_OPEN: {
            u32 i;

            for (i = 0; i < SLOG_OCB_MAX; i++)
                if (!slog_ocbs[i].used) {
                    slog_ocbs[i].used = 1;
                    slog_ocbs[i].off = 0;
                    r = (long)i + 1;
                    break;
                }
            if (r == -22)
                r = -12;
            break;
        }
        case RT_FDOP_WRITE: {
            /* 入环:data[0]=级别,data[1..]=文本(req.a=窗长含级别);
             * 站50:双卷——环(既有)+文件('S '前缀行);写=piggyback
             * flush(参考实现水位 flush 的站内形) */
            struct slog_slot *s;
            u32 n = req.a, i;

            if (n == 0 || n > RT_FD_DATA)
                break;
            s = &slog_ring[slog_head];
            s->level = (char)(req.data[0] & 3);
            s->len = n - 1 > SLOG_LINE - 1 ? SLOG_LINE - 1 : n - 1;
            for (i = 0; i < s->len; i++)
                s->text[i] = req.data[1 + i];
            s->text[s->len] = '\0';
            if (slog_count < SLOG_RING)
                slog_count++;
            {   /* 文件卷:'S [X] text\n'(用户源前缀 S=system) */
                char fline[SLOG_LINE + 12];
                u32 j = 0;

                fline[j++] = 'S'; fline[j++] = ' ';
                fline[j++] = '['; fline[j++] = slog_ch[(u32)s->level & 3];
                fline[j++] = ']'; fline[j++] = ' ';
                for (i = 0; i < s->len; i++)
                    fline[j++] = s->text[i];
                fline[j++] = '\n';
                slog_store_ready();  /* 惰性开卷(首个客户端写) */
                if (slog_coid)
                    slog_file_append(fline, j);
            }
            slog_klog_flush();               /* piggyback:klog 收割 */
            if (slog_echo && (u32)s->level >= SLOG_WARN) {
                /* 策略:warn+ 回显(单行;logctrl echo 开关) */
                char echo[SLOG_LINE + 12];
                u32 j = 0;

                echo[j++] = 's'; echo[j++] = 'l'; echo[j++] = 'o';
                echo[j++] = 'g'; echo[j++] = ':'; echo[j++] = ' ';
                echo[j++] = '['; echo[j++] = slog_ch[(u32)s->level & 3];
                echo[j++] = ']'; echo[j++] = ' ';
                for (i = 0; i < s->len; i++)
                    echo[j++] = s->text[i];
                echo[j++] = '\n';
                rt_puts_n(echo, j);
            }
            slog_head = (slog_head + 1) % SLOG_RING;
            r = (long)n;
            break;
        }
        case RT_FDOP_READ: {
            /* 倾倒:ocb.off=环序(旧到新),一行 "[X] text" */
            struct slog_ocb *o = req.ocb && req.ocb <= SLOG_OCB_MAX &&
                slog_ocbs[req.ocb - 1].used ? &slog_ocbs[req.ocb - 1] : NULL;
            struct slog_slot *s;
            u32 i, n = 0;

            if (!o)
                break;
            if (o->off >= slog_count)
                break;                    /* status 0=EOF */
            s = &slog_ring[(slog_head + SLOG_RING - slog_count +
                            o->off) % SLOG_RING];
            out[n++] = '[';
            out[n++] = slog_ch[(u32)s->level & 3];
            out[n++] = ']';
            out[n++] = ' ';
            for (i = 0; i < s->len && n < RT_FD_RDATA - 1; i++)
                out[n++] = s->text[i];
            out[n++] = '\n';
            o->off++;
            rt_msg_reply(chid, ri.rcvid, out, n, (long)n);
            continue;
        }
        case RT_FDOP_STAT:
            r = ((long)0x81A4u) << 32;    /* S_IFCHR|0644,size 0 */
            break;
        case RT_FDOP_CLOSE: {
            struct slog_ocb *o = req.ocb && req.ocb <= SLOG_OCB_MAX &&
                slog_ocbs[req.ocb - 1].used ? &slog_ocbs[req.ocb - 1] : NULL;

            if (o)
                o->used = 0;
            r = 0;
            break;
        }
        case RT_FDOP_DUP:
            r = 0;
            break;
        default:
            r = -22;
        }
        rt_msg_reply(chid, ri.rcvid, NULL, 0, r);
    }
}

/* ---- 站五十:logctrl 命令体(sys_demo 蹦床;参考实现 log_ctrl 白描) ----
 * flush=立即收割;stat=双世代尺寸+收割游标+回显开关;echo on|off=
 * warn+ 控制台回显开关。 */
void k_logctrl(const char *uargs)
{
    char w[12];
    u32 n = 0, i;

    while (uargs[n] == ' ')
        uargs++;
    while (uargs[n] && uargs[n] != ' ' && n < sizeof(w) - 1)
        n++;
    for (i = 0; i < n; i++)
        w[i] = uargs[i];
    w[n] = '\0';
    if (w[0] == 'f' && w[1] == 'l' && w[2] == 'u' && w[3] == 's'
        && w[4] == 'h' && n == 5) {
        /* flush:立即收割(收割游标推进+当前世代尺寸) */
        u32 before;

        before = slog_kcursor;
        slog_klog_flush();
        rt_puts_u64("logctrl: flushed, kcursor ", before, " -> ");
        rt_puts_u64("", slog_kcursor, ", fsz ");
        rt_puts_u64("", slog_fsz, "\n");
        return;
    }
    if (w[0] == 'e' && w[1] == 'c' && w[2] == 'h' && w[3] == 'o'
        && n == 4) {
        const char *p = uargs + n;

        while (*p == ' ')
            p++;
        if (p[0] == 'o' && p[1] == 'n' && (p[2] == ' ' || p[2] == '\0')) {
            slog_echo = 1;
            rt_puts("logctrl: echo on\n");
            return;
        }
        if (p[0] == 'o' && p[1] == 'f' && p[2] == 'f'
            && (p[3] == ' ' || p[3] == '\0')) {
            slog_echo = 0;
            rt_puts("logctrl: echo off\n");
            return;
        }
        rt_puts("usage: logctrl echo on|off\n");
        return;
    }
    if (n == 0 || (w[0] == 's' && w[1] == 't' && w[2] == 'a'
                   && w[3] == 't' && n == 4)) {
        /* stat(默认面):双世代+游标+回显 */
        rt_puts("slog files:\n");
        rt_puts_u64("  ", slog_gen_size(0), " B  /var/log/slog\n");
        rt_puts_u64("  ", slog_gen_size(1), " B  /var/log/slog.1\n");
        rt_puts_u64("  kcursor ", slog_kcursor, ", gen ");
        rt_puts_u64("", slog_gen, ", echo ");
        rt_puts(slog_echo ? "on\n" : "off\n");
        return;
    }
    rt_puts("usage: logctrl [flush|stat|echo on|off]\n");
}

void rt_slog_boot(void)
{
    if (!rt_task_create("slogsrv", srv_slog, NULL, 8))
        rt_puts("srv: slogsrv spawn failed\n");
}
