/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
/* 第十一站:设备树 blob 载体。真身由 tools/dtb/eshine-vm.dts 编译
 * (QEMU dumpdtb 裁剪,来源与裁剪规则见该文件头);incbin 直入 .rodata,
 * vm_create 时整拷进 guest RAM 固定偏移(bootloader 同构语义)。
 * 符号声明见 include/dtb.h——这里只发 asm,不重复定义 C 变量。 */
asm(".section .rodata\n"
    ".global eshine_dtb\n"
    "eshine_dtb:\n"
    ".incbin \"tools/dtb/eshine-vm.dtb\"\n"
    ".global eshine_dtb_end\n"
    "eshine_dtb_end:\n"
    ".global eshine_linux_dtb\n"
    "eshine_linux_dtb:\n"
    ".incbin \"tools/dtb/eshine-linux.dtb\"\n"
    ".global eshine_linux_dtb_end\n"
    "eshine_linux_dtb_end:\n");
