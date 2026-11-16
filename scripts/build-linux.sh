#!/bin/bash
# SPDX-License-Identifier: Apache-2.0 */
# Copyright (c) 2026 Julian Chu
# 第十四站:Linux guest 内核构建配方(可复现)。内核是可从源构建的
# 工件,不入仓(全仓 Apache-2.0,Linux 内核 GPL-2.0,二进制入库=许可
# 污染)——本脚本把源码与产物放在 $ESHINE_WORK/linux(可覆盖
# ESHINE_LINUX_DIR)。verify 门禁先验 Image 存在,缺则指引回本脚本。
set -e
REPO="$(cd "$(dirname "$0")/.." && pwd)"
. "$REPO/scripts/env.sh"
VER=6.6
DIR="$ESHINE_LINUX_DIR"
URL="https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-${VER}.tar.xz"

mkdir -p "$DIR"
cd "$DIR"
if [ ! -f "linux-${VER}.tar.xz" ]; then
    # bootstrap --fetch 把归档放在 cache/downloads;先复用,再谈网络。
    for cand in "$ESHINE_DOWNLOAD_DIR/linux-${VER}.tar.xz" \
                "$ESHINE_LINUX_DIR/linux-${VER}.tar.xz"; do
        [ -f "$cand" ] || continue
        cp --update=none "$cand" "linux-${VER}.tar.xz" 2>/dev/null || \
            cp -n "$cand" "linux-${VER}.tar.xz"
        break
    done
fi
if [ ! -f "linux-${VER}.tar.xz" ]; then
    echo "== 下载 ${VER} =="
    if ! curl -fL --retry 3 -o "linux-${VER}.tar.xz" "$URL"; then
        rm -f "linux-${VER}.tar.xz"
        wget -q -T 120 -O "linux-${VER}.tar.xz" "$URL"
    fi
fi
LINUX_SHA=$(python3 -c "import json;print(json.load(open('$REPO/tools/artifacts.json'))['linux']['sha256'])")
echo "$LINUX_SHA  linux-${VER}.tar.xz" | sha256sum -c -
if [ ! -d "linux-${VER}" ]; then
    echo "== 解压 =="
    tar xf "linux-${VER}.tar.xz"
fi
cd "linux-${VER}"

echo "== defconfig + virt 内建 =="
make ARCH=arm64 CROSS_COMPILE="$CROSS" defconfig
# 门禁依赖的驱动全部内建(无根文件系统,不能 m):virtio-mmio 块设备/
# pl011(earlycon+console)/GICv3/arch timer。defconfig 基线通常已 y,
# scripts/config 幂等强保。
./scripts/config --set-val CONFIG_VIRTIO_MMIO y \
                 --set-val CONFIG_VIRTIO_BLK y \
                 --set-val CONFIG_SERIAL_AMBA_PL011 y \
                 --set-val CONFIG_SERIAL_AMBA_PL011_CONSOLE y \
                 --set-val CONFIG_SERIAL_EARLYCON y \
                 --set-val CONFIG_ARM_GIC_V3 y \
                 --set-val CONFIG_ARM_ARCH_TIMER y \
                 --set-val CONFIG_CMDLINE "" \
                 --set-val CONFIG_CMDLINE_FORCE n
# 消除构建期外依赖(defconfig 不需要的都无关紧要;幂等)
make ARCH=arm64 CROSS_COMPILE="$CROSS" olddefconfig

echo "== 构建 Image =="
make ARCH=arm64 CROSS_COMPILE="$CROSS" -j"$(nproc)" Image

IMG="arch/arm64/boot/Image"
ls -la "$IMG"
echo "== 完成: ${DIR}/linux-${VER}/arch/arm64/boot/Image =="
