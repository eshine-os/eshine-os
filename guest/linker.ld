/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
OUTPUT_ARCH(aarch64)
ENTRY(_start)

SECTIONS
{
    . = 0x40000000;                        /* guest IPA 链接地址(物理在 0x50000000) */
    .text : { KEEP(*(.text.boot)) *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    . = ALIGN(16);
    __bss_start = .;
    .bss : { *(.bss .bss.*) *(COMMON) }
    . = ALIGN(8);
    __bss_end = .;
    .stack (NOLOAD) : { . = ALIGN(16); . += 0x2000; __stack_top = .; }
    /DISCARD/ : { *(.note*) *(.comment) *(.eh_frame*) }
}
