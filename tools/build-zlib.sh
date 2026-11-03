#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Julian Chu
# 站五十九:zlib(1.3.1,上游 Zlib 许可)交叉构建配方——从零可复现。
# 产物:/tmp/zlib-build/libz.a(静态库,demo 消费者=tools/ramfs-ztest.c
# =slog 压缩落盘:deflate /var/log/slog→/tmp/slog.z+读回 inflate 校验)。
# 坑记档:免 configure 直编核心源文件表(make libz.a 的内部清单,交叉
# 语境下最稳——CHOST configure 对 musl specs 语境不友好)。
set -e
REPO="$(cd "$(dirname "$0")/.." && pwd)"
WORK="${ZLIB_BUILD_DIR:-/tmp/zlib-build}"
CROSS=aarch64-linux-gnu-
MUSL="${MUSL_BUILD_DIR:-/tmp/musl-build}"
VER=1.3.1

mkdir -p "$WORK"
cd "$WORK"
if [ ! -d zlib-${VER} ]; then
    tar xf "$REPO/3rd/zlib-${VER}.tar.gz"
fi
cd zlib-${VER}

echo "== 编 libz.a(核心源直编;CC 携 musl 头)=="
SRCS="adler32.c crc32.c deflate.c infback.c inffast.c inflate.c inftrees.c \
 trees.c zutil.c compress.c uncompr.c"
OBJS=""
for s in $SRCS; do
    ${CROSS}gcc -O2 -c -I. \
        -isystem "$MUSL/inst/include" \
        -specs "$MUSL/inst/lib/musl-gcc.specs" \
        -o "$WORK/${s%.c}.o" "$s"
    OBJS="$OBJS $WORK/${s%.c}.o"
done
${CROSS}ar rcs "$WORK/libz.a" $OBJS
echo "== 产物: $WORK/libz.a =="
