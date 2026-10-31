#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Julian Chu
# 站三十四:源归档——git archive HEAD 出提交树 tar(参考实现 do_compile
# "git archive 单仓"机制镜像)。构建吃提交树非工作树=可复现纪律;
# 归档落层 files/ 供 bitbake file:// 搜索面(gitignore 工件,层内
# 不入 git——v0 代价记档 spec §4)。
set -e
REPO="$(cd "$(dirname "$0")/.." && pwd)"
LAYER="$REPO/meta-eshine/recipes-eshine/eshine-rtos/files"
PV="$(sed -n 's/^PV *= *"\(.*\)".*/\1/p' "$REPO/meta-eshine/recipes-eshine/eshine-rtos/eshine-rtos.bb")"

mkdir -p "$LAYER"
git -C "$REPO" archive --format=tar.gz --prefix="eshine-os-${PV}/" \
    -o "$LAYER/eshine-os-${PV}.tar.gz" HEAD
echo "== 源归档: $LAYER/eshine-os-${PV}.tar.gz ($(git -C "$REPO" rev-parse --short HEAD))"
