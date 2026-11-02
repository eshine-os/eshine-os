/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
/* VM 抽象:静态双槽 + 配置驱动;表驱动状态机,表外 panic(默认拒绝) */
#include "arch/arm64/include/mm.h"
#include "kernel/include/console.h"
#include "kernel/include/dtb.h"
#include "kernel/include/panic.h"
#include "kernel/include/util.h"
#include "kernel/include/vcpu.h"
#include "drivers/include/gicv3.h"
#include "drivers/include/timer.h"
#include "kernel/include/sched.h"
#include "kernel/include/vm.h"

static struct eshine_vm vm_table[ESHINE_MAX_VMS];

/* 预定义配置(第十四站几何表化):RAM 窗口不重叠 —— VM0 0x50000000/16MB,
 * VM1 0x51000000/16MB,VM2(Linux)0x58000000/128MB(恰占满 QEMU 512MB 顶,
 * 与 0/1 槽有安全距离);同 IPA 0x40000000 不同 PA,隔离由映射层保证
 * (物理事实,非约定)。入口与 x0(DTB 指针,Linux 入口协议)随槽几何化。 */
#define VM_RAM_IPA    0x40000000UL
#define VM_UART_IPA   0x09000000UL
#define VM_UART_PA    0x09000000UL
#define VM_UART_SIZE  0x1000UL
#define VM_DTB_IPA    0x40F00000UL    /* guest 视角 DTB 指针(= RAM_IPA + 0xF00000) */
#define VM_DTB_OFF    0xF00000UL      /* 第十一站:固定偏移,bootloader 同构 */
#define VM_DTB_BUDGET 0x10000UL        /* 64KB 预算 */

struct vm_geometry {
    u64 ram_pa;
    u64 ram_size;
    u64 entry;                     /* IPA 入口:裸机=RAM 基,Linux=+0x80000(text_offset) */
    u32 n_vcpu;                    /* 站二十:槽位 vCPU 数入几何表(拆 19D
                                   * 的 id==2 硬编码)——vm0 双核/裸机
                                   * 对照组单核/Linux 双核 */
    const unsigned char *dtb;      /* per-slot 树(第十四站:裸机/Linux 两树) */
    const unsigned char *dtb_end;
};

static const struct vm_geometry vm_geo[ESHINE_MAX_VMS] = {
    [0] = { 0x50000000UL, 0x1000000UL, 0x40000000UL, 2,
            eshine_dtb, eshine_dtb_end },
    [1] = { 0x51000000UL, 0x1000000UL, 0x40000000UL, 1,
            eshine_dtb, eshine_dtb_end },
    [2] = { 0x58000000UL, 0x8000000UL, 0x40080000UL, 2,   /* Linux:128MB,Image@+0x80000 */
            eshine_linux_dtb, eshine_linux_dtb_end },
};

/* 站十九D:VM×vCPU→pCPU 静态绑定(不做迁移/均衡,YAGNI 红线):
 * vm2 双 vCPU——vcpu0 专泵 CPU1,vcpu1(guest 的 PSCI CPU_ON 后才有
 * 帧)专泵 CPU2。站二十:vm0 升双核——vcpu0 留 CPU0(与宿主 shell/
 * vm1 轮转,前台面),vcpu1(RTOS 次核,PSCI CPU_ON 后才有帧)专泵
 * CPU3(真物理并行,-smp 4)。 */
u32 vm_cpu_map[ESHINE_MAX_VMS][ESHINE_MAX_VCPUS] = {
    /* 站19D 原设计 vcpu0@CPU1/vcpu1@CPU2 与实验变体的教训见 git 史;
     * tick 生命周期竞态与泵语境无关([53]/[41] 族放大面,冻点恒落
     * SMP 交接窗 ~0.2s 内核时),站二十 [41] 五案闭环后四核扩面。 */
    {0, 3}, {0, 0}, {1, 2},
};

static bool vm_on_cpu(u32 id, u32 cpu)
{
    u32 vi;

    for (vi = 0; vi < ESHINE_MAX_VCPUS; vi++) {
        if (vm_cpu_map[id][vi] == cpu)
            return true;
    }
    return false;
}


static void vm_id_label(u32 id)
{
    console_puts("vm");
    util_putdec(id);
    console_puts(": ");
}

static const char *names[VM_STATE_MAX] = {
    [VM_INIT]      = "VM_INIT",
    [VM_CREATED]   = "VM_CREATED",
    [VM_RUNNING]   = "VM_RUNNING",
    [VM_PAUSED]    = "VM_PAUSED",
    [VM_TEARDOWN]  = "VM_TEARDOWN",
    [VM_DEAD]      = "VM_DEAD",
};

/* allowed[current][next] — 唯一合法转换集。
 * 第二站新增两条:CREATED→TEARDOWN(vmkill),DEAD→INIT(槽位复用)。 */
static const bool allowed[VM_STATE_MAX][VM_STATE_MAX] = {
    [VM_INIT]     = { [VM_CREATED]  = true, },
    [VM_CREATED]  = { [VM_RUNNING]  = true, [VM_TEARDOWN] = true, },
    [VM_RUNNING]  = { [VM_PAUSED]   = true, [VM_TEARDOWN] = true, },
    [VM_PAUSED]   = { [VM_RUNNING]  = true, [VM_TEARDOWN] = true, },
    [VM_TEARDOWN] = { [VM_DEAD]     = true, },
    [VM_DEAD]     = { [VM_INIT]     = true, },
};

const char *vm_state_name(enum vm_state s)
{
    if (s >= VM_STATE_MAX)
        return "VM_?";
    return names[s];
}

bool vm_transition(struct eshine_vm *vm, enum vm_state next)
{
    if (next >= VM_STATE_MAX || !allowed[vm->state][next])
        panic("vm_transition: illegal state transition");
    vm->state = next;
    return true;
}

int vm_create(u32 id)
{
    struct eshine_vm *vm;

    if (id >= ESHINE_MAX_VMS) {
        console_puts("no such vm\n");
        return -1;
    }
    vm = &vm_table[id];
    if (vm->state == VM_DEAD)
        vm_transition(vm, VM_INIT);            /* DEAD→INIT 槽位复用 */
    if (vm->state != VM_INIT) {
        vm_id_label(id);
        console_puts("denied: slot busy\n");
        return -1;
    }

    vm->vmid = id;
    vm->n_mem = 1;
    vm->mem[0].ipa  = VM_RAM_IPA;
    vm->mem[0].pa   = vm_geo[id].ram_pa;
    vm->mem[0].size = vm_geo[id].ram_size;
    vm->mem[0].attr = S2_ATTR_NORMAL;
    if (id == 0 || id == 1) {
        /* 站47:跨 VM 服务窗(safeshm)——2MB 双映 vm0/vm1
         * @IPA 0x41200000(块 9)→PA 0x52000000(三客 RAM 之外
         * 空档,spec §3 定谳);配置权唯一属宿主=安全面所在。
         * 参考实现 xspace 全局窗白描的跨 VM 形;零拷(VM_SET 单拷
         * 的优记档)。 */
        vm->mem[1].ipa  = 0x41200000UL;
        vm->mem[1].pa   = 0x52000000UL;
        vm->mem[1].size = 0x200000UL;
        vm->mem[1].attr = S2_ATTR_NORMAL;
        vm->n_mem = 2;
    }
    vm->devices[0].ipa  = VM_UART_IPA;
    vm->devices[0].pa   = 0;                /* 仿真设备无 PA:不映射,只有窗口 */
    vm->devices[0].size = VM_UART_SIZE;
    vm->devices[0].attr = S2_ATTR_EMULATED; /* 撤直通:访问即 data abort 陷阱 */
    vm->devices[0].dev  = VM_DEV_VUART;
    vm->devices[1].ipa  = VIRTIO_BASE;     /* 第十站:virtio-mmio 窗口(承接 vblk 槽位) */
    vm->devices[1].pa   = 0;
    vm->devices[1].size = 0x1000UL;
    vm->devices[1].attr = S2_ATTR_EMULATED;
    vm->devices[1].dev  = VM_DEV_VIRTIO;

    /* DTB 注入(第十一站):整树拷进 guest RAM 固定偏移,bootloader 同构。
     * 头校验(magic+totalsize==实测)+预算检查,坏 blob 默认拒绝 create;
     * 双 VM 共树(IPA 硬件一致),EL2 恒等映射直写 PA 即可。 */
    {
        const unsigned char *blob = vm_geo[id].dtb;
        u64 dtb_size = (u64)(vm_geo[id].dtb_end - blob);
        u32 be32_0 = ((u32)blob[0] << 24) | ((u32)blob[1] << 16) |
                     ((u32)blob[2] << 8) | (u32)blob[3];
        u32 be32_4 = ((u32)blob[4] << 24) | ((u32)blob[5] << 16) |
                     ((u32)blob[6] << 8) | (u32)blob[7];
        if (be32_0 != 0xd00dfeedUL || be32_4 != dtb_size ||
            dtb_size > VM_DTB_BUDGET) {
            vm_id_label(id);
            console_puts("dtb rejected (bad blob)\n");
            return -1;
        }
        memcpy((void *)(vm_geo[id].ram_pa + VM_DTB_OFF), blob, dtb_size);
    }
    vuart_init(&vm->vu);
    memset(&vm->virtio, 0, sizeof(vm->virtio));  /* regs 复位;磁盘数据不在生命周期内 */
    /* 站二十:重分发帧数随几何表(拆 19D 的 id==2 硬码)——vm0 双核后
     * 次核同样要帧 1 面;nframes=1 时 vgic 的挂起循环(f<nframes)与
     * sgi_take/重分发读写的帧卫兵把帧 1 全 WI/RAZ,次核的 SGI0 使能
     * 写读回恒 0(ipidemo rx1=0 定谳:窗口已宽,帧数没跟上)。 */
    vgic_reset(&vm->vgic, vm_geo[id].n_vcpu);

    /* vGIC 窗口(第十二站):镜像 QEMU virt 真布局,树里 intc 节点据此描述。
     * ESHINE_MAX_DEV=4 恰好满编:vuart/virtio/vgicd/vgicr。 */
    vm->devices[2].ipa  = 0x08000000UL;
    vm->devices[2].pa   = 0;
    vm->devices[2].size = 0x10000UL;
    vm->devices[2].attr = S2_ATTR_EMULATED;
    vm->devices[2].dev  = VM_DEV_VGICD;
    vm->devices[3].ipa  = 0x080A0000UL;
    vm->devices[3].pa   = 0;
    vm->devices[3].size = (vm_geo[id].n_vcpu > 1) ? 0x40000UL
                                    : 0x20000UL;
    /* 站19D:vm2 双帧;站二十泛化按几何表(每帧 RD+SGI 各 64KB,
     * 帧号=off>>17)——vm0 双核后次核同样要寻址自家重分发帧开
     * SGI/PPI 面(ipidemo rx=0 定谳:窗口单帧=次核使能写落空)。 */
    vm->devices[3].attr = S2_ATTR_EMULATED;
    vm->devices[3].dev  = VM_DEV_VGICR;
    vm->n_dev = 4;                     /* 四设备满编 */
    {
        /* 站19D:vCPU 阵列初始化——vcpu0 恒为引导帧;次核(vi>0)
         * 无帧停泊(wait_state=1 泵滤掉),待 guest 的 PSCI CPU_ON
         * 经 EL2 转发置帧(帧即真相,同 vcpu0 哲学)。站二十:槽位
         * vCPU 数改走几何表(vm0 双核/vm1 单核对照/vm2 双核),拆
         * 19D 的 id==2 硬编码——漏初始化的次核帧全零会被专泵当
         * 就绪帧入场(pc=0 取指崩,vm0.vcpu1 实测定谳)。 */
        u32 nvc = vm_geo[id].n_vcpu;
        u32 vi;

        for (vi = 0; vi < nvc; vi++) {
            struct vcpu *v = &vm->vcpus[vi];

            memset(v->gprs, 0, sizeof(v->gprs));
            v->idx = vi;
            v->exit_reason = VCPU_EXIT;
            /* 帧尾复位(第五-八站字段):DEAD->INIT 槽位复用时不许带
             * 出旧值(wait_state 残 1 则新客永不被泵,潜在雷实测前修) */
            v->vtimer_ctl = 0UL;
            v->vtimer_cval = 0UL;
            v->mdscr = 0UL;         /* 站53:debug ctrl 槽清零起步 */
            v->sp_el0 = 0UL;        /* 站十四:guest current 槽清零起步
                                      * (Linux primary 早期 init_cpu_task
                                      * 自会写入 init_task) */
            v->wait_reason = 0UL;
            v->wait_state = 0UL;
            /* 站28 vtimer 专站补遗(帧即真相同律的站16 漏网):SCTLR/
             * TTBR0/TCR/VBAR/CPACR/LR0/VMCR/FP 入帧后从未进"槽位复用
             * 不许带旧值"复位清单——DEAD->CREATE 复用时新客带着前世
             * SCTLR(M=1)入场:引导的 bss 清零抹掉正在翻译的页表,
             * 取指即 L2 翻译错;向量取指落同址=向量自陷风暴,泵
             * "跑到出"楔死宿主壳([79] 脸;-d int 230MB 风暴定谳,
             * ELR=FAR=VBAR 同址无限循环)。全清=与冷靴 bss 帧
             * 完全同构,引导自会按新几何建表、设 VBAR、配 ICV。 */
            v->sctlr = 0UL;         /* MMU 关起步 */
            v->ttbr0 = 0UL;
            v->tcr = 0UL;
            v->vbar = 0UL;           /* guest 首异常前自设(boot 契约) */
            v->cpacr = 0UL;          /* FPEN 关:首 FP 指令走 guest lazy 陷阱 */
            v->lr_vtimer = 0UL;      /* 无陈尸 vLR 可装 */
            v->vmcr = 0UL;           /* 虚拟面全关起步,guest 经 ICV 自配 */
            v->fpcr = 0UL;
            v->fpsr = 0UL;
            if (vi == 0) {
                v->pc = vm_geo[id].entry;      /* 帧即真相:新跑=初始化帧 */
                v->sp = 0x40200000UL;          /* guest 自会重设 */
                v->pstate = 0x3C5UL;           /* EL1h + DAIF 全掩 */
                v->gprs[0] = VM_DTB_IPA;       /* x0=DTB 指针(bootloader 义务;
                                                 * 裸机 guest 不读,Linux 入口协议) */
            } else {
                v->pc = 0UL;
                v->sp = 0UL;
                v->pstate = 0UL;
                v->wait_state = 1UL;           /* 无帧停泊:CPU_ON 置帧后归零 */
            }
        }
    }

    if (stage2_build(&vm->space, vm->vmid, vm->mem, vm->n_mem,
                     vm->devices, vm->n_dev) != 0) {
        vm_id_label(id);
        console_puts("stage2_build rejected config\n");
        return -1;
    }
    if (id == 2) {
        sched_poke_cpu1();          /* 站十八D:CPU1 泵即扫新客 */
        sched_poke_cpu2();          /* 站19D:CPU2 泵同醒(扫到的是无帧
                                     * 停泊的 vcpu1,睡回;CPU_ON 再醒) */
        sched_poke_cpu3();          /* 站二十:CPU3 同醒(vm0.vcpu1 专泵) */
    }
    vm_transition(vm, VM_CREATED);
    vm_id_label(id);
    console_puts("CREATED (ram ");
    console_puthex(vm->mem[0].pa);
    console_puts(", entry ");
    console_puthex(vm->vcpus[0].pc);
    console_puts(")\n");
    return 0;
}

/* 异步启动:置 RUNNING 立即回 shell,跑动交给调度泵(shell 等输入处)。 */
int vm_start(u32 id)
{
    struct eshine_vm *vm;
    bool resumed;

    if (id >= ESHINE_MAX_VMS) {
        console_puts("no such vm\n");
        return -1;
    }
    vm = &vm_table[id];
    resumed = (vm->state == VM_PAUSED);
    if (vm->state == VM_PAUSED || vm->state == VM_CREATED)
        vm_transition(vm, VM_RUNNING);       /* 断点续跑 / 新跑 */
    else {
        vm_id_label(id);
        console_puts("denied: vmcreate first\n");
        return -1;
    }
    if (!resumed) {
        /* VM 出生即清 stage-1(第十四站从 vcpu_run 收编至此:进场不许
         * 碰 guest 的 SCTLR_EL1,出生清一次防上一位 guest 的陈旧页表
         * 泄给新客;Linux 随后自会置 M 走两级翻译)。 */
        u64 sctlr;

        __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
        sctlr &= ~1UL;
        __asm__ volatile("msr sctlr_el1, %0" :: "r"(sctlr));
        vcpu_flush_all_tables();   /* 站19D:出生口同熄(同 vm_finish 理由) */
    }
    if (id == 2) {
        sched_poke_cpu1();          /* 站十八D:CPU1 泵即扫 */
        sched_poke_cpu2();          /* 站19D:CPU2 同醒(vcpu1 仍停泊则睡回) */
        sched_poke_cpu3();          /* 站二十:CPU3 同醒(vm0.vcpu1 专泵) */
    }
    vm_id_label(id);
    if (resumed)
        console_puts("resumed (breakpoint)\n");
    else
        console_puts("entering EL1 via stage-2 (non-identity map)\n");
    return 0;
}

/* 退场善后:yield 暂停/死亡,两者都回收 RX 所有权(guest 不再读线)。 */
/* 站十八D:VM 状态锁——kill(CPU0)与在途退场(CPU1 的 vm_finish)
 * 对同一 VM 的迁移都是"查后写",跨核非原子窗互踩即表外 panic。锁内
 * 重检 RUNNING 者独占入殓/让出;他侧见已入殓只收 owner。锁外打印。 */
static volatile unsigned long vm_state_lock;

static void vm_state_lock_take(void)
{
    for (;;) {
        unsigned long v;

        __asm__ volatile("ldaxr %0, [%1]" : "=r"(v) : "r"(&vm_state_lock));
        if (!v) {
            unsigned long ok;

            __asm__ volatile("stxr %w0, %w2, [%1]"
                             : "=&r"(ok)
                             : "r"(&vm_state_lock), "r"(1UL));
            if (!ok)
                return;
        }
        __asm__ volatile("wfe");
    }
}

static void vm_state_lock_drop(void)
{
    __asm__ volatile("stlr %0, [%1]" :: "r"(0UL), "r"(&vm_state_lock));
    __asm__ volatile("sev");
}

static void vm_finish(u32 id, u64 reason)
{
    struct eshine_vm *vm = &vm_table[id];
    bool paused = false, dead = false;

    vm->vu.flush_at = 0;                /* 站23C:即冲口同步熄武装——陈旧
                                           * 死线不得再对死台开冲(评审 c) */
    vuart_tx_flush(&vm->vu, id);        /* 死/停前冲尾:半行不吞(tag 定前缀) */
    vm_state_lock_take();
    if (vm->state != VM_RUNNING) {
        vm_state_lock_drop();
        /* 已入殓/已让出(他侧先手):只收 owner,不二次迁移 */
        if (sched_console_owner && vm_of_vcpu(sched_console_owner) == vm)
            sched_console_owner = NULL;   /* 站19D:owner 按 VM 收(vm2 双 vcpu) */
        return;
    }
    if (reason == VCPU_YIELD) {
        vm->state = VM_PAUSED;          /* 锁内自证迁移,表语义构造成立 */
        paused = true;
    } else {
        vm->state = VM_TEARDOWN;
        vm->state = VM_DEAD;
        dead = true;
    }
    vm_state_lock_drop();
    if (paused) {
        vm_id_label(id);
        console_puts("yielded -> PAUSED (vmrun to resume)\n");
    } else if (dead) {
        vm_id_label(id);
        if (reason == VCPU_FAULT)
            console_puts("faulted -> VM_DEAD\n");
        else
            console_puts("VM_DEAD\n");
    }
    sched_console_owner = NULL;   /* guest 不再读线,RX 归还宿主 */
    /* 站19D:生命周期口熄表记账(死/停后槽位可被 vmcreate 复用,同值
     * VTTBR 下重建页表;进场侧按值对比判不出,此口强制下一进场全量
     * flush,防旧 stage-2 缓存泄给新客) */
    vcpu_flush_all_tables();
}

struct vcpu *vm_vcpu_idx(u32 id, u32 vidx)   /* 站19D:(id,vidx) -> vcpu */
{
    if (id >= ESHINE_MAX_VMS || vidx >= ESHINE_MAX_VCPUS)
        return &vm_table[0].vcpus[0];  /* 不变量破坏兜底(PSCI 只合法达 2,1) */
    return &vm_table[id].vcpus[vidx];
}

struct vcpu *vm_vcpu(u32 id)
{
    return &vm_table[id].vcpus[0];
}

/* 站二十:槽位真实 vCPU 数(几何表)。泵/扫描的 vi 循环必须以此封界
 * ——vm_cpu_map 的单核行(如 vm1={0,0})第二格是填格,无帧实体,
 * 误当真帧入场=pc=0 取指崩(vm1 实测定谳)。 */
u32 vm_vcpu_count(u32 id)
{
    return vm_geo[id].n_vcpu;
}

/* vcpu -> 所属 VM(mmio_dispatch 用;2 槽全表扫,表外=内部不变量破坏) */
struct eshine_vm *vm_of_vcpu(struct vcpu *v)
{
    u32 id;

    for (id = 0; id < ESHINE_MAX_VMS; id++) {
        u32 vi;

        for (vi = 0; vi < ESHINE_MAX_VCPUS; vi++)
            if (&vm_table[id].vcpus[vi] == v)
                return &vm_table[id];
    }
    panic("vm_of_vcpu: vcpu not in vm_table");
}

/* 站28 vtimer 专站:本核 vtimer 归属(sched_vtimer_last)的可跑业主判定。
 * 27 在宿主语境起火的第四类判据——可跑客的活火落在片隙(sweep 只认
 * 泊者,inflight 只认 LR 在途,双不中被老代码当"无主"收尸=滴答永死,
 * trail ev15→ev8 终局定谳)。返回 NULL=非可跑业主(回落老语义);
 * 否则=业主帧指针,调用方注入后回写其 lr_vtimer 槽(泵下一圈进场
 * 装回,防站16"活不过切换"律以旧帧覆掉注入)。 */
struct vcpu *vm_vtimer_runnable_owner(u32 cpu)
{
    u64 own = sched_vtimer_last[cpu];
    struct eshine_vm *vm;
    struct vcpu *v;

    /* 哨兵碰撞修真(站28 定谳):(id<<4)|vi 对 vm0.vcpu0 打包值=0,
     * 与数组 bss 初值不可分——但泵入场(685)必先写,27 能起火则本核
     * 必已入过场,0=真业主 vm0.vcpu0 是唯一诚实读法;真守门员是
     * 下面的 VM_RUNNING 态判(未建客 state=INIT 必拒)。
     * 曾以 own==0 早退,首轮猎捕 ev8 后无 ev16 为证拒掉了唯一
     * 受害者,滴答照死——此行不复。 */
    if (cpu >= 4UL || (own >> 4) >= ESHINE_MAX_VMS)
        return NULL;
    vm = &vm_table[own >> 4];
    v = &vm->vcpus[own & 0xFUL];
    if (vm->state != VM_RUNNING || v->wait_state != 0UL)
        return NULL;
    return v;
}

/* 静态翻译(第九站建/第十站 virtio 数据面):IPA 落 mem[] 区且全尺寸含 → PA,
 * 否则 0(调用方按设备错处理,默认拒绝不杀 guest)。
 * EL2 恒等映射,PA 即宿主可直接访存的地址。 */
u64 vm_ipa_to_pa(struct eshine_vm *vm, u64 ipa, u64 size)
{
    u32 i;

    for (i = 0; i < vm->n_mem; i++) {
        const struct mem_region *r = &vm->mem[i];

        if (ipa >= r->ipa && ipa + size <= r->ipa + r->size)
            return r->pa + (ipa - r->ipa);
    }
    return 0UL;
}

/* 每拍到期扫描(第七站,多等待者):物理 CNTV 恒为"当前客"的,别家
 * 到期不响物理线——本扫描按保存的 ctx 表判定,WOKE 过期者(多家皆醒,
 * 先后由各自 cval 决定)。纯表逻辑:无 27 未响则无需关线降优先级
 * (vgic_wake_waiter 只属真正响过的物理 27 路径);拍频 = 1 tick,
 * 与泵粒度同阶——WOKE 的生效本就要等下一次泵轮到它,与即时判定
 * 等效(单核调度的本质粒度)。 */
/* 站20 [41] 取证探针:事件迹环(溢出覆写)。站20 时代"环头竞态
 * 可容忍"是 64 槽探针尺度的豁免——16K 取证主证据链不继承:丢更新
 * 会并槽吞事件、头先于字段会以旧槽冒充新条(幽灵取证,直接污染
 * ev5 类存档证据)。站25 收编:预约与字段写入入环专用自旋锁(同
 * console_lock 的 owner 递归式:同核 IRQ 语境复入只计数,跨核
 * ldaxr/stxr+WFE 互斥),头自增后行于字段——dump 快照头以下的
 * 槽保证完整,丢更新与幽灵双杀。锁成本:无争 ~20ns @~600 ev/s。 */
/* 站25C 仪器化:事件迹环 64→16384——64 条在 ~600 事件/秒的三核
 * 打拍负载下只余 0.1s 历史,"分钟级偶发死亡"的现场永远滚出环外
 * (站二十造仪器以来所有 dump 都只是即时尾巴,真凶现场从未入环)。
 * 16K 条 @~600 ev/s ≈ 27s 窗口;槽 24B ≈ 384KB 静态(QEMU 512MB,
 * 宿主低址区预算无虞)。vmtrail 带可选 N(默认 512):16K 行全量
 * 倾倒是串口级灾难。 */
#define TRAIL_N 16384
/* 站25 评审修真:C 臂重复唤醒上界——consume-fail 帧(guest 收到
 * 注入却 IRQ_NONE 不重装,站十九定谳的死法)在 C 臂下会每泵圈
 * 再醒=热活锁;计数同 cval 连醒 >2000 次即停醒+吐一行标记
 * (真绊线:唯一可达的坏类=它),证物交 verify 失败路径自动取证。 */
static u16 wake_retries[ESHINE_MAX_VMS * ESHINE_MAX_VCPUS];
static u64 wake_retries_cval[ESHINE_MAX_VMS * ESHINE_MAX_VCPUS];
struct trail_slot {
    u32 cpu;
    u32 ev;
    u64 a, b;
};
static struct trail_slot trail[TRAIL_N];
static volatile u32 trail_h;
static volatile unsigned long trail_lk;
static volatile long trail_owner = -1;
static volatile unsigned long trail_nest;

static void trail_lock(void)
{
    if (trail_owner == (long)cpu_id()) {
        trail_nest++;
        return;
    }
    for (;;) {
        unsigned long v;

        __asm__ volatile("ldaxr %0, [%1]" : "=r"(v) : "r"(&trail_lk));
        if (!v) {
            unsigned long ok;

            __asm__ volatile("stxr %w0, %w2, [%1]"
                             : "=&r"(ok)
                             : "r"(&trail_lk), "r"(1UL));
            if (!ok) {
                trail_owner = (long)cpu_id();
                trail_nest = 1;
                return;
            }
        }
        __asm__ volatile("wfe");
    }
}

static void trail_unlock(void)
{
    if (--trail_nest == 0UL) {
        trail_owner = -1;
        __asm__ volatile("stlr %0, [%1]" :: "r"(0UL), "r"(&trail_lk));
        __asm__ volatile("sev");
    }
}

void trail_add(u32 ev, u64 a, u64 b)
{
    struct trail_slot *s;

    trail_lock();
    s = &trail[trail_h % TRAIL_N];
    trail_h++;
    s->cpu = cpu_id();
    s->ev = ev;
    s->a = a;
    s->b = b;
    trail_unlock();
}

void vm_trail_dump(u32 n)
{
    u32 i, start, h;

    /* 站25 评审修真:头快照——活写者(~600 ev/s)使逐次重读
     * trail_h 的循环界可"追环不止"(全量 dump 在持续负载下
     * 永不收敛);快照一次,界死,慢串口下丢新不丢旧。 */
    h = trail_h;
    /* 站25 评审修真(b 腿):证据行整行持锁——旧版一行 ~11 个独立
     * 锁片段,vm1/vm2 靴流冲刷行可合法插入片段间,512 行现场撕碎
     * (站22A "判据行不许串字"同律的漏网裸奔者)。console 锁按
     * owner 递归,内层 puts/putdec/puthex 复入零成本;锁粒度=行
     * (非整场 dump——16K 行全量锁会把他客冲刷饿死成 [54] 类雷)。 */
    console_lock();
    console_puts("vmtrail: h=");
    util_putdec(h);
    console_puts("\n");
    console_unlock();
    if (h > TRAIL_N)
        i = h - TRAIL_N;                 /* 环已覆写:只余最近 TRAIL_N 条 */
    else
        i = 0;
    start = i;
    if (n && h - i > n)
        start = h - n;                   /* 站25:只要最近 n 条 */
    for (i = start; i < h; i++) {
        struct trail_slot *s = &trail[i % TRAIL_N];

        console_lock();
        console_puts("  #");
        util_putdec(i);
        console_puts(" cpu");
        util_putdec(s->cpu);
        console_puts(" ev");
        util_putdec(s->ev);
        console_puts(" a=");
        console_puthex(s->a);
        console_puts(" b=");
        console_puthex(s->b);
        console_puts("\n");
        console_unlock();
    }
}

u32 sched_vtimer_sweep(void)
{
    u64 now;
    u32 id, woke = 0;

    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(now));
    trail_add(12, now, woke);
    for (id = 0; id < ESHINE_MAX_VMS; id++) {
        u32 vi;

        if (vm_table[id].state != VM_RUNNING)
            continue;
        for (vi = 0; vi < vm_vcpu_count(id); vi++) {
            struct vcpu *v = &vm_table[id].vcpus[vi];

            if (vm_cpu_map[id][vi] != cpu_id())
                continue;            /* 站十九D:各核只扫自家(vcpu) 的帧 */
            if (v->wait_state != 1UL || v->wait_reason != VCPU_WAIT_TIMER)
                continue;
            if (!(v->vtimer_ctl & 1UL) || v->vtimer_cval > now)
                continue;
            v->wait_state = 2UL;          /* WOKE:泵轮到时注 LR */
            /* 站20 定谳:ctx 不许熄火(撤站十八A 的 ctl=0)。熄火后泵
             * 进场装回 ctl=0,被唤醒的 guest 收到注入的 v27 却读到
             * ISTATUS=0——轮询式 handler 判 IRQ_NONE 丢 tick,不重装
             * 下一拍,jiffies 冻死,整靴停在首个 msleep(实测两 vCPU
             * 双双 ws=1/reason=RX/ctl=0 停在 cpu_do_idle,站 14 注释
             * 早写下的死法)。装回武装过期值:进场线重挂,HW LR 重锁
             * pending,guest 认账(ISATATUS=1)重装即闭环;重挂窗
             * (进场→guest 重装,µs 级)的重复 27 由 el2_irq_guest
             * 27 分支幂等吸收,自限。 */
            woke++;
            trail_add(2, ((u64)id << 4) | vi, now);
        }
    }
    return woke;
}

/* 调度泵:轮转游标遍历,每 VM 一个量子;宿主权威在 shell(命令即原子段,
 * 泵只在 shell 等输入处被调)。前台模式(owner 非空)只跑 owner VM——
 * console 独占,双 guest 并行仅在后台模式(owner=NULL)。 */
/* 单 VM 跑一轮:就绪/等待过滤+WOKE 与设备注入+共享面装载+切出保存
 * +原因善后。CPU0 泵(前台过滤在泵侧)与 CPU1 专泵共用;前台过滤
 * 不在函数内——vm2 在 CPU1 无 RX 直读竞争(owner 环推送),免过滤。 */
static bool pump_run_vcpu(u32 id, u32 vi)
{
    struct eshine_vm *vm = &vm_table[id];
    struct vcpu *v = &vm->vcpus[vi];
    u64 reason;

    if (vm->state != VM_RUNNING)
        return false;                    /* 非就绪 */
    if (v->wait_state == 1UL) {
        /* 设备中断等待者(RX/virtio 完成等非 TIMER 源)仍有待取
         * 事件(注入与消费的时序缝隙的兜底网):再唤醒,本轮流到
         * 它时注入+消费;TIMER 睡眠与无事件者继续让位。
         * 第十二站:再唤醒同律过门控(guest 使能了才算"待取")。
         * 站20:SGI 搁浅判据前置且不限 reason——发送窗落在目标跑动
         * 期、目标先停泊后进场的竞态里,IPI 搁浅在软件挂起位,
         * 取走只发生在进场路径,此复查不看即死锁(主核 csd 自旋
         * 等 ack + 副核 tickless 空闲睡 = 整靴冻死,实测);TIMER
         * 停泊者早醒无害,进场照装 ctx 定时器,拍不丢。
         * 站25 收尾修真:时钟读后置——mrs CNTVCT 是 TCG 时钟查询,
         * 旧位每泵圈无条件付读,RX/never-armed 泊帧的自旋泵在四核
         * 世界里把它烧成模拟器热耗面(普查取证窗实测);后置到
         * armed TIMER 泊帧才读,语义不变(纯门控重排)。 */
        if (vgic_sgi_pending(&vm->vgic, vi) ||
            /* 站二十:vSPI33/35 归 vcpu0 面(19D 注入已 vi==0 定向,
             * DTB 中断路由的诚实镜像)——复查唤醒必须同面,否则共享
             * vuart 上的 RX 字节会误醒同 VM 的无帧次核([19] 入环即
             * pc=0 取指崩,vm0.vcpu1 实测定谳)。SGI 仍按帧。
             * 站24B 评审收口:owner 帧放开 TIMER 排除——输入⇒runnable
             * 不变量在三处唤醒位(推环/泊位复查/泵入口网)全对齐,
             * owner 的 TIMER 泊(真 Linux 恒 armed)亦可被环内待取
             * 事件再唤醒;非 owner 帧维持 TIMER 排除不动。 */
            (vi == 0 && (v->wait_reason != VCPU_WAIT_TIMER ||
                         sched_console_owner == v) &&
             ((vuart_rx_irq_ready(&vm->vu) &&
               vgic_spi_enabled(&vm->vgic, GIC_SPI_UART)) ||
              (virtio_irq_ready(vm) &&
               vgic_spi_enabled(&vm->vgic, GIC_SPI_VIRTIO)))))
            v->wait_state = 0UL;
        else if (v->wait_reason == VCPU_WAIT_TIMER &&
                 (v->vtimer_ctl & 1UL)) {
            u64 now_pump;

            __asm__ volatile("mrs %0, cntvct_el0" : "=r"(now_pump));
            if (v->vtimer_cval > now_pump)
                return false;              /* armed 未到点:续泊 */
            /* 站25C:泵扫级 vtimer 兜底臂(pending⇒runnable 不变量
             * 的 vtimer 面)。旧况:泊定 TIMER 帧的唤醒只押 26/27
             * IRQ 驱动的 sweep——介入运行者(他 VM 的片)进场装回
             * 自己的 ctx 即覆写本核物理线,泊者挂起的 27 蒸发,
             * 唤醒迟至 1Hz 26 扫描 = tick 迟到 ≤1s(µs 应得)。
             * 泵扫每圈直查 saved cval(µs 级,泊位帧才付此读),
             * 唤醒迟到 ≤1 泵圈;置 ws=2 同 sweep——WOKE 专属
             * vtimer 注入块(下)照走,guest 认账重装闭环。
             * 与 sweep 双醒幂等(ws!=1 即跳);不看裸 cval 过期
             * (ctl&1 同守,防 never-armed 客的陈 cval 热自旋
             * ——站二十四教训)。
             * 评审修真:热活锁上界——consume-fail 帧(guest 收注入
             * 却 IRQ_NONE 不重装,cval 原地不动)本臂会每泵圈再
             * 醒;同 cval 连醒 2000 次即判死循环,停醒+一行标记
             * (唯一可达坏类的绊线),cval 变即清零(健康路径
             * guest 每拍重装,cval 恒前进,计数恒清)。 */
            u16 slot = (u16)(id * ESHINE_MAX_VCPUS + vi);

            if (wake_retries_cval[slot] != v->vtimer_cval) {
                wake_retries_cval[slot] = v->vtimer_cval;
                wake_retries[slot] = 0;
            }
            ++wake_retries[slot];
            /* 站28 修真:绊线越过 2000 恒停醒(原语义),标记只印
             * 一次(旧代码每泵圈一印,冻结尸曾洪 9 万行淹串口) */
            if (wake_retries[slot] > 2000) {
                if (wake_retries[slot] == 2001) {
                    console_lock();
                    console_puts("CARM-STALL: vm=");
                    util_putdec(id);
                    console_puts(".");
                    util_putdec(vi);
                    console_puts(" cval frozen (consume-fail face)\n");
                    console_unlock();
                }
                return false;          /* 停醒:证物交 verify 自动取证 */
            }
            v->wait_state = 2UL;
        }
        else {
            return false;
        }
    }
    trail_add(3, ((u64)id << 4) | vi,
              (v->wait_state << 8) | v->wait_reason);
    sched_vtimer_last[cpu_id()] = (id << 4) | vi;   /* 站十八A/D+19D:
                                        * 物理 CNTV 归属随本核进场;
                                        * (vm,vcpu) 打包进 last */
    v->slice_left = SCHED_QUANTUM_TICKS;
    /* 站十六:GIC 虚拟面恢复必须在 WOKE 注入之前(注入覆写成新唤醒;
     * 放后面会吃掉刚注入的 v27,睡眠永不醒,实测)。LR0 是 per-pCPU
     * 单点:双 vtimer 客并跑,泵为 vm0 注入会覆写 vm2 未认领的 tick。 */
    __asm__ volatile("msr ich_lr0_el2, %0" :: "r"(v->lr_vtimer));
    __asm__ volatile("msr ich_vmcr_el2, %0" :: "r"(v->vmcr));
    sched_need_resched = 0;
    if (v->wait_state == 2UL) {        /* WOKE 专属 vtimer(27/扫描经
                                        * ctx 表定位;RX 走直通 runnable
                                        * 不经 WOKE——两源零串台) */
        if (v->wait_reason == VCPU_WAIT_TIMER) {
            /* 第十二站:注入过门控——guest 没使能 PPI 27 就不注入,
             * 也不代完成(它自己的配置自己负责;隔离面哲学) */
            if (vgic_ppi_enabled(&vm->vgic, GIC_PPI_VTIMER)) {
                vgic_inject_vtimer(vgic_ppi_prio(&vm->vgic,
                                                 GIC_PPI_VTIMER));/* per-pCPU 单 LR 只能泵挂 */
            }
            trail_add(5, v->vtimer_ctl, v->vtimer_cval);
        }
        v->wait_state = 0UL;
        v->wait_reason = 0UL;
    }
    /* 注入门控(第十二站):distributor 成为权威——guest 使能了
     * 对应 SPI 才注 LR。通道不变:vtimer 27(HW)/vuart 33(LR1)/
     * virtio 35(LR3)。 */
    if (vi < ESHINE_MAX_VCPUS) {
        /* 站十九D:本 vCPU 的 SGI 面挂起(帧号=vi,与 VMPIDR/TYPER 的
         * 亲和三角一致)→ 取走注 LR2;取走即清,与 uart 同律。
         * SGI 挂起未被使能时不取(guest 自己的配置自己负责)。 */
        u32 sintid;
        u8 sprio;

        if (vgic_sgi_take(&vm->vgic, vi, &sintid, &sprio)) {
            vgic_inject_sgi(sintid, sprio);
        }
    }
    if (vi == 0 && vuart_rx_irq_ready(&vm->vu) &&
        vgic_spi_enabled(&vm->vgic, GIC_SPI_UART)) {
        /* 第八站:UART RX 虚拟中断,LR1 通道泵注入(同纪律:防串台)。
         * 站19D:SPI(串口/块)归 vcpu0 面(DTB 中断路由的诚实镜像)。
         * RIS 在 DR 读时重评定:多字符爆发每注一发弹一个,续注排空 */
        vgic_inject_uart();
        trail_add(4, (u64)vm->vu.rx_h, vm->vu.rx_t);
    }
    if (vi == 0 && virtio_irq_ready(vm) &&
        vgic_spi_enabled(&vm->vgic, GIC_SPI_VIRTIO)) {
        /* 第十站:virtio 完成中断(LR3);站19D:归 vcpu0 面(同上) */
        vgic_inject_virtio();
    }
    /* vtimer ctx 切入对(第七站):cortex-a57(v8.0)无 CNTV 陷阱位,
     * guest 直写物理;泵以切换对齐——切入还本人 ctx,物理 CNTV
     * 恒为"当前客"的;切出即存(全出口含 YIELD/EXIT/FAULT/WAIT),
     * 别家错过由每拍扫描兜住。CNTVOFF 共享 0:双 guest 同墙钟。 */
    /* 站十六入口序:共享面先装,vtimer 最后装并直接进场——装载与
     * vcpu_run 之间零宿主代码,27 无从插队。共享面(全 EL 一份的
     * 物理面,异构双客互换必串,各客归各)=SIMD 帧 + VBAR/CPACR +
     * stage-1 控制面(SCTLR/TTBR0/TCR);GIC 虚拟面已在 WOKE 注入
     * 前装(见上)。 */
    /* 入口段同屏(站十六):装载->eret 之间的 27 若插队,已装载的
     * ctl 会被 wake_waiter 关线清零,guest 带着软件以为 armed、硬件
     * 实熄的定时器跑,msleep 永不到点,tickless 停摆(实测残余闪断
     * ~25% 即此窗)。eret 装 SPSR 自然解屏。 */
    __asm__ volatile("msr daifset, #2");
    /* 站十八D:VMPIDR——EL1 的 MPIDR 视角即 VMPIDR_EL2。站十九D 起
     * per-vCPU:0x80000000|idx——vm0/vm1 恒 cpu0(单核视角);vm2 的
     * vcpu1 虚拟化成 cpu1,与双帧 TYPER 亲和(帧 0=aff0,帧 1=aff1)
     * 及 Linux 重分发帧发现三面一致;宿主读 MPIDR_EL2 不受影响。 */
    __asm__ volatile("msr vmpidr_el2, %0" :: "r"(0x80000000UL | v->idx));
    fpsimd_load(v);
    __asm__ volatile("msr vbar_el1, %0" :: "r"(v->vbar));
    __asm__ volatile("msr cpacr_el1, %0" :: "r"(v->cpacr));
    __asm__ volatile("msr tcr_el1, %0" :: "r"(v->tcr));
    __asm__ volatile("msr ttbr0_el1, %0" :: "r"(v->ttbr0));
    __asm__ volatile("msr sctlr_el1, %0" :: "r"(v->sctlr));
    __asm__ volatile("msr cntv_cval_el0, %0" :: "r"(v->vtimer_cval));
    __asm__ volatile("msr cntv_ctl_el0, %0" :: "r"(v->vtimer_ctl));
    reason = vcpu_run(v, vm->space.vttbr);

    /* 站十六出口序:DAIF 先屏,vtimer 最先存。vcpu_exit 已解 IRQ,
     * 这段保存窗若插进 el2_irq_host 的 27(vgic_wake_waiter 写
     * cntv_ctl=0"关线"),armed 状态被存成 0 -> WAIT 归 RX -> 永久
     * 停摆。旧窗(仅 tick 检查)近乎为零;本站共享面保存把窗撑到
     * 微秒级,轮转宿主相高频期(陷阱量子 ~40ms 一片)命中率陡升
     * ——单跑 Linux 实测闪断,T1/T2 侥幸即此窗。整段屏蔽 DAIF。 */
    __asm__ volatile("msr daifset, #2");
    __asm__ volatile("mrs %0, cntv_ctl_el0" : "=r"(v->vtimer_ctl));
    __asm__ volatile("mrs %0, cntv_cval_el0" : "=r"(v->vtimer_cval));
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(v->sctlr));
    __asm__ volatile("mrs %0, ttbr0_el1" : "=r"(v->ttbr0));
    __asm__ volatile("mrs %0, tcr_el1" : "=r"(v->tcr));
    __asm__ volatile("mrs %0, vbar_el1" : "=r"(v->vbar));
    __asm__ volatile("mrs %0, cpacr_el1" : "=r"(v->cpacr));
    __asm__ volatile("mrs %0, ich_lr0_el2" : "=r"(v->lr_vtimer));
    __asm__ volatile("mrs %0, ich_vmcr_el2" : "=r"(v->vmcr));
    fpsimd_save(v);
    __asm__ volatile("msr daifclr, #2");
    cur_vcpu = NULL;                /* 宿主语境:shell 命令期 tick 不误
                                      * 计入 guest 量子(27 分支已独立查) */
    if (reason == VCPU_PREEMPT) {
        sched_preempts++;             /* 量子耗尽:出口已转写帧,下一 VM */
    } else if (reason == VCPU_WAIT) {
        v->pc += 4;                   /* WFI 不 retire:代完成,醒后自查 */
        v->wait_state = 1UL;
        /* 两源等待(第六站;第七站起判据用切出保存的本人 ctx——
         * 物理此刻可能是他人视角):cntv 武装 = TIMER(到期经 WOKE
         * 注 LR 醒),否则 RX(shell 输入入环直通唤醒)。RX 等待持
         * owner——输入即唤醒源,前台的意义所在;TIMER 等待才归还
         * (宿主睡期间要能敲命令,回收出口在 guest 的 host 命令)。 */
        v->wait_reason = (v->vtimer_ctl & 1UL) ? VCPU_WAIT_TIMER
                                               : VCPU_WAIT_RX;
        trail_add(1, v->wait_reason, v->vtimer_cval);
        /* 站24B:泊位落定复查(owner 面)——推环唤醒检查与停泊
         * 出口窗的竞态关窗:推环读到 wait_state=0(退出在途)不醒
         * →随后泊定→环内字节无主,唤醒源只剩客自家 vtimer([41]
         * 家族脸#5 的洞②)。评审三修:(a) Dekker 补障——wait_state
         * 写(上)与环读之间 dmb ish(推环侧对偶,见 shell.c),双
         * 侧立障后至少一侧必见对侧写;(b) 门控同泵入口复查网
         * (vuart_rx_irq_ready+vgic 使能)——不看使能面的裸
         * has_data 复查会造泵热自旋(客无 RXIM 可取时泊-醒死
         * 循环,µs 级烧核);过门即真有可取事件,自愈有界;
         * (c) ev16 专用事件码(a=(id<<4|vi),b=reason)——复用
         * ev1 会与站二十 [41] 探针契约(a=reason)冲突,新旧
         * 取证不可比。翻回 runnable,泵下一圈即取(同核序贯,
         * 无需 poke)。 */
        if (sched_console_owner == v) {
            __asm__ volatile("dmb ish");
            if (vuart_rx_irq_ready(&vm->vu) &&
                vgic_spi_enabled(&vm->vgic, GIC_SPI_UART)) {
                v->wait_state = 0UL;
                trail_add(16, ((u64)id << 4) | vi, v->wait_reason);
            }
        }
        /* 站十五撤自动归还:真 Linux 壳恒 250Hz armed,timer 睡
         * 即被收台,输入永远喂给宿主 shell(owner 形同虚设)。
         * owner 从此是显式接管,归还只走 vmconsole host / 逃生
         * 0x1d / VM 死亡三口(裸机客另有 host HVC#4 自报)。 */
        (void)0;
    } else {
        vm_finish(id, reason);        /* YIELD/EXIT/FAULT */
    }
    return true;
}

/* 站23C:全帧泊位判(武装与 fire 时刻共用同一静默判)——任一
 * vCPU 未泊位(含 runnable 未跑:下一片即续写)= 行或在途,
 * 不冲。单帧 VM 恒过(无同侪)。 */
static bool vm_all_parked(struct eshine_vm *vm)
{
    u32 vn = vm_vcpu_count(vm->vmid);
    u32 vi;

    for (vi = 0; vi < vn; vi++) {
        if (vm->vcpus[vi].wait_state != 1UL)
            return false;
    }
    return true;
}

bool sched_pump(void)
{
    static u32 rr_next;
    bool progressed = false;
    u32 n;

    /* 站23C:延迟冲刷扫描(泵入口)——泊位武装的 flush_at 到期
     * 即冲。评审修真二连:
     * (a) fire 时刻复验武装时刻的同一静默判(全帧泊位+RX 环空)
     *     ——陈旧武装(泊后自醒长跑、死线已过)在行在途时拦腰
     *     冲出半行 = [54] 双雷复发(新路径共享旧状态);不满足则
     *     保留武装,下一泵入口复扫,行 '\n' 收口后自然变 no-op。
     * (b) 判-清入 console_lock,与泊位武装(exceptions.c 同锁)
     *     互斥——无锁清零可吞并发武装,该行吸收窗失效。
     * 时间基面:CPU0 宿主壳忙自旋(shell_run 每迭代一圈本函数,
     * µs 级,boot 序即恒在,不倚 vm0 tick);长量子期间扫描停摆
     * ≤1 量子,(a) 复验下迟冲无害。先清后冲,他核同到双冲无
     * 害(emit 空缓冲早退)。仅查已建 VM(state 门挡表垃圾)。 */
    {
        u64 now;

        __asm__ volatile("mrs %0, cntpct_el0" : "=r"(now));
        for (n = 0; n < ESHINE_MAX_VMS; n++) {
            struct eshine_vm *fvm = &vm_table[n];

            if (fvm->state != VM_RUNNING)
                continue;
            if (fvm->vu.flush_at && (long long)(now - fvm->vu.flush_at) >= 0) {
                console_lock();
                if (vm_all_parked(fvm) && !vuart_rx_has_data(&fvm->vu)) {
                    fvm->vu.flush_at = 0;
                    console_unlock();
                    vuart_tx_flush(&fvm->vu, n);
                } else {
                    console_unlock();   /* 行或在途:留武装,复扫 */
                }
            }
        }
    }

    /* 站十八A:在途回业主——27 三分支置的标志,本圈以最后进场者为
     * 旋转起点(首圈即回业主消费自己的拍;重装帧 ctl=1+过期 cval,
     * 线仍挂,HW LR 进场重锁 pending,guest 消费即闭环)。业主已
     * 死/停则顺手熄线收尸;他核业主与本核无关(数组 per-CPU)。 */
    if (sched_vtimer_recall[cpu_id()]) {
        u32 last = (u32)sched_vtimer_last[cpu_id()];
        u32 lid = last >> 4, lvi = last & 0xF;

        sched_vtimer_recall[cpu_id()] = 0UL;
        if (vm_table[lid].state != VM_RUNNING ||
            vm_cpu_map[lid][lvi] != cpu_id())
            vgic_wake_waiter();
        else
            rr_next = lid;
    }
    for (n = 0; n < ESHINE_MAX_VMS; n++) {
        u32 id = (rr_next + n) % ESHINE_MAX_VMS;
        struct eshine_vm *vm = &vm_table[id];

        if (!vm_on_cpu(id, cpu_id()))
            continue;                     /* 站十九D:各核只跑自家 VM */
        if (sched_console_owner &&
            vm_of_vcpu(sched_console_owner) != vm)
            continue;                     /* 前台模式:console 按 VM 独占
                                          * (站19D:vm2 双 vCPU 同 VM 皆前台) */
        /* 站二十:拆 19D 硬编码 vi=0——按映射核逐帧泵(vm0.vcpu0 与
         * vm1.vcpu0 同在 CPU0;vm0.vcpu1 归 CPU3 专泵不进本循环)。 */
        {
            u32 vi;

            for (vi = 0; vi < vm_vcpu_count(id); vi++) {
                if (vm_cpu_map[id][vi] != cpu_id())
                    continue;
                if (pump_run_vcpu(id, vi)) {
                    rr_next = (id + 1) % ESHINE_MAX_VMS;
                    progressed = true;
                }
            }
        }
    }
    return progressed;
}

/* 站十九D:专泵泛化——CPU1 跑 vm2.vcpu0,CPU2 跑 vm2.vcpu1(各自
 * "跑到出";无量子无轮转,PPI26 不配);空转 WFE:本核 27(睡客
 * 醒)与 SEV 邮箱(CPU_ON 的 poke)皆可唤醒。noreturn:
 * secondary_main 直落于此。 */
void pump_cpu(u32 cpu)
{
    for (;;) {
        u32 id, vi;
        bool ran = false;

        for (id = 0; id < ESHINE_MAX_VMS; id++) {
            if (vm_table[id].state != VM_RUNNING)
                continue;
            for (vi = 0; vi < vm_vcpu_count(id); vi++) {
                if (vm_cpu_map[id][vi] != cpu)
                    continue;
                if (pump_run_vcpu(id, vi))
                    ran = true;
            }
        }
        if (!ran)
            __asm__ volatile("wfe");
    }
}

int vm_kill(u32 id)
{
    struct eshine_vm *vm;
    bool already;

    if (id >= ESHINE_MAX_VMS) {
        console_puts("no such vm\n");
        return -1;
    }
    vm = &vm_table[id];
    /* 站十四:RUNNING 也入列——宿主对僵尸客的绝对处决权。ARM 单核
     * 无 EL2 级不可屏蔽抢占(DAIF 关尽的死循环客把 PPI26 也挡掉,
     * Linux panic 后实测 shell 冻结),管理面处决是唯一解;无异步
     * 问题:shell 命令即原子段,执行时泵必不在跑,帧为静态槽位。
     * 死客(INIT/DEAD)拒绝:无事可做,防误报。 */
    if (vm->state != VM_CREATED && vm->state != VM_PAUSED &&
        vm->state != VM_RUNNING) {
        vm_id_label(id);
        console_puts("kill denied (dead or never created)\n");
        return -1;
    }
    if (sched_console_owner && vm_of_vcpu(sched_console_owner) == vm) {
        sched_console_owner = NULL;         /* 管理面接管输入路由(按 VM) */
        console_puts("console -> host\n");
    }
    vm_state_lock_take();
    already = !(vm->state == VM_CREATED || vm->state == VM_PAUSED ||
                vm->state == VM_RUNNING);
    if (!already) {
        vm->state = VM_TEARDOWN;   /* 站十八D:锁内自证迁移(kill 与
                                     * CPU1 在途退场赛跑,查后写互踩会
                                     * 表外 panic;谁先见 RUNNING 谁入殓) */
        vm->state = VM_DEAD;
    }
    vm_state_lock_drop();
    if (already) {
        vm_id_label(id);
        console_puts("kill denied (already closing)\n");
        return -1;
    }
    if (id == 2) {
        sched_poke_cpu1();          /* 站十八D:即扫即停 */
        sched_poke_cpu2();          /* 站19D:CPU2 同扫(其 vcpu1 一并收尸) */
        sched_poke_cpu3();          /* 站二十:CPU3 同醒(vm0.vcpu1 专泵) */
    }
    vm_id_label(id);
    console_puts("killed (host authority)\n");
    return 0;
}

/* 站53:per-VM mdscr 槽倾倒——跨 VM 隔离判据(宿主查槽:vm0 写 0x1234
 * 与 vm1 写 0x5678 并存=陷阱代访真身;物理寄存器容不下两个值)。 */
void vm_dbg_dump(void)
{
    u32 id, vi;

    for (id = 0; id < ESHINE_MAX_VMS; id++) {
        struct eshine_vm *vm = &vm_table[id];

        for (vi = 0; vi < ESHINE_MAX_VCPUS; vi++) {
            char c[3];

            c[0] = (char)('0' + id);
            c[1] = (char)('0' + vi);
            c[2] = '\0';
            console_puts("vm");
            console_putc(c[0]);
            console_puts(".v");
            console_putc(c[1]);
            console_puts(": mdscr=0x");
            console_puthex(vm->vcpus[vi].mdscr);
            console_puts("\n");
        }
    }
}

void vm_list(void)
{
    u32 i;

    console_puts("VM table:\n");
    for (i = 0; i < ESHINE_MAX_VMS; i++) {
        struct eshine_vm *vm = &vm_table[i];

        console_puts("  [");
        util_putdec(i);
        console_puts("] ");
        console_puts(vm_state_name(vm->state));
        if (vm->state != VM_INIT) {
            console_puts("  ram ");
            console_puthex(vm->mem[0].pa);
            console_puts("  entry ");
            console_puthex(vm->vcpus[0].pc);
        }
        console_putc('\n');
    }
}

/* 站20 [41] 取证面:RX 链两域现场的宿主半边。命令语境 = 宿主 shell
 * (EL2,LR 面直接可读);owner=guest 时进不来(0x1d 逃生后可用)。
 * 壳层另一半(rrx 环/任务态)在客内存,由门禁 debug 变体经 QMP 读。
 * 用途:token 单见类闪断,定位字节搁浅在 vuart 环(宿主域)还是
 * 已入 rrx(客域)。 */
void vm_rx_dump(void)
{
    static const char hex[] = "0123456789abcdef";
    u32 i;
    u64 lr0, lr1, lr2, lr3;

    __asm__ volatile("mrs %0, ich_lr0_el2" : "=r"(lr0));
    __asm__ volatile("mrs %0, ich_lr1_el2" : "=r"(lr1));
    __asm__ volatile("mrs %0, ich_lr2_el2" : "=r"(lr2));
    __asm__ volatile("mrs %0, ich_lr3_el2" : "=r"(lr3));
    console_puts("vmrx: owner=");
    if (sched_console_owner)
        console_puthex((u64)sched_console_owner);
    else
        console_puts("host");
    console_puts(" lr0=");
    console_puthex(lr0);
    console_puts(" lr1=");
    console_puthex(lr1);
    console_puts(" lr2=");
    console_puthex(lr2);
    console_puts(" lr3=");
    console_puthex(lr3);
    console_puts("\n");
    for (i = 0; i < ESHINE_MAX_VMS; i++) {
        struct eshine_vm *vm = &vm_table[i];
        u32 k = vm->vu.rx_t;

        if (vm->state == VM_INIT)
            continue;
        console_puts("  [");
        util_putdec(i);
        console_puts("] h=");
        util_putdec(vm->vu.rx_h);
        console_puts(" t=");
        util_putdec(vm->vu.rx_t);
        console_puts(" ris=");
        console_puthex(vm->vu.ris);
        console_puts(" imsc=");
        console_puthex(vm->vu.imsc);
        console_puts(" cr=");
        console_puthex(vm->vu.cr);
        console_puts(" rx:");
        while (k != vm->vu.rx_h) {
            u8 b = (u8)vm->vu.rx[k];

            console_putc(hex[(b >> 4) & 0xFu]);
            console_putc(hex[b & 0xFu]);
            console_putc(' ');
            k = (k + 1u) % VUART_RX_SIZE;
        }
        console_puts("\n");
    }
    {
        /* 站20 [41] 收口取证:CPU0 重分发帧 PPI 物理面(挂起/活动/使能/
         * 优先级——ICC_* 系统寄存器在 EL2(non-VHE) 读到的是虚面,
         * 物理真相只有 MMIO)。26=bit26,27=bit27。 */
        u64 sgi = 0x080A0000UL + 0x10000UL;
#define RD32(a) (*(volatile u32 *)(a))

        {
            u64 vc, vv, vs;

            __asm__ volatile("mrs %0, cntv_ctl_el0" : "=r"(vc));
            __asm__ volatile("mrs %0, cntv_cval_el0" : "=r"(vv));
            __asm__ volatile("mrs %0, cntvct_el0" : "=r"(vs));
            console_puts("  cntv: ctl=");
            console_puthex(vc);
            console_puts(" cval=");
            console_puthex(vv);
            console_puts(" now=");
            console_puthex(vs);
            console_puts(" hpctl=");
            __asm__ volatile("mrs %0, cnthp_ctl_el2" : "=r"(vc));
            console_puthex(vc);
            console_puts(" hpcval=");
            __asm__ volatile("mrs %0, cnthp_cval_el2" : "=r"(vv));
            console_puthex(vv);
            /* 站28 vtimer 专站:虚拟接口三面直读——ICH_HCR(En 总闸)、
             * ICH_VMCR(VENG1/VPMR=guest ICV 底座)。vLR 双 pending 而
             * guest 未掩却两拍不取,唯此可判:En=0/VENG1=0/VPMR=0
             * 皆"线在而面灭"。 */
            console_puts(" hcr=");
            __asm__ volatile("mrs %0, ich_hcr_el2" : "=r"(vv));
            console_puthex(vv);
            console_puts(" vmcr=");
            __asm__ volatile("mrs %0, ich_vmcr_el2" : "=r"(vv));
            console_puthex(vv);
            console_puts("\n");
        }
        console_puts("  gicr0: pend=");
        console_puthex(RD32(sgi + 0x200));
        console_puts(" act=");
        console_puthex(RD32(sgi + 0x300));
        console_puts(" en=");
        console_puthex(RD32(sgi + 0x100));
        console_puts(" p26=");
        console_puthex(RD32(sgi + 0x418));
        console_puts(" p27=");
        console_puthex(RD32(sgi + 0x41c));
        console_puts("\n");
    }
}

/* 演示对象独立于 vm_table;实体内嵌 vm_space 后体积 ~20KB,必须静态。
 * 每次演示前直置 INIT(演示自复位,非生命周期通道)。 */
void vm_state_demo(void)
{
    static struct eshine_vm demo;
    static const enum vm_state path[] = {
        VM_CREATED, VM_RUNNING, VM_PAUSED, VM_RUNNING, VM_TEARDOWN, VM_DEAD,
    };
    u32 i;

    demo.state = VM_INIT;
    console_puts("VM state machine (lifecycle first):\n");
    console_puts("  VM_INIT -> VM_CREATED -> VM_RUNNING <-> VM_PAUSED\n");
    console_puts("  VM_RUNNING/VM_PAUSED -> VM_TEARDOWN -> VM_DEAD\n");
    console_puts("  any other transition: panic (default-deny)\n");

    console_puts("demo walk:");
    for (i = 0; i < sizeof(path) / sizeof(path[0]); i++) {
        vm_transition(&demo, path[i]);
        console_puts(" ");
        console_puts(vm_state_name(demo.state));
    }
    console_puts("\n");
}
