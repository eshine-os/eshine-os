/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "rtos.h"
#include "miniuart.h"
#include "hvc.h"
#include "kernel/include/version.h"

/* 站二十八:syscall 分发+内核镜像命令蹦床。
 *
 * 帧契约(vec.S guest_el0_sync):传入帧=内核栈上的用户上下文,
 * [0]=elr,[1]=psr,x_i 槽字节偏移 16+16i(与 RT_PUSH_GPRS 严格
 * 镜像);x8=号,x0-x5=参,返回值写 x0 槽,eret 即达用户。
 *
 * ABI=Linux arm64 标准(偏差 #27:对参考实现七参走结构体指针);
 * 号面:asm-generic 原样(read/write/getpid)+eshine 扩展 512 起;
 * 静态表 [ES_NR_MAX] 全填 ni 后覆写(参考实现分发形态,实现分文件
 * ——本仓按面分文件即本文件+后续站各面文件,不蹈其 5456 行
 * 单文件审计债)。
 *
 * 蹦床(sys_demo):EL0 壳让内核语境跑既有命令体,80 门判据逐
 * 字节不变(体从 rt_shell.c 原样移植);30+ 各面 syscall 化后
 * 逐批退役。uaccess=access_ok 用户窗校验+直接拷贝(v0 单窗静态
 * 映射无缺页,cortex-a57 无 PAN,30+ 按需上异常表)。 */

/* x_i 槽(字节偏移,帧基=elr 槽):RT_PUSH_GPRS 是紧打包——
 * stp 双寄存器 16B/对无 hole,仅 x0 的 str 带 8B hole(站16
 * rt_frame_init 注释自证:"x1 槽在 +32,x0 槽后带 8B hole")。
 * 布局(字节):elr@0 psr@8 x0@16 hole@24 x_i@24+8i(i>=1)。
 * 站28 冒烟定谳:曾按 u64 16B 步长错读(FR(8) 落 x15 槽),壳的
 * write/read 全打 sys_ni 返 -ENOSYS——静默提示符+每秒百万次
 * ENOSYS SVC 风暴一案全解。 */
#define FR(i) (*(u64 *)((char *)frame + ((i) == 0 ? 16 : 24 + 8 * (i))))

#define ES_EFAULT 14

#define UWIN_LO 0x40200000UL
#define UWIN_HI 0x41000000UL

static int access_ok(const void *p, u64 len)
{
    u64 a = (u64)p;

    return a >= UWIN_LO && len <= UWIN_HI - UWIN_LO && a <= UWIN_HI - len;
}

/* ---------------- 标准面 ---------------- */

static long sys_read(u64 *frame)
{
    char *ub = (char *)FR(1);
    long c;
    for (;;) {
        c = rt_getc_nb();
        if (c >= 0)
            break;
        rt_block_rx();               /* 站17 机器原样:DAIF 窗+RT_RX_BIT */
    }
    ub[0] = (char)c;
    return 1;
}

static long sys_write(u64 *frame)
{
    const char *ub = (const char *)FR(1);
    u64 len = FR(2);
    char kbuf[256];

    if (len > sizeof(kbuf))
        len = sizeof(kbuf);
    if (len && !access_ok(ub, len))
        return -ES_EFAULT;
    for (u64 i = 0; i < len; i++)
        kbuf[i] = ub[i];
    rt_puts_n(kbuf, (u32)len);
    return (long)len;
}

static long sys_getpid(u64 *frame)
{
    (void)frame;
    return (long)rt_pid_of(rt_cur);
}

static long sys_sysinfo(u64 *frame)
{
    struct rt_sysinfo *ub = (struct rt_sysinfo *)FR(0);
    u64 len = FR(1);
    struct rt_sysinfo si;
    u32 i;

    if (len < sizeof(si))
        return -ES_EINVAL;
    if (!access_ok(ub, sizeof(si)))
        return -ES_EFAULT;
    for (i = 0; i + 1 < sizeof(si.ver) && ESHINE_VERSION_STR[i]; i++)
        si.ver[i] = ESHINE_VERSION_STR[i];
    for (; i < sizeof(si.ver); i++)
        si.ver[i] = '\0';
    si.tick = rt_ticks;
    si.el0 = 1;
    si.rsv = 0;
    *ub = si;
    return 0;
}

/* ---------------- eshine 扩展面 ---------------- */

static long sys_hvc(u64 *frame)
{
    long fn = (long)FR(0);          /* 首参 x0(壳 sys1 映射) */

    switch (fn) {
    case 1:
        hvc_yield();                 /* EL1 语境发 HVC,EL2 往返 */
        return 0;
    case 2:
        hvc_host();
        return 0;
    case 3:
        hvc_exit();                  /* noreturn:VM_DEAD 内核收尸 */
        __builtin_unreachable();
    default:
        return -ES_EINVAL;
    }
}

/* ---- 内核镜像命令体(自 rt_shell.c 原样移植:判据字节不变) ---- */

static void k_selftest(void)
{
    u64 sum = 0;
    unsigned int i;

    for (i = 0; i < 100; i++) {
        sum += i;
        __asm__ volatile("" : "+r"(sum));   /* 挡常量折叠:寄存器存活性判据 */
        uart_putc('.');   /* 100 次 MMIO 陷阱(同裸机判据) */
    }
    if (sum == 4950)
        rt_puts("\nselftest OK\n");
    else
        rt_puts("\nselftest FAIL\n");
}

static void k_ps(void)
{
    unsigned int idx;
    rt_task_t *t = rt_ps_first(&idx);
    rt_puts("ps: task table (prio 0 high)\n");
    while (t) {
        if (t->entry && t->state != RT_DEAD) {
            rt_puts("  ");
            rt_puts(t->name);
            rt_puts(" prio ");
            rt_puts_u64("", t->prio, "");
            rt_puts("  ");
            rt_puts(rt_state_name(t->state));
            rt_puts(" stk ");
            if (idx < RT_POOL_MAX)
                rt_puts_u64("", rt_cur_stack_margin(idx), "");
            else
                rt_puts("-");
            rt_puts("\n");
        }
        t = rt_ps_next(&idx);
    }
}

static void k_stackcheck(void)
{
    unsigned int idx = 0;
    int bad = 0;
    rt_task_t *t = rt_ps_first(&idx);

    while (t) {
        if (t->entry && t->state != RT_DEAD && idx < RT_POOL_MAX) {
            rt_puts("stk: ");
            rt_puts(t->name);
            rt_puts_u64(" margin ", rt_cur_stack_margin(idx), "\n");
            if (rt_stack_slot_ok(idx) == 0) {
                rt_puts("stk: magic BAD in ");
                rt_puts(t->name);
                rt_puts("\n");
                bad = 1;
            }
        }
        t = rt_ps_next(&idx);
    }
    if (!bad)
        rt_puts("stk: magic ok\n");
}

static void k_spawned_task(void *arg)
{
    rt_puts((const char *)arg);
    rt_puts(": hello from task\n");
}

static void k_spawn(const char *uname)
{
    static char spawn_name[12];
    const char *nm = uname;
    unsigned int i;

    for (i = 0; i < sizeof(spawn_name) - 1 && nm[i]; i++)
        spawn_name[i] = nm[i];
    spawn_name[i] = '\0';
    if (i == 0) {
        rt_puts("usage: spawn <name>\n");
    } else if (!rt_task_create(spawn_name, k_spawned_task, spawn_name, 8)) {
        rt_puts("spawn: pool full\n");
    }
    /* 成功静默:问候行由任务自己打 */
}

static void k_fpstat(void)
{
    rt_puts_u64("lazy: ", rt_fp_stat_total(), " switches, ");
    rt_puts_u64("", rt_fp_saves, " fp-saves\n");
}

static long sys_demo(u64 *frame)
{
    long id = (long)FR(0);          /* 首参 x0:壳 sys2(nr,id,0) 映射
                                     * id→x0(冒烟定谳:曾读 x1 恒 0,
                                     * 全部命令静默 EINVAL) */

    switch (id) {
    case RT_U_SELFTEST:  k_selftest();   break;
    case RT_U_PS:         k_ps();         break;
    case RT_U_SPAWN:      k_spawn((const char *)FR(1)); break;
    case RT_U_FP:
        if (rt_task_create("fptest", rt_fp_task, NULL, 12))
            rt_puts("fp: task spawned\n");
        else
            rt_puts("fp: pool full\n");
        break;
    case RT_U_SEMDEMO:    rt_demo_sem();   break;
    case RT_U_MUTEXDEMO:  rt_demo_mutex(); break;
    case RT_U_PIDEMO:     rt_demo_pi();    break;
    case RT_U_FPDUAL:     rt_demo_fpdual();break;
    case RT_U_EVENTDEMO:  rt_demo_event(); break;
    case RT_U_QUEUEDEMO:  rt_demo_queue(); break;
    case RT_U_SMPDEMO:    rt_demo_smp();   break;
    case RT_U_IPIDEMO:    rt_demo_ipi();   break;
    case RT_U_FPSMP:      rt_demo_fpsmp(); break;
    case RT_U_TMODEMO:    rt_demo_tmo();   break;
    case RT_U_SWTDEMO:    rt_demo_swt();   break;
    case RT_U_IRQDEMO:    rt_demo_irq();   break;
    case RT_U_LOWDEMO:    rt_demo_low();   break;
    case RT_U_STKDEMO:    rt_demo_stk();   break;
    case RT_U_STACKCHECK: k_stackcheck(); break;
    case RT_U_OVERFLOW:  rt_demo_overflow(); break;
    case RT_U_FPSTAT:     k_fpstat();      break;
    case RT_U_HB:
        rt_puts_u64("rtos: alive tick=", rt_ticks, "\n");
        break;
    case RT_U_SLEEP:
        rt_sleep_ticks((u64)FR(1) * RT_HZ);
        break;
    default:
        return -ES_EINVAL;
    }
    return 0;
}

/* ---------------- 分发表 ---------------- */

typedef long (*rt_sysfn_t)(u64 *frame);

static long sys_ni(u64 *frame)
{
    (void)frame;
    return -ES_ENOSYS;
}

static rt_sysfn_t table[ES_NR_MAX];

void rt_syscall_init(void)
{
    for (u32 i = 0; i < ES_NR_MAX; i++)
        table[i] = sys_ni;
    table[ES_NR_read]    = sys_read;
    table[ES_NR_write]   = sys_write;
    table[ES_NR_getpid]  = sys_getpid;
    table[ES_NR_sysinfo] = sys_sysinfo;
    table[ES_NR_hvc]     = sys_hvc;
    table[ES_NR_demo]    = sys_demo;
}

void rt_syscall_dispatch(u64 *frame)
{
    u64 nr = FR(8);

    FR(0) = (nr < ES_NR_MAX) ? (u64)table[nr](frame) : (u64)-ES_ENOSYS;
}

/* EL0 非 SVC 异常的 v0 诚实终态面(29+ per-process 缺页接管后退役):
 * 裸打(站21B panic 语境取锁=同核自死锁教训)——打印 ESR/ELR/FAR
 * 后走 rt_panic 扫描面。 */
void rt_el0_fault(u64 *frame)
{
    u64 esr, far;

    __asm__ volatile("mrs %0, esr_el1" : "=r"(esr));
    __asm__ volatile("mrs %0, far_el1" : "=r"(far));
    rt_puts_hex("EL0 fault: esr=0x", esr,
                " (v0: no demand paging, see station-29)\n");
    rt_puts_hex("EL0 fault: elr=0x", frame[0], "\n");
    rt_puts_hex("EL0 fault: far=0x", far, "\n");
    rt_panic("EL0 sync fault (v0 terminal face)",
             (unsigned int)(rt_pid_of(rt_cur) - 1));
}
