#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Julian Chu
# 站三十四:构建面判据脚手架——bitbake eshine-rtos(sstate 快路径,
# 首建全量)+回显 DEPLOY 路径。verify [93] 以此取部署物起 QEMU:
# 验证的是产品(部署物),非构建系统自嗨。
#
# 退出面:0=成功;末行 DEPLOY=<dir>=部署目录(tmp 后缀随 TCLIBC
# 变,故 bitbake -e 问真值,不猜路径)。
set -e
REPO="$(cd "$(dirname "$0")/.." && pwd)"
Poky="${ESHINE_POKY_DIR:-$HOME/projects/poky}"
BUILD="${ESHINE_YOCTO_BUILD:-$HOME/projects/eshine-yocto-build}"

if [ ! -d "$Poky/bitbake" ] || [ ! -f "$BUILD/conf/local.conf" ]; then
    echo "setup first: $REPO/scripts/setup-yocto.sh" >&2
    exit 2
fi

# 站37 自愈定谳:归档=gitignore 工件,站34 后无人重生则 bitbake
# 恒吃陈旧归档——[99] 曾以站34 部署物混过三轮绿灯(陈旧归档也有
# hello)。每次构建前重生=门恒验当下提交树(源变即新哈希,
# sstate 自然失配重建)。
bash "$REPO/scripts/archive-source.sh" > /dev/null

cd "$BUILD"
# oe-init-build-env 需 bash 且打印帮助:静默化;环境就位即可
. "$Poky/oe-init-build-env" "$BUILD" > /dev/null 2>&1

bitbake eshine-rtos > /dev/null
DEPLOY="$(bitbake -e eshine-rtos | sed -n 's/^DEPLOYDIR="\(.*\)"/\1/p')"
if [ -z "$DEPLOY" ] || [ ! -f "$DEPLOY/rtos.bin" ]; then
    echo "deploy artifacts missing (DEPLOY='$DEPLOY')" >&2
    exit 3
fi
echo "DEPLOY=$DEPLOY"

# 站61:镜像聚合(终装)——eshine-image.bb:boot/ 四件+run.sh+
# manifest.json;MACHINE=目标机(启动参数单一事实源)。
MACHINE=eshine-qemu-a57-type1 bitbake eshine-image > /dev/null
IMAGE_DEPLOY="$(MACHINE=eshine-qemu-a57-type1 bitbake -e eshine-image \
    | sed -n 's/^DEPLOYDIR="\(.*\)"/\1/p')"
IMG="$IMAGE_DEPLOY/eshine-image"
if [ ! -f "$IMG/run.sh" ] || [ ! -f "$IMG/manifest.json" ] || \
   [ ! -f "$IMG/boot/eshine.elf" ]; then
    echo "image assembly missing (IMG='$IMG')" >&2
    exit 4
fi
echo "IMAGE=$IMG"
