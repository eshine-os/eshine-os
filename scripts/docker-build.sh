#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Julian Chu
#
# 站六十三:CI 容器化判据——docker 钉工具链镜像,容器内重建全链
# (3rd 三件 + make demos + make -B)+ sbom-check 全绿 + 版本对账
# (容器实采 vs manifest.yaml pin 逐字相等)。
# 同一棵树,任何机器,同样的二进制:可复现构建的运输形态兑现。
#
# 退出面:0=全绿;10/11/12=版本对账红;20/21=构建红/不净;22=sbom 红;
# 2=docker 不可用。
# 用法:bash scripts/docker-build.sh
set -e
REPO="$(cd "$(dirname "$0")/.." && pwd)"
. "$REPO/scripts/env.sh"
TAG="eshine-build:$(cat "$REPO/VERSION")"

command -v docker > /dev/null || { echo "docker not installed" >&2; exit 2; }
docker info > /dev/null 2>&1 || { echo "docker daemon not reachable" >&2; exit 2; }

# manifest pin 提取(宿主侧;组件块 = "- name: X" 之后首个 version: 行)
pin_of() {
    awk -v n="  - name: $1" 'index($0, n) {grab=1} grab && /^    version: / {sub(/^    version: /, ""); print; exit}' \
        "$REPO/3rd/manifest.yaml"
}
GCC_PIN="$(pin_of gcc)"
BINUTILS_PIN="$(pin_of binutils)"
QEMU_PIN="$(pin_of qemu)"
[ -n "$GCC_PIN" ] && [ -n "$BINUTILS_PIN" ] && [ -n "$QEMU_PIN" ] || {
    echo "manifest.yaml pin extraction failed" >&2; exit 2; }

echo "== docker build: $TAG (ubuntu:24.04 + pinned cross toolchain) =="
docker build -q -t "$TAG" -f "$REPO/docker/Dockerfile.build" "$REPO" > /dev/null

# 容器内:版本对账 + 全链重建 + sbom。源树挂 /src,以宿主 uid/gid 降权
# 跑(产物落宿主树且 owner 即宿主用户,镜像层零仓字节);构建输出静默
# 化,失败 tail 定位(站 34 同律)。
docker run --rm -e GCC_PIN="$GCC_PIN" -e BINUTILS_PIN="$BINUTILS_PIN" \
    -e QEMU_PIN="$QEMU_PIN" \
    --user "$(id -u):$(id -g)" \
    -v "$REPO":/src -v "$ESHINE_WORK":/work -e ESHINE_WORK=/work \
    -w /src "$TAG" bash -ec '
    quiet() { local log="$1"; shift; "$@" > "$log" 2>&1 || {
        echo "== $1 failed, tail $log =="; tail -20 "$log"; exit 20; }; }

    echo "== toolchain audit: container vs 3rd/manifest.yaml pin =="
    # -dumpversion 在 gcc 7+ 只出主版本号("13");-dumpfullversion
    # 出全 pin 形("13.3.0")——对账用全版面。
    gccv=$(aarch64-linux-gnu-gcc -dumpfullversion)
    ldv=$(aarch64-linux-gnu-ld --version | sed -n "1s/.* \([0-9][0-9.]*\)$/\1/p")
    # qemu 版本行尾带发行版括注("(Debian 1:...)",非数字结尾)——
    # 锚 "version " 前缀提取首数字串。
    qv=$(qemu-system-aarch64 --version | sed -n "1s/^QEMU emulator version \([0-9.]*\).*/\1/p")
    [ "$gccv" = "$GCC_PIN" ] || { echo "gcc pin mismatch: $gccv != $GCC_PIN"; exit 10; }
    [ "$ldv" = "$BINUTILS_PIN" ] || { echo "binutils pin mismatch: $ldv != $BINUTILS_PIN"; exit 11; }
    [ "$qv" = "$QEMU_PIN" ] || { echo "qemu pin mismatch: $qv != $QEMU_PIN"; exit 12; }
    echo "pin ok: gcc $gccv / binutils $ldv / qemu $qv"

    echo "== rebuild from tree (3rd x3 + make demos + make -B) =="
    quiet /work/logs/musl.log bash tools/build-musl.sh
    quiet /work/logs/cjson.log bash tools/build-cjson.sh
    quiet /work/logs/zlib.log bash tools/build-zlib.sh
    quiet /work/logs/demos.log make demos
    quiet /work/logs/make.log make -B
    grep -qE "warning:|error:" /work/logs/make.log && {
        echo "build not clean (warnings/errors in make -B)"; exit 21; }

    python3 tools/sbom-check.py
'
echo "docker: pinned toolchain in container, tree rebuilt clean, sbom ALL GREEN"
