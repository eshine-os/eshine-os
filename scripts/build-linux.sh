#!/bin/bash
# SPDX-License-Identifier: Apache-2.0 */
# Copyright (c) 2026 Julian Chu
# 第十四站:Linux guest 内核构建配方(可复现)。内核是可从源构建的
# 工件,不入仓(全仓 Apache-2.0,Linux 内核 GPL-2.0,二进制入库=许可
# 污染)——本脚本把源码与产物放在仓外 $LINUX_DIR(默认 ~/projects/
# eshine-linux)。verify 门禁先验 Image 存在,缺则指引回本脚本。
set -e
VER=6.6
DIR="${LINUX_DIR:-$HOME/projects/eshine-linux}"
URL="https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-${VER}.tar.xz"

mkdir -p "$DIR"
cd "$DIR"
if [ ! -f "linux-${VER}.tar.xz" ]; then
    echo "== 下载 ${VER} =="
    curl -fL --retry 3 -o "linux-${VER}.tar.xz" "$URL"
fi
if [ ! -d "linux-${VER}" ]; then
    echo "== 解压 =="
    tar xf "linux-${VER}.tar.xz"
fi
cd "linux-${VER}"

echo "== defconfig + virt 内建 =="
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- defconfig
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
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- olddefconfig

echo "== 构建 Image =="
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j"$(nproc)" Image

IMG="arch/arm64/boot/Image"
ls -la "$IMG"
echo "== 完成: ${DIR}/linux-${VER}/arch/arm64/boot/Image =="
