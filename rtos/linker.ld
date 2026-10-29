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
}

SECTIONS
{
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
