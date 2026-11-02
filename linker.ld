/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
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
    . = 0x40200000;

    .text : {
        KEEP(*(.text.boot))
        *(.text .text.*)
    } :text

    .vectors ALIGN(2048) : {
        KEEP(*(.vectors))
    } :text

    .rodata : { *(.rodata .rodata.*) } :rodata
    .data    : { *(.data .data.*) } :data

    /* 站49:eshine_log 模块注册段(linker section 聚合,GNU ld 自动
     * 给 __start_/__stop_;level 运行时可改故入 data 段) */
    .eshine_log_mods : {
        KEEP(*(.eshine_log_mods))
    } :data

    . = ALIGN(16);
    __bss_start = .;
    .bss : { *(.bss .bss.*) *(COMMON) } :data
    . = ALIGN(8);
    __bss_end = .;

    .stack (NOLOAD) : {
        . = ALIGN(16);
        . += 0x4000;                /* 16 KB EL2 stack */
        __stack_top = .;
    } :data

    .secstack (NOLOAD) : {
        . = ALIGN(16);
        . += 0x3000;                /* 站十八D 4KB;站十九D 两页;站二十 三页:
                                     * CPU1 驻 top,CPU2 驻 top-0x1000,
                                     * CPU3(vm0.vcpu1 专泵)驻 top-0x2000 */
        secondary_stack_top = .;
    } :data

    /DISCARD/ : { *(.note*) *(.comment) *(.eh_frame*) }
}
