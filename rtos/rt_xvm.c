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

/* 站53:MDSCR_EL1/PMCR_EL0 探针——真硬件:MDCR_EL2.TDA/TPM 陷阱
 * 代访(mdscr 写读环=per-VM 槽 0x1234,pmcr 读 0=RAZ);QEMU TCG
 * 定谳(平台事实,站 53 实证):不实现 MDCR 陷阱,MDSCR=惰性存根
 * (写弃读 0),PMCR=ID 真值可读计数惰性——平台级无泄漏。 */
void k_dbgdemo(void)
{
    u64 md = 0, want = 0x1234UL, pm = 1UL;

    __asm__ volatile("msr mdscr_el1, %0" :: "r"(want));
    __asm__ volatile("mrs %0, mdscr_el1" : "=r"(md));
    __asm__ volatile("mrs %0, pmcr_el0" : "=r"(pm));
    if (md == want)
        rt_puts("dbg: mdscr trapped (per-vm emu live)\n");
    else
        rt_puts("dbg: mdscr quiescent (tcg stub, no trap)\n");
    if (pm == 0)
        rt_puts("dbg: pmu raz (tpm trap live)\n");
    else
        rt_puts("dbg: pmu id readable (tcg, counters inert)\n");
    rt_puts_u64("dbg: probe mdscr=", md, " pmcr=");
    rt_puts_hex("", pm, "\n");
}

/* ---- 站55:PSCI 补面(hvc #0 直调;psci_rtos_cpu_on 同 conduit) ---- */
static u64 psci_call(u64 fn, u64 a1)
{
    register u64 x0 __asm__("x0") = fn;
    register u64 x1 __asm__("x1") = a1;

    __asm__ volatile("hvc #0"
                     : "+r"(x0)
                     : "r"(x1)
                     : "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9",
                       "x10", "x11", "x12", "x13", "x14", "x15", "x16",
                       "x17", "memory");
    return x0;
}

void k_pscidemo(void)
{
    u64 r0, r1, rbad;

    r0 = psci_call(0x84000004UL, 0x80000000UL);   /* AFFINITY_INFO vcpu0 */
    r1 = psci_call(0x84000004UL, 0x80000001UL);   /* vcpu1 */
    rbad = psci_call(0x84000004UL, 0x8000000FUL); /* 无效帧 */
    rt_puts_u64("psci: aff vcpu0=", r0, " vcpu1=");
    rt_puts_u64("", r1, " invalid=");
    rt_puts_u64("", rbad, "\n");
}

void k_pscioff(void)
{
    rt_puts("psci: powering off\n");
    psci_call(0x84000008UL, 0);       /* SYSTEM_OFF:noreturn(VM 干净死) */
    rt_puts("psci: off returned?!\n"); /* 不可达(诚实面) */
}

void k_pscireset(void)
{
    rt_puts("psci: resetting\n");
    psci_call(0x84000009UL, 0);       /* SYSTEM_RESET:重建+重启 */
    rt_puts("psci: reset returned?!\n");
}
