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
