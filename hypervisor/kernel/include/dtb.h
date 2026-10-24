/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */
#ifndef ESHINE_DTB_H
#define ESHINE_DTB_H

/* incbin 载体(dtb.c):FDT blob 连接期嵌入 .rodata,运行期只读。
 * 第十四站起 per-slot 两树:裸机树(16MB)+Linux 树(128MB+bootargs) */
extern const unsigned char eshine_dtb[];      /* 数组非指针:end - start 即尺寸 */
extern const unsigned char eshine_dtb_end[];
extern const unsigned char eshine_linux_dtb[];
extern const unsigned char eshine_linux_dtb_end[];

#endif /* ESHINE_DTB_H */
