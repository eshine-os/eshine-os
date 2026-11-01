#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Julian Chu
# 站三十三:musl(1.2.5,上游 MIT,零 patch)交叉构建配方——从零可复现。
# 产物:tools/ramfs/bin/hello(静态 ELF,单 PT_LOAD @0x40C00000=
# RTOS mmap 窗基;同一字节入 vm0 ramfs 与 vm2 busybox initramfs=
# "双 musl 世界"判据面)。
#
# 用法:tools/build-musl.sh          # 构建并落 tools/ramfs/bin/hello
#
# 坑记档:
# 1) musl configure --target=aarch64 派生 CROSS_COMPILE=aarch64-,而
#    Ubuntu 工具链真名 aarch64-linux-gnu-——AR/RANLIB 找不到
#    (aarch64-ar 不存在)。修:configure 后改 config.mak 的 AR/
#    RANLIB 为 aarch64-linux-gnu-ar/-ranlib 续建(本脚本内置)。
# 2) 自带链接脚本(build-musl-link.lds):. = 0x40C00000 落 RTOS 用户
#    mmap 窗(0x40C00000..0x40E00000),ld 并段出单 RWE PT_LOAD——
#    loader 最简态;__init_array 符号显式供(musl 弱引用)。
# 3) musl-gcc specs 无 -lc(靠驱动默认),-nostdlib 杀了默认库——
#    显式 -lc 不可少;静态 musl hello ≈33KB 文本=musl 小体积名不虚传。
set -e
REPO="$(cd "$(dirname "$0")/.." && pwd)"
WORK="${MUSL_BUILD_DIR:-/tmp/musl-build}"
CROSS=aarch64-linux-gnu-

mkdir -p "$WORK"
cd "$WORK"
if [ ! -d musl-1.2.5 ]; then
    tar xf "$REPO/3rd/musl-1.2.5.tar.gz"
fi
cd musl-1.2.5
if [ ! -f lib/libc.a ]; then
    CC=${CROSS}gcc ./configure --target=aarch64 --disable-shared \
        --prefix="$WORK/inst"
    sed -i "s|^AR = .*|AR = ${CROSS}ar|; s|^RANLIB = .*|RANLIB = ${CROSS}ranlib|" \
        config.mak
    make -j"$(nproc)"
    make install
fi

echo "== 链接 hello(窗口链接脚本 0x40C00000)=="
cd "$REPO"
${CROSS}gcc -static -O2 -fno-asynchronous-unwind-tables \
    -isystem "$WORK/inst/include" \
    -specs "$WORK/inst/lib/musl-gcc.specs" \
    -T tools/build-musl-link.lds -Wl,--no-warn-rwx-segments -Wl,-z,noseparate-code \
    -o tools/ramfs/bin/hello tools/ramfs-hello.c -lc
aarch64-linux-gnu-readelf -lW tools/ramfs/bin/hello | head -12
echo "== 产物: tools/ramfs/bin/hello =="

echo "== 链接 pt(站35 pthread demo,同窗口脚本)=="
${CROSS}gcc -static -O2 -fno-asynchronous-unwind-tables \
    -isystem "$WORK/inst/include" \
    -specs "$WORK/inst/lib/musl-gcc.specs" \
    -T tools/build-musl-link.lds -Wl,--no-warn-rwx-segments \
    -o tools/ramfs/bin/pt tools/ramfs-pt.c -lpthread -lc
echo "== 产物: tools/ramfs/bin/pt =="
aarch64-linux-gnu-readelf -lW tools/ramfs/bin/pt | grep -c LOAD
