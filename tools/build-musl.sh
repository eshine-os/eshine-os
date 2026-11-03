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
    # --enable-wrapper=gcc(站63 定谳):wrapper 自动探测=host 编译
    # 一个 #include <stdlib.h> 的探针——纯净环境(容器无 libc6-dev)
    # 交叉编译器报错即探成 "none",TOOL_LIBS 清空,musl-gcc.specs
    # 从不生成而链接面 -specs 硬依赖;显式置 gcc 免探测(宿主 libc-dev
    # 在场时探测恒过=增量环境遮罩,容器才现形)。
    CC=${CROSS}gcc ./configure --target=aarch64 --disable-shared \
        --prefix="$WORK/inst" --enable-wrapper=gcc
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
LC_ALL=C aarch64-linux-gnu-readelf -lW tools/ramfs/bin/hello | head -12
echo "== 产物: tools/ramfs/bin/hello =="

echo "== 链接 pt(站35 pthread demo,同窗口脚本)=="
${CROSS}gcc -static -O2 -fno-asynchronous-unwind-tables \
    -isystem "$WORK/inst/include" \
    -specs "$WORK/inst/lib/musl-gcc.specs" \
    -T tools/build-musl-link.lds -Wl,--no-warn-rwx-segments \
    -o tools/ramfs/bin/pt tools/ramfs-pt.c -lpthread -lc
echo "== 产物: tools/ramfs/bin/pt =="
LC_ALL=C aarch64-linux-gnu-readelf -lW tools/ramfs/bin/pt | grep -c LOAD

echo "== 链接 initmark/beat(站40:inittab sysinit/respawn 活证件)=="
for prog in initmark beat; do
    ${CROSS}gcc -static -O2 -fno-asynchronous-unwind-tables \
        -isystem "$WORK/inst/include" \
        -specs "$WORK/inst/lib/musl-gcc.specs" \
        -T tools/build-musl-link.lds -Wl,--no-warn-rwx-segments -Wl,-z,noseparate-code \
        -o "tools/ramfs/bin/$prog" "tools/ramfs-$prog.c" -lc
done
echo "== 产物: tools/ramfs/bin/{initmark,beat} =="

echo "== 链接 sockdemo(站57:AF_UNIX 八门真号判据件)=="
${CROSS}gcc -static -O2 -fno-asynchronous-unwind-tables \
    -isystem "$WORK/inst/include" \
    -specs "$WORK/inst/lib/musl-gcc.specs" \
    -T tools/build-musl-link.lds -Wl,--no-warn-rwx-segments -Wl,-z,noseparate-code \
    -o tools/ramfs/bin/sockdemo tools/ramfs-sockdemo.c -lc
echo "== 产物: tools/ramfs/bin/sockdemo =="

echo "== 链接 jsontest/ztest(站59:生态件消费者;cJSON+zlib 静态链)=="
CJSON="$(cd "$(dirname "$0")" && pwd)/build-cjson.sh"
ZLIB="$(cd "$(dirname "$0")" && pwd)/build-zlib.sh"
bash "$CJSON" >/dev/null
bash "$ZLIB" >/dev/null
${CROSS}gcc -static -O2 -fno-asynchronous-unwind-tables \
    -isystem "$WORK/inst/include" \
    -I"${CJSON_BUILD_DIR:-/tmp/cjson-build}/cJSON-1.7.18" \
    -specs "$WORK/inst/lib/musl-gcc.specs" \
    -T tools/build-musl-link.lds -Wl,--no-warn-rwx-segments -Wl,-z,noseparate-code \
    -o tools/ramfs/bin/jsontest tools/ramfs-jsontest.c \
    "${CJSON_BUILD_DIR:-/tmp/cjson-build}/libcjson.a" -lc
${CROSS}gcc -static -O2 -fno-asynchronous-unwind-tables \
    -isystem "$WORK/inst/include" \
    -I"${ZLIB_BUILD_DIR:-/tmp/zlib-build}/zlib-1.3.1" \
    -specs "$WORK/inst/lib/musl-gcc.specs" \
    -T tools/build-musl-link.lds -Wl,--no-warn-rwx-segments -Wl,-z,noseparate-code \
    -o tools/ramfs/bin/ztest tools/ramfs-ztest.c \
    "${ZLIB_BUILD_DIR:-/tmp/zlib-build}/libz.a" -lc
echo "== 产物: tools/ramfs/bin/{jsontest,ztest} =="
