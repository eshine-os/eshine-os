#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Julian Chu
# 站三十四:yocto 环境装配——poky 净克隆(scarthgap LTS)+仓外
# build 目录生成 bblayers/local conf(机器外部工件纪律,与
# eshine-linux 同位)+源归档。可复现:全新机器跑本脚本+
# `bitbake eshine-rtos` 即得部署三镜像。
set -e
REPO="$(cd "$(dirname "$0")/.." && pwd)"
. "$REPO/scripts/env.sh"
Poky="$ESHINE_POKY_DIR"
BUILD="$ESHINE_YOCTO_BUILD"
PROXY="${ESHINE_PROXY:-}"

# 1) poky(scarthgap LTS,shallow;直连不通时经 ESHINE_PROXY)
if [ ! -d "$Poky/bitbake" ]; then
    echo "== 克隆 poky(scarthgap)到 $Poky =="
    if ! git clone --depth 1 -b scarthgap \
        https://git.yoctoproject.org/poky.git "$Poky" 2>/dev/null; then
        if [ -z "$PROXY" ]; then
            echo "ERROR: poky clone failed; set ESHINE_PROXY to retry via proxy" >&2
            exit 1
        fi
        git -c http.proxy="$PROXY" clone --depth 1 -b scarthgap \
            https://git.yoctoproject.org/poky.git "$Poky"
    fi
fi

# 2) build 目录+conf(BBLAYERS 指本仓层)
mkdir -p "$BUILD/conf"
cat > "$BUILD/conf/bblayers.conf" <<EOF
LCONF_VERSION = "7"
BBPATH = "\${TOPDIR}"
BBFILES ?= ""
BBLAYERS ?= " \\
  $Poky/meta \\
  $REPO/meta-eshine \\
  "
BBLAYERS_CONF_VERSION = "2"
EOF
cat > "$BUILD/conf/local.conf" <<EOF
# 站61:目标机=eshine-qemu-a57-type1(meta-eshine machine conf;
# 镜像配方读其 ESHINE_QEMU_* 面生成 run.sh=启动参数单一事实源。
# 站34 时代 qemuarm64 位面占位退役)
MACHINE = "eshine-qemu-a57-type1"
DL_DIR = "$BUILD/downloads"
SSTATE_DIR = "$BUILD/sstate"
TMPDIR = "$BUILD/tmp"
BB_NUMBER_THREADS = "8"
EOF

# 3) 源归档(构建吃提交树)
"$REPO/scripts/archive-source.sh"

echo "== 就绪: cd $BUILD && source $Poky/oe-init-build-env && bitbake eshine-rtos =="
