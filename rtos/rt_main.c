/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "rtos.h"
#include "miniuart.h"
#include "gic.h"
#include "timer.h"
#include "fdt.h"
#include "hvc.h"
#include "kernel/include/version.h"
#include "eshine_log.h"

LOG_MODULE_REGISTER("boot");

/* eshine-rtos bring-up:与裸机客同一套设备自证链(同一 ABI 头),
 * 再起调度世界。IRQ 解掩(gic_guest_enable)在 RTOS 世界就绪之后——
 * 早到的 RX 中断也要有 rt_cur 可依。CPACR 由 guest 自设(站十五
 * 哲学:FPEN 归 guest,宿主不碰)。 */

static void vtimer_arm_periodic(void)
{
    u64 period = vtimer_freq() / RT_HZ;

    __asm__ volatile("msr cntv_cval_el0, %0"
                     :: "r"(vtimer_now() + period));
    __asm__ volatile("msr cntv_ctl_el0, %0" :: "r"(1UL));
}

/* 站二十:次核上线(PSCI CPU_ON,SMC conduit 走 EL2 EC 0x17 转发,
 * 19D 机制)。x1=客视角 MPIDR(VMPIDR=0x80000000|1),x2=入口,
 * x3=context_id=核号(次核入口 asm 拿它写 TPIDR_EL1)。 */
static u64 psci_rtos_cpu_on(u64 target, void *entry, u64 ctx)
{
    register u64 x0 __asm__("x0") = 0xc4000003UL;
    register u64 x1 __asm__("x1") = target;
    register u64 x2 __asm__("x2") = (u64)entry;
    register u64 x3 __asm__("x3") = ctx;

    __asm__ volatile("hvc #0"      /* 站二十:SMC 会被 QEMU 机器级
                                    * PSCI 截获(19D 实证:TCG conduit
                                    * 优先),hvc #0 才落我们的
                                    * EC 0x16 imm=0 PSCI 转发分支 */
                     : "+r"(x0)
                     : "r"(x1), "r"(x2), "r"(x3)
                     : "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11",
                       "x12", "x13", "x14", "x15", "x16", "x17", "memory");
    return x0;
}

/* 站二十:次核世界(EL2 置帧后 poke,本核自此活)。引导语境就地
 * 转正 idle1(池外伪任务,栈=链接器 .cpu1stack,入口 asm 已驻);
 * GIC 本核 ICV 面 + 自臂 vtimer + FPEN 自设,与主核引导同律。 */
void rtos_secondary_main(u32 cpu)
{
    rt_task_t *idle = rt_idle1_task();

    (void)cpu;                       /* tpidr 已在入口 asm 登记为 1 */
    /* 站二十八:次核同律开 stage-1(表幂等,寄存器 per-CPU)+蹦床 */
    rt_mm0_init();
    /* 蹦床不在此臂:SP_EL1=蹦床顶的不变量在 eret→EL0 边界维护
     * (vec.S .Lpop EL0 路径),首帧派发必经 .Lpop;此处直写
     * SP_EL1 曾把引导栈(恰驻 SP_EL1,EL1h 契约)覆写成蹦床址,
     * 冒烟定谳为 sp=0 压栈环绕→address-size fault→park */
    rt_cur = idle;
    idle->entry = (void (*)(void *))rt_idle_loop;
    idle->prio = RT_NPRIO - 1;
    idle->state = RT_RUN;
    idle->cpu = 1;
    idle->name[0] = 'c'; idle->name[1] = 'p'; idle->name[2] = 'u';
    idle->name[3] = '1'; idle->name[4] = ' '; idle->name[5] = 'i';
    idle->name[6] = 'd'; idle->name[7] = 'l'; idle->name[8] = 'e';
    idle->name[9] = '\0';
    idle->slice_left = RT_SLICE;
    __asm__ volatile("msr cpacr_el1, %0" :: "r"(3UL << 20));
    __asm__ volatile("isb");
    gic_secondary_frame_init(1);    /* 站二十:帧 1 GICR 面(WAKER/
                                      * SGI0+PPI27 使能——ipidemo rx=0
                                      * 定谳:次核不开自家帧,SGI 取走
                                      * 被 isenabler 门滤掉) */
    gic_guest_enable();              /* 本核 ICV 面(PMR/IGRPEN/解掩) */
    vtimer_arm_periodic();           /* 本核 tick 起拍:RT_HZ 同律 */
    rt_puts_u64("cpu1: online (rtos psci) gicr1_en=",
                (u64)vgicr_read(0x20000UL + GICR_SGI + GICR_ISENABLER0),
                "\n");              /* 站二十:帧 1 使能回读自证
                                      * (SGI0|PPI27 应见 0x08000001) */
    rt_idle_loop();                  /* noreturn:idle1 即本核引导语境 */
}

/* ---- 站23A:irq handler 注册表 ----
 * 硬编码四分支(27/0/33/35)退役:登记表替换式(一 intid 一槽,重复
 * 登记=替换,返回旧 isr 供链式包装/恢复——irqdemo 判据面);IAR→
 * 查表→isr 先行、掩码后醒(RT_RX_BIT 等待位泛化至此,uart 登记
 * 携带,余为 0);无登记 intid 一行报(默认拒绝面诚实化,rt_puts
 * ISR 安全同站二十二契约)。登记窗口期(替换瞬间)同核序贯,IRQ
 * 走新旧槽皆可,语义无害。 */
static struct {
    u32 intid;
    void (*isr)(void);
    u32 wake_mask;
} rt_irq_tab[RT_IRQ_MAX];

void (*rt_irq_register(u32 intid, void (*isr)(void), u32 wake_mask))(void)
{
    void (*old)(void) = NULL;
    unsigned int i;

    for (i = 0; i < RT_IRQ_MAX; i++) {
        if (rt_irq_tab[i].intid == intid && rt_irq_tab[i].isr) {
            old = rt_irq_tab[i].isr;
            rt_irq_tab[i].isr = isr;
            rt_irq_tab[i].wake_mask = wake_mask;
            return old;
        }
    }
    for (i = 0; i < RT_IRQ_MAX; i++) {
        if (!rt_irq_tab[i].isr) {
            rt_irq_tab[i].intid = intid;
            rt_irq_tab[i].isr = isr;
            rt_irq_tab[i].wake_mask = wake_mask;
            break;
        }
    }
    return old;                      /* 空槽登记:无旧主(NULL) */
}

/* 登记期四 isr(原内联逻辑原样移槽;唤醒走表掩码,不再各写各的) */
static void isr_vtimer(void)
{
    u64 period = vtimer_freq() / RT_HZ;
    u64 cval;

    /* 站二十:全局 tick 记账单写者化(核 0 独记,核 1 只读)。 */
    if (rt_cpu_id() == 0)
        rt_ticks++;
    __asm__ volatile("mrs %0, cntv_cval_el0" : "=r"(cval));
    /* 周期重臂:上拍到期值为基(+period),零漂移;泵扫描注入路径
     * 可能已熄 ctl,一并点回。 */
    __asm__ volatile("msr cntv_cval_el0, %0" :: "r"(cval + period));
    __asm__ volatile("msr cntv_ctl_el0, %0" :: "r"(1UL));
    rt_tick_bookkeep();
}

static void isr_uart(void)
{
    rt_uart_drain_isr();
}

static void isr_ipi(void)
{
    rt_resched_ipi();                /* 站二十:重调度判+收拍计数 */
}

/* 站23A:uart 槽换钩助手——INTID 常量留 gic.h 的合法 TU
 * (gic.h 是单含静态头,拖进 rt_demo.c 会产未用静态警);demo 经
 * 此换/恢复 33 号槽(RT_RX_BIT 唤醒掩码恒由 uart 面携带)。 */
void (*rt_irq_swap_uart(void (*isr)(void)))(void)
{
    return rt_irq_register(GUEST_INTID_UART, isr, RT_RX_BIT);
}

rt_task_t *rt_irq_dispatch(void)
{
    u64 iar = gic_claim();
    unsigned int i;
    int hit = 0;

    for (i = 0; i < RT_IRQ_MAX; i++) {
        if (rt_irq_tab[i].isr && rt_irq_tab[i].intid == (u32)iar) {
            rt_irq_tab[i].isr();
            if (rt_irq_tab[i].wake_mask)
                rt_task_wake_all(rt_irq_tab[i].wake_mask);
            hit = 1;
            break;
        }
    }
    if (!hit && iar != GUEST_INTID_SPURIOUS) {
        /* 站23 评审:报面默认拒绝=诚实,但持续未知源在 ISR 语境
         * 行级洪泛;同源抑制一静态量(首发必报,重复源静默——
         * 变换源仍各报一次,残余面记 devlog)。 */
        static u32 unclaimed_prev;

        if (iar != unclaimed_prev) {
            unclaimed_prev = (u32)iar;
            rt_puts_u64("irq: unclaimed ", iar, "\n");
        }
    }
    if (iar != GUEST_INTID_SPURIOUS)
        gic_eoir(iar);
    /* 出口判决:要切(轮转/更高优先就绪)或当前已非 RUN(阻塞/睡/死)
     * 则摘队头;被抢占的原任务排回队尾。
     * 站19 [41] 第二次定谳:醒客一律回流令牌(rt_ready_requeue,不碰
     * 状态)。醒客帧有两种冻结位——wfi 循环体内(被本出口从中切走的
     * 驻留者)与 .Lcoop_ret(正常让位)。前者醒来落在循环里,靠自拣
     * 兑付令牌才能 *返回调用者*(否则醒令被吞,state 已被改成 RUN,
     * wake_sleepers/wake_all 从此永久跳过——旧代码全靠僵尸条目碰巧
     * 在队才没必现,即 [41] 闪断的真身);后者醒来直接回调用者,残
     * 留令牌由各阻塞位点的 rdy_remove 清除,不残留不占位。 */
    if (rt_need_switch() || rt_cur->state != RT_RUN) {
        rt_task_t *next = rt_pick_ready();

        if (next) {
            rt_ready_requeue(next);
            next->state = RT_RUN;
            if (next != rt_cur && rt_cur->state == RT_RUN)
                rt_preempt_requeue(rt_cur);  /* 被抢占者恒单份回队 */
            return next;
        }
    }
    return NULL;
}

static rt_task_t *shell_task;        /* 站24A:心跳判别器——壳自主睡
                                      * (RT_SLEEP)才说话;at-prompt=
                                      * RT_BLOCK(rt_block_rx)/命令处理
                                      * (RUN/READY)皆静默 */

static void heartbeat_task(void *arg)
{
    (void)arg;
    for (;;) {
        rt_sleep_ticks(50);          /* 0.5s 一拍:任务级交错的可见判据 */
        /* 站24A(用户令"进入 rtos shell 不要一直打印,可以输入
         * 命令再打印"):心跳只在壳自主睡时说话——替睡死的壳报
         * 平安,不打扰正用的人;sleep 窗([24] 任务级并发活证)
         * 语义保全。前台门(HVC#5)保留:后台恒静默(站20 用户令)。
         * 按需面交 hb 命令。 */
        if (shell_task && shell_task->state == RT_SLEEP && hvc_fg())
            rt_puts_u64("rtos: alive tick=", rt_ticks, "\n");
    }
}

void rt_idle_loop(void)
{
    for (;;)
        rt_sched();                   /* 就绪者上场;空则内含 WFI 让位 */
}

void guest_main(void)
{
    rt_task_t *idle = rt_idle_task();

    __asm__ volatile("msr tpidr_el1, %0" :: "r"(0UL));  /* 站二十:本核
                                              * 索引登记(簿记宏/rt_cur 宏;
                                              * 次核入口自写 1) */
    /* 站二十八:两区式 stage-1 v0 先于一切 MMIO(设备窗入表后
     * vuart/vgic/virtio 陷阱照走 stage-2 EMULATED 面);SP_EL1=
     * 本核蹦床顶(EL0 异常入口的 scratch 域,vec.S 契约);syscall
     * 表先立(壳首发即达)。恒等映射切换取指连续,零过渡窗。 */
    rt_mm0_init();
    /* 蹦床不在此臂(不变量归 eret→EL0 边界,见 vec.S .Lpop;
     * 此处直写 SP_EL1 曾覆写引导栈致 sp=0 压栈环绕,冒烟定谳) */
    rt_syscall_init();
    uart_pl011_init();                /* 站13:身份证+开机(先开机才有输出) */
    rt_puts("eshine-rtos " ESHINE_VERSION_STR " @EL1 via stage-2 (vuart @trap)\n");
    /* RTOS 世界先立(引导语境转正 idle):任何潜在 IRQ 到达前,rt_cur 必须有效 */
    rt_cur = idle;
    idle->entry = (void (*)(void *))rt_idle_loop;   /* 非 NULL:ps 可见 */
    idle->prio = RT_NPRIO - 1;
    idle->state = RT_RUN;
    idle->name[0] = 'i'; idle->name[1] = 'd'; idle->name[2] = 'l';
    idle->name[3] = 'e'; idle->name[4] = '\0';
    idle->slice_left = RT_SLICE;
    gic_dist_init();                  /* 站12:vGIC 真启动式 */
    rt_blk_boot();                    /* 站39:探针相(rt_blk 单 TU) */
    fdt_check();                      /* 站11:三节点自查 */
    __asm__ volatile("msr cpacr_el1, %0" :: "r"(3UL << 20));  /* FPEN 自设 */
    __asm__ volatile("isb");

    /* 任务世界:壳与心跳上池 */
    rt_ticks = 0;
    /* 站二十八:壳=EL0 用户任务(内核栈+用户栈双就绪,vec.S 锻
     * EL0t 首帧;心跳维持内核任务——demo 期过渡态见 spec §4) */
    shell_task = rt_task_create_el0("shell", rt_shell_task, NULL, 5,
                                    rt_shell_ustack + sizeof(rt_shell_ustack));
    /* 站29:进程 1=壳——per-process PGD/ASID 建立+.user 镜像/栈 VMA
     * 申报+eager 预映(首指令前无缺页悬念;ttbr 已入任务帧)。 */
    rt_proc_boot_shell(shell_task);
    rt_task_create("heartbeat", heartbeat_task, NULL, 10);
    /* 站31:开机链(参考实现 PM 固定序列形状,协商位留后)——tty->
     * ramfs->pm。spawn 序=同优先级 FIFO 序:壳先跑打横幅后阻塞,
     * 服务器逐个起立挂名,PM 的 stdio open 一次即中。 */
    rt_xvm_boot();                 /* 站47:跨 VM 服务生产者(常驻,静默) */
    rt_cpart_init();               /* 站45:分区 0=system(100%)
                                 * 先于 srv boot=服务器入 system 组 */
    rt_srv_boot(shell_task);

    /* 站23A:四路登记(解 IRQ 前无中断,序贯免锁)——27 tick 单
     * 写者面/0 SGI 重调度/33 uart drain+RX 唤醒/35 virtio 完成。 */
    rt_irq_register(GUEST_INTID_VTIMER, isr_vtimer, 0);
    rt_irq_register(GUEST_INTID_IPI, isr_ipi, 0);
    rt_irq_register(GUEST_INTID_UART, isr_uart, RT_RX_BIT);
    vtimer_arm_periodic();            /* 100Hz tick 起拍 */
    gic_guest_enable();              /* 解 IRQ:此后 tick/RX 走真中断 */

    rt_puts("uart irq on\n");         /* 与裸机同 needle(RXIM 已使能) */
    rt_puts("virtio blk on\n");
    rt_puts("rtos: up (eshine-rtos " ESHINE_VERSION_STR ") 32-prio bitmap, tick 100Hz\n");
    LOG_INF("log core up, mods=%u\n", rt_log_mod_count());

    /* 站二十:次核上线(世界已立:调度器/队/锁/SGI 机制全就绪才可)。
     * EL2 置帧+poke,cpu3 专泵扫到即跑;拒绝只报不炸(单核退化仍活)。 */
    if (psci_rtos_cpu_on(0x80000001UL, rtos_secondary_entry, 1) != 0)
        rt_puts("smp: cpu1 cpu_on denied\n");

    rt_idle_loop();                   /* noreturn:idle 即引导语境 */
}
