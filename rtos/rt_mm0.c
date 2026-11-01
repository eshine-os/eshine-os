/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#include "rtos.h"

/* 站二十八:两区式 stage-1 v0(机制参考参考实现四层裁两层:arch 页表
 * 引擎+core/mm 物理面合为静态阵列;VMA/ASID/缺页全留 29+)。
 *
 * VA=IPA 恒等(0x40000000-0x41000000 VM0 槽+三设备窗),与 stage-2
 * (IPA→PA 0x5xxxxxxx)零组合意外;EL2 的 vuart/vgic/virtio 陷阱走
 * stage-2 EMULATED 无页,stage-1 只须放行取指/访存而非截胡——
 * 三设备窗以 Device 属性入表,不挡 stage-2 故障。
 *
 * 布局(L1 块粒度 2MB,共 8 块):
 *   块 0     0x40000000-0x401FFFFF  内核区(AP=EL1 only,XN=0)
 *   块 1..7  0x40200000-0x40FFFFFF  用户区(AP=EL0+EL1 RW;DTB
 *                                    0x40F00000 居块 7,内核语境
 *                                    可读——EL1 对用户页有权)
 * 设备窗(L0[0]→L1_dev):
 *   0x08000000 块(vGICD 0x08000000+vGICR 0x080A0000 同块)
 *   0x09000000 块(vuart)/0x0A000000 块(virtio-mmio)——EL1-only
 *   +XN+PXN:用户态永无直访设备(参考实现纪律,I/O 走 syscall)。
 *
 * 开机序:boot 恒等区先活(建表本身在 MMU-off 语境)→置 MAIR/TCR/
 * TTBR0→SCTLR.M=1+ISB→恒等映射下取指连续,零过渡窗(对照站 14
 * SP bank 教训:先归一再切换)。两核各调一次 rt_mm0_init():表幂等
 * 填充,寄存器 per-CPU 各置;表内存只写一次由首核完成,次核跳过。
 *
 * 宿主零改动:站十六五组帧已含 SCTLR-TTBR0-TCR per-VM 存还,
 * vm0 开自家 stage-1 与 vm2 Linux 同律(TTBR0 存的是 IPA,L1 表址
 * 经 stage-2 嵌套翻译,QEMU 实支持)。 */

#define MM0_BLOCK (2UL << 20)
#define MM0_KERN_BASE 0x40000000UL
#define MM0_USER_BASE 0x40200000UL

/* 描述符属性(4KB granule,L1 块):
 *   [1:0]=b01 有效块;[4:2]=AttrIndx;[7:6]=AP[2:1];[9:8]=SH(11=inner);
 *   [10]=AF;[54]=UXN;[53]=PXN。
 * AP[2]=bit7 只读旗,AP[1]=bit6 EL0 可访(Linux PTE_RDONLY=PTE_AP2
 * 同律):00=EL1-only RW,01=EL0+EL1 RW,1x=只读。首启曾误以 11
 * 为"EL1-only RW"——实为全级只读:内核块取指活、栈写即爆,
 * -d int 见 Permission fault 级联(站 28 冒烟第二雷)。 */
#define MM0_ATTRIDX_NORMAL 0UL
#define MM0_ATTRIDX_DEVICE 1UL
#define MM0_DESC_VALID     (1UL << 0)
#define MM0_AF             (1UL << 10)
#define MM0_SH_INNER       (3UL << 8)
#define MM0_AP_BOTH        (1UL << 6)   /* 0b01:EL0+EL1 RW */
#define MM0_AP_EL1         (0UL)       /* 0b00:EL1-only RW */
#define MM0_UXN            (1UL << 54)
#define MM0_PXN            (1UL << 53)

#define MM0_DESC_NORMAL_BOTH(pa) \
    ((pa) | MM0_DESC_VALID | (MM0_ATTRIDX_NORMAL << 2) | \
     MM0_AP_BOTH | MM0_SH_INNER | MM0_AF)
#define MM0_DESC_NORMAL_EL1(pa) \
    ((pa) | MM0_DESC_VALID | (MM0_ATTRIDX_NORMAL << 2) | \
     MM0_AP_EL1 | MM0_SH_INNER | MM0_AF)
#define MM0_DESC_DEVICE_EL1(pa) \
    ((pa) | MM0_DESC_VALID | (MM0_ATTRIDX_DEVICE << 2) | \
     MM0_AP_EL1 | MM0_AF | MM0_UXN | MM0_PXN)
#define MM0_DESC_TABLE(pa) ((pa) | 0x3UL)   /* 有效+table */

/* 静态三表两级走(T0SZ=25=39 位 VA:首查 1GB 步长表,次查即
 * 2MB 块描述符——参考实现四级裁两级即此形)。
 * 首启故障根因(冒烟无声自陷,站 14 同族):曾按三级链搭表
 * (l0→l1→l2 块),而 39 位 VA 只容三级走且块描述符仅第二级
 * 合法——落末级即保留编码,MMU 开后首条取指 Instruction Abort
 * →guest_sync→rt_sync_fault 静默停车。索引算术亦随之修正:
 *   L0[VA>>30]:(0x08000000→[0] 设备群,0x40000000→[1] RAM)
 *   L1[(VA>>21)&511]:设备窗块 64/72/80;RAM 块 0..7 */
static u64 mm0_l0[512]    __attribute__((aligned(4096)));
u64 *const rt_mm0_l0 = mm0_l0;               /* 站29:内核 PGD(vec.S
                                              * BOOKKEEP 经此双间接装,
                                              * 任务 ttbr==0 时) */
u64 *rt_mm0_l1_dev;                          /* 站29:per-process L0[0] 共享(导出) */
static u64 mm0_l1_dev[512] __attribute__((aligned(4096)));
static u64 mm0_l1_ram[512] __attribute__((aligned(4096)));

/* 站29:内核 PGD 的 L1 半区重划(进程+地址空间接管用户窗):
 *   [0]   内核块 EL1-only(不变)
 *   [1]   .user 段 —— **置 0**:EL0 永不在内核 PGD 上执行,
 *         壳/子进程经各自 per-process 表访此段
 *   [2..5]页池 0x40400000-0x40C00000 EL1-only 恒等——"内核高半区"
 *         (fork 拷贝/mmap 零填后备,任何语境可写,用户不可见)
 *   [6]   mmap 窗 0x40C00000 —— 置 0(per-process 表管辖)
 *   [7]   DTB 块 EL1 只读——内核 fdt 读(站 28 曾 BOTH,窗口
 *         收编后收紧为 EL1-only,用户面经自身表另判) */
#define MM0_DESC_NORMAL_EL1_RO(pa) \
    ((pa) | MM0_DESC_VALID | (MM0_ATTRIDX_NORMAL << 2) | \
     ((1UL << 7) | MM0_AP_EL1) | MM0_SH_INNER | MM0_AF)

void rt_mm0_init(void)
{
    u32 i;
    static u32 built;

    if (!built) {
        built = 1;

        mm0_l0[0] = MM0_DESC_TABLE((u64)mm0_l1_dev);
        mm0_l0[1] = MM0_DESC_TABLE((u64)mm0_l1_ram);

        /* 设备窗三块(与 guest 头文件对表:VGICD/VGICR 同 2MB 块) */
        mm0_l1_dev[0x08000000UL >> 21] = MM0_DESC_DEVICE_EL1(0x08000000UL);
        mm0_l1_dev[0x09000000UL >> 21] = MM0_DESC_DEVICE_EL1(0x09000000UL);
        mm0_l1_dev[0x0A000000UL >> 21] = MM0_DESC_DEVICE_EL1(0x0A000000UL);

        /* 站47:跨 VM 服务窗(块 9 @0x41200000,IPA 恒等,EL1
         * only+UXN+PXN)——vm0 设备后端形:窗口是内核设备,EL0
         * 经 demo op(rt_xvm.c)代访;stage-2 侧窗在宿主 vm.c。 */
        mm0_l1_ram[9] = MM0_DESC_NORMAL_EL1(0x41200000UL) |
                        (1UL << 54) | (1UL << 53);

        /* RAM 八块(站29 半区重划,见上注) */
        for (i = 0; i < 8; i++) {
            u64 pa = MM0_KERN_BASE + (u64)i * MM0_BLOCK;

            if (i == 0)
                mm0_l1_ram[i] = MM0_DESC_NORMAL_EL1(pa);
            else if (i >= 2 && i <= 5)
                mm0_l1_ram[i] = MM0_DESC_NORMAL_EL1(pa);
            else if (i == 7)
                mm0_l1_ram[i] = MM0_DESC_NORMAL_EL1_RO(pa);
            else
                mm0_l1_ram[i] = 0;        /* [1]/[6]:per-process 表 */
        }
        rt_mm0_l1_dev = mm0_l1_dev;
    }

    /* per-CPU 寄存器(两核各置) */
    __asm__ volatile("msr mair_el1, %0" :: "r"(0x0044UL) : "memory");
    /* TCR_EL1:IPS=40bit[35:32]|TG0=4KB[15:14]=0|SH0=inner[13:12]
     * |ORGN0/IRGN0=0b00|T0SZ=25(39 位 VA) */
    __asm__ volatile("msr tcr_el1, %0"
                     :: "r"((0x2UL << 32) | (3UL << 12) | 25UL)
                     : "memory");
    __asm__ volatile("msr ttbr0_el1, %0" :: "r"((u64)mm0_l0) : "memory");
    __asm__ volatile("isb");

    {
        u64 v;

        __asm__ volatile("mrs %0, sctlr_el1" : "=r"(v));
        v |= (1UL << 0)     /* M */
           | (1UL << 2)     /* C */
           | (1UL << 12);   /* I */
        __asm__ volatile("msr sctlr_el1, %0" :: "r"(v) : "memory");
        __asm__ volatile("isb");
    }
}
