/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
/* eshine-rtos 用户窗链接脚本(mmap 窗 0x40C00000..0x40E00000):
 * 静态 musl hello 落窗内,hint-mmap 直挂。
 *
 * 站33 雷记档:ELF 头必须入首 PT_LOAD(FILEHDR PHDRS 子句,
 * vmlinux 同律)——否则 ld 把 LOAD 落文件偏移 0x10000,真内核
 * 算 AT_PHDR=base+e_phoff 落进段内 .text 垃圾字节,musl init_libc
 * 按幻影 phdr 找 PT_TLS=SEGV(vm2 实抓;qemu-user 自合成 phdr
 * 活着=假阴性;vm0 活是因我们 loader 自带 phdr 副本)。
 * init_array 符号显式供(musl 弱引用,零长度也成立)。 */
OUTPUT_FORMAT(elf64-littleaarch64)
OUTPUT_ARCH(aarch64)
ENTRY(_start)

PHDRS
{
  text PT_LOAD FILEHDR PHDRS;
}

SECTIONS
{
  . = 0x40C10000;   /* 站33:FILEHDR 回填段首一页(头驻 0x40C00000=text-1 页,须≥窗基)*/
  .text   : { *(.text .text.*) } :text
  .rodata : { *(.rodata .rodata.*) *(.eh_frame .eh_frame.*) } :text
  .data : {
    *(.data .data.*)
    . = ALIGN(8);
    __init_array_start = .;
    KEEP(*(.init_array .init_array.*))
    __init_array_end = .;
  } :text
  .bss : { *(.bss .bss.*) *(COMMON) . = ALIGN(16); } :text
  /DISCARD/ : { *(.comment) *(.note*) }
}
