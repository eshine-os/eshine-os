/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "rtos.h"

/* 站四十七:跨 VM 服务面(safeshm 窗+vm0 设备后端)。参考实现
 * xspace 全局窗/VM_SET 白描的站内形:宿主自有 2MB 物理雕刻
 * (PA 0x52000000)经 stage-2 双映 vm0/vm1(IPA 0x41200000 块 9);
 * vm0 侧窗口在 stage-1 入内核 EL1-only(设备后端形:EL0 经
 * demo op 代访=设备驱动同律);vm1 侧裸机直访。
 *
 * 协议(safeshm):4KB 首页 {seq,ack,text[64]}——写方先 text
 * 后 dmb 再 bump seq(序的诚实);读方见 seq 变→dmb→读 text→
 * ack=seq(代际协议防撕裂复用;复用前写方必等 ack)。零拷
 * (参考实现 VM_SET 单拷的优记档)。 */

struct xvm_win {
    volatile u64 seq;                /* vm0→vm1 代际(写方先备货后递增) */
    volatile u64 ack;                /* vm1→vm0 应答(镜像 seq) */
    char text[64];                   /* 负载:seq 递增前写妥 */
};

#define XVM_WIN ((struct xvm_win *)0x41200000UL)

static void xvm_srv(void *arg)
{
    struct xvm_win *w = XVM_WIN;
    static const char msg[] = "ping from vm0";
    u32 i;

    (void)arg;
    for (;;) {
        if (w->ack != w->seq) {         /* 槽位占用:候 ACK(一次性窗) */
            u64 deadline = rt_ticks + 150;

            while (w->ack != w->seq && rt_ticks < deadline)
                rt_sleep_ticks(10);
            if (w->ack == w->seq)
                rt_puts_u64("xvm: round-trip seq=", w->seq, " ok\n");
        } else {                       /* 槽位空:静默发送(前台独占泵律
                                      * 下生产者开机常驻;打印只在真
                                      * 交换发生=站24 安静承诺) */
            for (i = 0; i < sizeof(w->text); i++)
                w->text[i] = (i < sizeof(msg) - 1) ? msg[i] : '\0';
            __asm__ volatile("dmb ishst" ::: "memory");
            w->seq++;
        }
        rt_sleep_ticks(50);             /* 下轮(2s 期) */
    }
}

/* boot:跨 VM 服务生产者任务(vm0=设备后端;消费面=vm1 客 xvm)。
 * 前台独占泵律(站19D:console 按 VM 独占)下,vm0/vm1 只在
 * owner=NULL(host 模式)时并跑——跨 VM 交换窗必经宿主面。 */
void rt_xvm_boot(void)
{
    if (!rt_task_create("xvmsrv", xvm_srv, NULL, 15))
        rt_puts("srv: xvmsrv spawn failed\n");
}

/* ---- 站四十八:健康面板(批四面合卷:43 观测/45 分区/46 崩溃/
 *      47 跨 VM;一屏自证,门 [128]) ---- */

extern u32 rt_fp_saves;                   /* rt_fp.c(计数面) */

u32 rt_proc_count(void);                  /* rt_as.c 池计数 */

void rt_health_show(void)
{
    struct xvm_win *w = XVM_WIN;
    unsigned int idx = 0, tasks = 0;
    rt_task_t *t;

    while ((t = rt_ps_next(&idx)) != NULL) {
        if (t != rt_idle_task() && t->state != RT_FREE &&
            t->state != RT_DEAD)
            tasks++;
    }
    rt_puts_u64("health: uptime ", (u64)rt_ticks, " tick");
    rt_puts_u64(", tasks ", tasks, "");
    rt_puts_u64(", procs ", rt_proc_count(), "");
    rt_puts_u64(", fp-saves ", rt_fp_saves, "\n");
    rt_puts_u64("health: cparts ", rt_cpart_used(), "");
    rt_puts_u64(", mparts ", rt_mpart_used(), "");
    rt_puts_u64(", crashes ", rt_crash_count(), "");
    rt_puts_u64(", xvm seq ", w->seq, "");
    rt_puts_u64(" ack ", w->ack, "\n");
}
