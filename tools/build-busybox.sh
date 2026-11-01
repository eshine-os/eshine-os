#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Julian Chu
# 站四十:musl-static busybox(1.36.1)构建配方——vm0/vm2 双世界同字节。
#
# 用法:tools/build-busybox.sh           # 产物落 tools/ramfs/bin/busybox
#
# 机制(0 patch 目标):
# 1) 工具链=站 33 musl 安装(/tmp/musl-build/inst;不存在则先跑
#    tools/build-musl.sh)。CC=交叉 gcc+-isystem musl 头(站 33 同律:
#    musl 头压默认 sysroot,linux/ uapi 头仍由交叉 sysroot 供给——
#    musl 安装不含内核头)。
# 2) GPL-2.0 隔离照 vm2 先例(站 15):busybox 源与产物皆仓外/
#    gitignore,配方与判据入仓。
# 3) 链接窗基 0x40C00000(站 33 AT_PHDR 律:头入段。-z
#    noseparate-code 单 PT_LOAD+FILEHDR,同 hello 形;真内核/vm2
#    按 p_vaddr 落 0x40C00000 不冲突)。
# 4) 栈预算:图像 ~1MB + 栈@0x40D80000 + malloc 缺省落位(游标恒越
#    hint 段尾,rt_as.c 契约)= 2MB 窗内账。
# 5) applet 集=allnoconfig+精选表(defconfig 1.3MB 驻 rtos 内核像
#    0x40200000 两区式硬界下塞不下——两区式 stage-1 块 0=2MB=内核
#    像预算,ramfs+内核文本/数据+bss 合账;精选集 ~600KB 留余量)。
#    旗舰判据不损:echo/cat/ls/date/md5sum 皆在集内。
set -e
REPO="$(cd "$(dirname "$0")/.." && pwd)"
WORK="${BUSYBOX_BUILD_DIR:-/tmp/busybox-build}"
MUSL="${MUSL_BUILD_DIR:-/tmp/musl-build}"
CROSS=aarch64-linux-gnu-
VER=1.36.1
TARBALLS=("$HOME/projects/eshine-linux/busybox-${VER}.tar.bz2")
BB_URL="https://busybox.net/downloads/busybox-${VER}.tar.bz2"

if [ ! -x "$MUSL/inst/bin/musl-gcc" ] || [ ! -f "$MUSL/inst/lib/musl-gcc.specs" ]; then
    echo "== musl 安装缺失,先跑 tools/build-musl.sh =="
    "$REPO/tools/build-musl.sh"
fi
SPECS="$MUSL/inst/lib/musl-gcc.specs"
# musl specs 自带 -nostdinc(站 40 实测):默认 sysroot 整个被摘,
# busybox 要的 <linux/*.h> uapi 失供。外科修法:只拷交叉 sysroot
# 的 uapi 三目录作独立 -isystem(零 glibc 头混入,musl 头恒先)。
SYSROOT=/usr/aarch64-linux-gnu/include
mkdir -p "$WORK/uapi"
for d in linux asm asm-generic mtd scsi sound video rdma xen; do
    [ -d "$SYSROOT/$d" ] || continue
    rm -rf "$WORK/uapi/$d"
    cp -r "$SYSROOT/$d" "$WORK/uapi/$d"
done
CCFLAGS="$CROSS"gcc" -isystem $MUSL/inst/include -isystem $WORK/uapi -specs $SPECS"
LDFLAGS_WIN="-Wl,-z,noseparate-code -Wl,-Ttext-segment=0x40C00000"

mkdir -p "$WORK"
cd "$WORK"
if [ ! -d "busybox-${VER}" ]; then
    if [ ! -f "busybox-${VER}.tar.bz2" ]; then
        found=""
        for t in "${TARBALLS[@]}"; do
            [ -f "$t" ] && found="$t" && break
        done
        if [ -z "$found" ]; then
            echo "== 下载 busybox ${VER}(直连先试,退 代理)=="
            CURL="curl -fL --retry 3"
            curl -sI --max-time 8 "$BB_URL" 2>/dev/null | head -1 | \
                grep -q " 200" || CURL="$CURL -x $ESHINE_PROXY"
            $CURL -o "busybox-${VER}.tar.bz2" "$BB_URL"
        else
            cp "$found" "busybox-${VER}.tar.bz2"
        fi
    fi
    tar xf "busybox-${VER}.tar.bz2"
fi
cd "busybox-${VER}"

echo "== allnoconfig + STATIC + 精选 applet 表(站 40 精选集)=="
make ARCH=arm64 CROSS_COMPILE="$CROSS" allnoconfig >/dev/null
APPLETS="STATIC ECHO CAT LS DATE UNAME MD5SUM SLEEP TRUE FALSE PWD \
 WC HEAD TAIL TOUCH MKDIR RM CP MV LN STAT HEXDUMP DU DF \
 READLINK BASENAME DIRNAME ID WHOAMI TEST CLEAR SYNC \
 ASH FEATURE_EDITING FEATURE_EDITING_FANCY_PROMPT FEATURE_SH_STANDALONE \
 MOUNT UMOUNT"
sed -i "s/^# CONFIG_STATIC is not set/CONFIG_STATIC=y/" .config
for a in $APPLETS; do
    sed -i "s/^# CONFIG_$a is not set/CONFIG_$a=y/" .config
done
yes "" | # oldconfig 对新符号交互提问(无 tty 即悬空)——yes 喂默认
    yes '' | make ARCH=arm64 CROSS_COMPILE="$CROSS" oldconfig >/dev/null

echo "== 构建(CC 携 musl 头+specs;LDFLAGS 窗基 0x40C00000)=="
make ARCH=arm64 CROSS_COMPILE="$CROSS" CC="$CCFLAGS" \
     EXTRA_LDFLAGS="$LDFLAGS_WIN" -j"$(nproc)" busybox_unstripped 2>&1 | tail -5

cp busybox_unstripped busybox
${CROSS}strip busybox
aarch64-linux-gnu-readelf -lW busybox | grep -E "LOAD|Entry" | head -8
sz=$(stat -c %s busybox)
echo "size: $sz bytes"
if [ "$sz" -gt 1500000 ]; then
    echo "WARN: 图像 >1.5MB,窗内账吃紧(0x40C00000+1.5MB=0x40D68000,栈 0x40D80000)"
fi
mkdir -p "$REPO/tools/ramfs/bin"
cp busybox "$REPO/tools/ramfs/bin/busybox"
echo "== 产物: tools/ramfs/bin/busybox(GPL:勿入 git;vm0 ramfs+vm2 initramfs 共用字节)=="
