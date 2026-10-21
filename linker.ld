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

    /DISCARD/ : { *(.note*) *(.comment) *(.eh_frame*) }
}
