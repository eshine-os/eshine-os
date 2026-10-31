/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
/* eshine-rtos:IPA 0x40000000(宿主 PA 0x50000000,VM0 槽)。
 * .stack 为引导任务(idle)栈;各任务栈为 rt_task.c 内静态阵列。 */
OUTPUT_ARCH(aarch64)
ENTRY(_start)

PHDRS
{
    text   PT_LOAD FLAGS(5);   /* R-X */
    rodata PT_LOAD FLAGS(4);   /* R-- */
    data   PT_LOAD FLAGS(6);   /* RW- */
    user   PT_LOAD FLAGS(7);   /* RWX(EL0 面:权限由站 28 stage-1 定,非 ELF) */
}

SECTIONS
{
    /* 站二十八:EL0 用户段@0x40200000——与两区式 stage-1 对齐
     * (L2 块 0=内核 EL1-only,块 1..7=用户 EL0+EL1)。壳整目标
     * 文件入用户段:代码/串/数据/栈全落用户窗,零逐函数标注。
     * 声明序=输入段分配序:先于下方内核全配通配符吃段,否则
     * .text 的 *(.text*) 先吞壳段(实测教训)。 */
    /* 站29:__user_start/__user_end=进程 1 的镜像 VMA 申报界
     * (rt_as.c eager 预映与 fork 拷贝循此;页对齐收口)。 */
    .user 0x40200000 : {
        __user_start = .;
        rtos/rt_shell.o(.text* .rodata* .data* .bss* .bss.* COMMON)
        . = ALIGN(4096);
        __user_end = .;
    } :user
    . = 0x40000000;                        /* guest IPA 链接地址(物理在 0x50000000) */
    .text : { KEEP(*(.text.boot)) *(.text .text.*) } :text
    .vectors ALIGN(0x800) : { KEEP(*(.vectors)) } :text
    .rodata : { *(.rodata .rodata.*) } :rodata
    .data : { *(.data .data.*) } :data
    . = ALIGN(16);
    __bss_start = .;
    .bss : { *(.bss .bss.*) *(COMMON) } :data
    . = ALIGN(8);
    __bss_end = .;
    .stack (NOLOAD) : { . = ALIGN(16); . += 0x2000; __stack_top = .; } :data
    .cpu1stack (NOLOAD) : { . = ALIGN(16); . += 0x1000; __cpu1_stack_top = .; } :data
    /* 站二十:次核引导栈一页(入口 asm 驻→首个 rt_sched 切走即换
     * idle1 帧栈;idle1 就地转正仍驻本页)。 */
    /DISCARD/ : { *(.note*) *(.comment) *(.eh_frame*) }
}
