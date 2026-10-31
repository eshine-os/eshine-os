/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Julian Chu */

/* 站三十一:rootfs 镜像内嵌——cpio(newc)归档由构建期
 * tools/gen_ramfs.py 打包 tools/ramfs 树而来(Makefile 规则),
 * 落内核 .rodata(EL1 恒等域;用户窗不可见——文件内容只经
 * 三段消息由 ramfs 服务器送达,这正是 fd=通道三元组的本义)。 */
    .section .rodata
    .global rt_ramfs_start
rt_ramfs_start:
    .incbin "rtos/ramfs.cpio"
    .global rt_ramfs_end
rt_ramfs_end:
