/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "rtos.h"

/* 站三十:三段消息 IPC(Send-Recv-Reply)——参考实现 同步队列件 机器
 * 的站内尺度收编(机制参考零拷贝):通道(到达面=站 17 信号量的
 * 阻塞/超时机器)+coid 连接实体+引用计数(六差距①)+优先级序到达
 * 队列+优先级传播(六差距②,发送者阻塞→提权阻塞中的接收者,
 * QNX adjust_priority 镜像;还原锚=base_prio,与站 18B PI 同律)+
 * abort 超时面(未收走=直接撤+ETIMEDOUT;已收走=递 UNBLOCK 脉冲
 * 予服务器,QNX unblock 镜像;CHF_UNBLOCK 门控=六差距③ flags 体系
 * 之首面)+命名登记(六差距④:层级路径分段比较;真层级树=站 31)
 * +脉冲(8B code+value 异步不阻塞,rcvid=0;系统码负/用户码≥0)。
 *
 * v0 中继与边界(诚实入档):消息 ≤64B 走内核槽中继——每拷贝都
 * 发生在对应进程的 SVC 语境,零跨址空间 VA 访问;参考实现长消息映射
 * /fast 寄存器路/VM_SET 单拷窗(六差距⑥)=跨址空间/跨 VM 面,
 * 站 31(用户态 server)/跨 VM 站后议;iov 同理。事件绑定族
 * (六差距⑤)的 v0 面=msg_pulse(coid) 即 SIGEV_PULSE 形,
 * ionotify/扩展位后议。 */

static struct rt_chan chans[RT_CH_MAX];
static struct rt_conn conns[RT_CONN_MAX];
static struct rt_msg  msgs[RT_MSGSLOT_MAX];
static struct rt_name names[RT_NAME_MAX];

static void msg_copy(void *dst, const void *src, u32 n)
{
    const char *s = src;
    char *d = dst;

    while (n--)
        *d++ = *s++;
}

static void msg_zero(void *dst, u32 n)
{
    char *d = dst;

    while (n--)
        *d++ = 0;
}

/* ---- 通道面 ---- */

int rt_msg_ch_create(u32 flags)
{
    u32 i;

    for (i = 0; i < RT_CH_MAX; i++) {
        if (!chans[i].used) {
            msg_zero(&chans[i], sizeof(chans[i]));
            chans[i].used = 1;
            chans[i].flags = flags;
            chans[i].owner_pid = rt_cur->proc ? rt_cur->proc->pid : 0;
            rt_sem_init(&chans[i].msgs, 0);
            return (int)i + 1;          /* chid 1..N(0=非法) */
        }
    }
    return -12;                        /* -ENOMEM */
}

int rt_msg_ch_destroy(int chid)
{
    struct rt_chan *ch;

    if (chid <= 0 || (u32)chid > RT_CH_MAX || !chans[chid - 1].used)
        return -22;
    ch = &chans[chid - 1];
    /* 队列余量直接撤:未收走的消息槽放还(发送者侧 take 会因
     * reply_sem 无 give 而 RT_TIMEOUT——服务器销毁后客户端收
     * 尸面=超时,v0 从简不递 ESRCH 唤醒,记档) */
    while (ch->nq) {
        struct rt_msg *m = ch->q[--ch->nq];

        m->used = 0;
    }
    ch->used = 0;
    return 0;
}

/* ---- 连接实体(六差距①) ---- */

int rt_msg_conn_attach(int chid)
{
    u32 i;

    if (chid <= 0 || (u32)chid > RT_CH_MAX || !chans[chid - 1].used)
        return -22;
    for (i = 0; i < RT_CONN_MAX; i++) {
        if (!conns[i].used) {
            conns[i].used = 1;
            conns[i].refcnt = 1;
            conns[i].chid = chid;
            conns[i].owner_pid = rt_cur->proc ? rt_cur->proc->pid : 0;
            return (int)i + 1;          /* coid 1..N */
        }
    }
    for (i = 0; i < RT_CONN_MAX; i++) {
        if (conns[i].used && conns[i].chid == chid) {
            conns[i].refcnt++;         /* 复用连接(同目标聚合) */
            return (int)i + 1;
        }
    }
    return -12;
}

int rt_msg_conn_detach(int coid)
{
    if (coid <= 0 || (u32)coid > RT_CONN_MAX || !conns[coid - 1].used)
        return -22;
    if (--conns[coid - 1].refcnt == 0)
        conns[coid - 1].used = 0;      /* 断连脉冲(CHF_DISCONNECT)=
                                        * 站 31 用户态 server 接管后议 */
    return 0;
}

/* ---- 命名面(六差距④:层级路径分段比较;平表=站内尺度) ---- */

static int name_cmp(const char *a, const char *b)
{
    /* 分段比较:逐 segment 精确匹配,杜绝前缀误中("/ipc/dem" 不中
     * "/ipc/demo");旧参考实现全平表线性 strcmp 的层级化修正在此。 */
    while (*a == *b) {
        if (*a == '\0')
            return 0;
        a++; b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static int name_len_ok(const char *p)
{
    u32 n = 0;

    while (p[n] && n < RT_NAME_PATH)
        n++;
    return p[n] == '\0' && n > 0 && p[0] == '/';
}

int rt_msg_name_attach(const char *path, int chid)
{
    u32 i, j;

    if (!name_len_ok(path) ||
        chid <= 0 || (u32)chid > RT_CH_MAX || !chans[chid - 1].used)
        return -22;
    for (i = 0; i < RT_NAME_MAX; i++)
        if (names[i].used && name_cmp(names[i].path, path) == 0)
            return -17;                /* -EEXIST */
    for (i = 0; i < RT_NAME_MAX; i++) {
        if (!names[i].used) {
            names[i].used = 1;
            for (j = 0; j + 1 < RT_NAME_PATH && path[j]; j++)
                names[i].path[j] = path[j];
            names[i].path[j] = '\0';
            names[i].chid = chid;
            return 0;
        }
    }
    return -12;
}

int rt_msg_name_detach(const char *path)
{
    u32 i;

    for (i = 0; i < RT_NAME_MAX; i++)
        if (names[i].used && name_cmp(names[i].path, path) == 0) {
            names[i].used = 0;
            return 0;
        }
    return -2;                         /* -ENOENT */
}

int rt_msg_name_open(const char *path)
{
    u32 i;

    if (!name_len_ok(path))
        return -22;
    for (i = 0; i < RT_NAME_MAX; i++)
        if (names[i].used && name_cmp(names[i].path, path) == 0)
            return rt_msg_conn_attach(names[i].chid);
    return -2;
}

/* ---- 到达队列:优先级序插(低值高优先,参考实现发送优先级链) ---- */

static void q_insert(struct rt_chan *ch, struct rt_msg *m)
{
    u32 i = ch->nq;

    while (i > 0 && ch->q[i - 1]->sprio > m->sprio) {
        ch->q[i] = ch->q[i - 1];
        i--;
    }
    ch->q[i] = m;
    ch->nq++;
}

static struct rt_msg *q_take(struct rt_chan *ch)
{
    struct rt_msg *m;
    u32 i;

    if (!ch->nq)
        return NULL;
    m = ch->q[0];
    ch->nq--;
    for (i = 0; i < ch->nq; i++)
        ch->q[i] = ch->q[i + 1];
    return m;
}

static void q_remove(struct rt_chan *ch, struct rt_msg *m)
{
    u32 i, j = 0;

    for (i = 0; i < ch->nq; i++)
        if (ch->q[i] != m)
            ch->q[j++] = ch->q[i];
    if (j != ch->nq)
        ch->nq = j;
}

/* ---- 优先级传播(六差距②):唤醒面提权阻塞中的接收者 ---- */

static void boost_receiver(struct rt_chan *ch, u32 sprio)
{
    rt_task_t *r = ch->msgs.wq_head;

    /* 接收者阻塞在到达面:QNX adjust_priority 镜像——客户端阻塞
     * 即服务器应提速服务;还原锚=base_prio(站 18B PI 同律,reply
     * 出口还原;PI 互斥叠加交互=v0 简化记档) */
    if (r && r->prio > sprio)
        r->prio = sprio;
}

/* ---- 脉冲 ---- */

long rt_msg_pulse(int coid, s64 code, u64 value)
{
    struct rt_chan *ch;
    struct rt_msg *m;
    u32 i;

    if (coid <= 0 || (u32)coid > RT_CONN_MAX || !conns[coid - 1].used)
        return -22;
    ch = &chans[conns[coid - 1].chid - 1];
    for (i = 0; i < RT_MSGSLOT_MAX; i++) {
        if (!msgs[i].used) {
            m = &msgs[i];
            msg_zero(m, sizeof(*m));
            m->used = 1;
            m->is_pulse = 1;
            m->sender_pid = rt_cur->proc ? rt_cur->proc->pid : 0;
            m->sprio = rt_cur->prio;
            m->code = code;
            m->value = value;
            q_insert(ch, m);
            boost_receiver(ch, rt_cur->prio);
            rt_sem_give(&ch->msgs);
            return 0;
        }
    }
    return -12;
}

/* ---- 三段之一:SEND(阻塞至 REPLY/超时) ---- */

long rt_msg_send(int coid, const void *sbuf, u32 slen,
                 void *rbuf, u32 rlen, u64 ticks)
{
    struct rt_chan *ch;
    struct rt_msg *m;
    u32 i;
    long r;

    if (coid <= 0 || (u32)coid > RT_CONN_MAX || !conns[coid - 1].used)
        return -22;
    if (slen > RT_MSG_BYTES || rlen > RT_MSG_BYTES)
        return -22;
    ch = &chans[conns[coid - 1].chid - 1];
    for (i = 0; i < RT_MSGSLOT_MAX; i++) {
        if (!msgs[i].used)
            break;
    }
    if (i == RT_MSGSLOT_MAX)
        return -11;                    /* 槽满 -EAGAIN */
    m = &msgs[i];
    msg_zero(m, sizeof(*m));
    m->used = 1;
    m->sender_pid = rt_cur->proc ? rt_cur->proc->pid : 0;
    m->sender = rt_cur;
    m->sprio = rt_cur->prio;
    m->len = slen;
    if (slen)
        msg_copy(m->data, sbuf, slen);
    rt_sem_init(&m->reply_sem, 0);
    q_insert(ch, m);
    boost_receiver(ch, rt_cur->prio);
    rt_sem_give(&ch->msgs);
    /* 等回执:REPLY 给回执;超时=abort 面(参考实现镜像:未收走直接
     * 撤返 ETIMEDOUT;已收走递 UNBLOCK 脉冲予服务器,发送者先走) */
    r = rt_sem_take_to(&m->reply_sem, ticks);
    if (r != 0) {
        if (m->state == 0) {           /* QUEUED:未收走,直接撤 */
            q_remove(ch, m);
            m->used = 0;
            return -110;               /* -ETIMEDOUT */
        }
        m->aborted = 1;                /* RECEIVED:服务器持有 */
        if (ch->flags & RT_CHF_UNBLOCK) {
            struct rt_msg *up;
            u32 j;

            for (j = 0; j < RT_MSGSLOT_MAX; j++) {
                if (!msgs[j].used) {
                    up = &msgs[j];
                    msg_zero(up, sizeof(*up));
                    up->used = 1;
                    up->is_pulse = 1;
                    up->sprio = m->sprio;
                    up->code = RT_PULSE_UNBLOCK;
                    up->value = ((u64)(conns[coid - 1].chid) << 4) |
                                ((u64)(m - msgs) + 1);
                    q_insert(ch, up);
                    rt_sem_give(&ch->msgs);
                    break;
                }
            }
        }
        /* 槽由服务器的 REPLY 收尾(aborted 路径)释放 */
        return -110;
    }
    /* 回执到:拷回复+status 即返回值(参考实现 REPLY 语义),槽自释 */
    if (rlen && m->rlen) {
        u32 n = rlen < m->rlen ? rlen : m->rlen;

        msg_copy(rbuf, m->rdata, n);
    }
    r = m->status;
    m->used = 0;
    return r;
}

/* ---- 三段之二:RECV(阻塞/超时;脉冲 rcvid=0) ---- */

long rt_msg_recv(int chid, void *buf, u32 buflen, u64 ticks,
                struct rt_recvinfo *info)
{
    struct rt_chan *ch;
    struct rt_msg *m;
    long r;
    u32 n;

    if (chid <= 0 || (u32)chid > RT_CH_MAX || !chans[chid - 1].used)
        return -22;
    ch = &chans[chid - 1];
    r = rt_sem_take_to(&ch->msgs, ticks);
    if (r != 0)
        return -110;
    m = q_take(ch);
    if (!m)
        return -11;                    /* 计数与队列失配(不变量破) */
    m->state = 1;                      /* RECEIVED:服务器持有 */
    if (info) {
        info->rcvid = m->is_pulse ? 0 :
            (((u64)chid << 4) | ((u64)(m - msgs) + 1));
        info->len = m->is_pulse ? 0 : m->len;
        info->code = m->code;
        info->value = m->value;
    }
    if (!m->is_pulse && m->len && buflen) {
        n = m->len < buflen ? m->len : buflen;
        msg_copy(buf, m->data, n);
    }
    if (m->is_pulse)
        m->used = 0;                   /* 脉冲即取即释(无回执) */
    return 0;
}

/* ---- 三段之三:REPLY(status 即发送者返回值)+提权还原 ---- */

long rt_msg_reply(int chid, u64 rcvid, const void *rbuf, u32 rlen,
                  long status)
{
    struct rt_chan *ch;
    struct rt_msg *m;
    u32 slot;

    if (chid <= 0 || (u32)chid > RT_CH_MAX || !chans[chid - 1].used)
        return -22;
    /* rcvid 复合=(chid<<4)|(槽+1):低位非零才是合法消息句柄——
     * 曾把"低位非零"写成拒绝条件,合法 rcvid 全数被拒 -22,回执
     * 永不达,发送者恒满窗超时(reply -110 打无符号大数定谳)。 */
    if ((rcvid >> 4) != (u64)chid || !(rcvid & 0xFULL))
        return -22;
    slot = (u32)(rcvid & 0xFULL) - 1;
    if (slot >= RT_MSGSLOT_MAX)
        return -22;
    m = &msgs[slot];
    if (!m->used || m->state != 1)
        return -3;                     /* -ESRCH:早已不在/非持有态 */
    if (rlen > RT_MSG_BYTES)
        return -22;
    m->rlen = rlen;
    if (rlen)
        msg_copy(m->rdata, rbuf, rlen);
    m->status = status;
    if (m->aborted) {
        m->used = 0;                   /* 客户端已超时先走:静默收尾 */
    } else {
        rt_sem_give(&m->reply_sem);    /* 发送者醒:status 即其返回值 */
    }
    /* 提权还原(六差距②的出口):anchor=base_prio,只还原被提过
     * 的(prio<base 才提过;与 PI 互斥的叠加交互 v0 简化记档) */
    if (rt_cur->prio < rt_cur->base_prio)
        rt_cur->prio = rt_cur->base_prio;
    return 0;
}

/* ---- demo 体(sys_demo 蹦床 spawn;服务器=内核任务直调本层) ----
 * 三案:ipc=三段全链+优先级传播实证;pulse=异步脉冲 5 连发;
 * abort=发送者超时先走+UNBLOCK 脉冲(QNX unblock 镜像)。 */

static long srv_recv1(int chid, char *buf,
                      struct rt_recvinfo *ri, u64 ticks)
{
    return rt_msg_recv(chid, buf, RT_MSG_BYTES, ticks, ri);
}

/* ipcdemo 服务器:建道+挂名+收一答一(v+1)+还原收场 */
void rt_msg_demo_server(void *arg)
{
    int chid = rt_msg_ch_create(0);
    char buf[RT_MSG_BYTES];
    struct rt_recvinfo ri;
    long v = 0, rv;

    (void)arg;
    if (chid > 0 && rt_msg_name_attach("/ipc/demo", chid) == 0) {
        if (srv_recv1(chid, buf, &ri, 200) == 0 && ri.rcvid) {
            v = *(long *)buf;
            rv = v + 1;
            rt_msg_reply(chid, ri.rcvid, &rv, sizeof(rv), 0);
        }
        rt_msg_name_detach("/ipc/demo");
    }
    if (chid > 0)
        rt_msg_ch_destroy(chid);
}

/* 占先干扰:窗内 1 拍自旋+1 拍睡的占空循环(不给服务器让路就
 * 永远轮不到——提权让 recv 者越它而行,传播的活证面) */
void rt_msg_demo_spinner(void *arg)
{
    u64 until = rt_ticks + (u64)(unsigned long)arg;

    while ((long long)(until - rt_ticks) > 0) {
        u64 t = rt_ticks;

        while (rt_ticks == t)
            ;                          /* 1 拍自旋 */
        rt_sleep_ticks(1);             /* 1 拍睡:服务器(20)得隙起立 */
    }
}

/* pulsedemo 服务器:挂名+排 5 脉冲+总和自证后收场 */
void rt_msg_pulse_server(void *arg)
{
    int chid = rt_msg_ch_create(0);
    char buf[RT_MSG_BYTES];
    struct rt_recvinfo ri;
    u64 sum = 0;
    u32 got = 0;

    (void)arg;
    if (chid > 0 && rt_msg_name_attach("/ipc/pulse", chid) == 0) {
        while (got < 5) {
            if (srv_recv1(chid, buf, &ri, 300) != 0)
                break;                 /* 超时:脉冲失窃,提前收场 */
            if (ri.rcvid == 0) {
                sum += (u64)ri.code;
                got++;
            }
        }
        rt_msg_name_detach("/ipc/pulse");
        rt_puts_u64("pulse: drained ", (u64)got, " sum=");
        rt_puts_u64("", sum, "\n");
    }
    if (chid > 0)
        rt_msg_ch_destroy(chid);
}

/* abortdemo 服务器:挂名(CHF_UNBLOCK)+收下不发(持)+再收
 * UNBLOCK 脉冲(rcvid=0, code=-1,value=被持 rcvid)+礼貌 REPLY
 * 收尾槽+自证收场 */
void rt_msg_abort_server(void *arg)
{
    int chid = rt_msg_ch_create(RT_CHF_UNBLOCK);
    char buf[RT_MSG_BYTES];
    struct rt_recvinfo ri;
    u64 held = 0;

    (void)arg;
    if (chid > 0 && rt_msg_name_attach("/ipc/abort", chid) == 0) {
        if (srv_recv1(chid, buf, &ri, 300) == 0 && ri.rcvid)
            held = ri.rcvid;           /* 收下,故意不发(客户在等) */
        if (held && srv_recv1(chid, buf, &ri, 300) == 0 &&
            ri.rcvid == 0 && ri.code == RT_PULSE_UNBLOCK) {
            rt_puts_hex("abort: unblock pulse value=0x", ri.value,
                        "\n");
            rt_msg_reply(chid, held, buf, 0, -3);   /* 礼貌收尾槽 */
        }
        rt_msg_name_detach("/ipc/abort");
    }
    if (chid > 0)
        rt_msg_ch_destroy(chid);
}
