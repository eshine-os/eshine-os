#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Julian Chu
# 站五十九:cJSON(1.7.18,上游 MIT)交叉构建配方——从零可复现。
# 产物:/tmp/cjson-build/libcjson.a(静态库,musl 窗口语境;demo 消费
# 者=tools/ramfs-jsontest.c,解析 tools/ramfs/etc/services.json=
# 治理面真清单——cjson 不进 RTOS 内核(内核自研最小 JSON,站 51 决策
# 记档),只做 EL0 生态件)。
# 坑记档:单文件库直编免 configure(cJSON.c 一发,无依赖)。
set -e
REPO="$(cd "$(dirname "$0")/.." && pwd)"
WORK="${CJSON_BUILD_DIR:-/tmp/cjson-build}"
CROSS=aarch64-linux-gnu-
MUSL="${MUSL_BUILD_DIR:-/tmp/musl-build}"
VER=1.7.18

mkdir -p "$WORK"
cd "$WORK"
if [ ! -d cJSON-${VER} ]; then
    tar xf "$REPO/3rd/cjson-${VER}.tar.gz"
fi
cd cJSON-${VER}

echo "== 编 libcjson.a(CC 携 musl 头;窗口语境同 busybox 配方)=="
${CROSS}gcc -O2 -c -I. \
    -isystem "$MUSL/inst/include" \
    -specs "$MUSL/inst/lib/musl-gcc.specs" \
    -o "$WORK/cJSON.o" cJSON.c
${CROSS}ar rcs "$WORK/libcjson.a" "$WORK/cJSON.o"
aarch64-linux-gnu-readelf -h "$WORK/cJSON.o" | grep -E "Machine|Type"
echo "== 产物: $WORK/libcjson.a =="
