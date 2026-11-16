#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Julian Chu
# Reproducible build/verify in the pinned toolchain container.
set -e
REPO="$(cd "$(dirname "$0")/.." && pwd)"
. "$REPO/scripts/env.sh"

ENGINE="${ESHINE_CONTAINER_ENGINE:-docker}"
IMAGE="${ESHINE_CONTAINER_IMAGE:-eshine-build:$(cat "$REPO/VERSION")}"
cmd="${1:-help}"
shift || true

build() {
    "$ENGINE" build -t "$IMAGE" -f "$REPO/docker/Dockerfile.build" \
        ${ESHINE_PROXY:+--build-arg http_proxy="$ESHINE_PROXY" \
                        --build-arg https_proxy="$ESHINE_PROXY"} "$REPO"
}

run() {
    mkdir -p "$ESHINE_WORK/home" "$ESHINE_WORK/tmp"
    # docker run 默认不挂 stdin:make run 的 QEMU 控制台和 shell 都会
    # "能看不能敲"。调用方是 tty 时补 -i/-t;CI/脚本管道保持现状。
    local tty=()
    if [ -t 0 ]; then tty=(-i); fi
    if [ -t 0 ] && [ -t 1 ]; then tty=(-i -t); fi
    "$ENGINE" run --rm --network "${ESHINE_CONTAINER_NETWORK:-none}" \
        --user "$(id -u):$(id -g)" \
        ${tty[@]+"${tty[@]}"} \
        -v "$REPO:/src" -v "$ESHINE_WORK:/work" \
        -e ESHINE_WORK=/work -e HOME=/work/home -e TMPDIR=/work/tmp \
        -e ESHINE_PROXY="${ESHINE_PROXY:-}" \
        -w /src "$IMAGE" "$@"
}

case "$cmd" in
    build) build ;;
    make) run make "$@" ;;
    shell) run bash ;;
    run)
        # 交互跑 QEMU(make run):.work 从宿主挂载,镜像在容器内启动。
        # 容器默认 --network none:bootstrap 的网络面(下载 pinned 源)
        # 必须在宿主侧完成——先补齐工作根再进容器,容器内 make run 的
        # --check 即过、零网络需求(工作根本来就是挂载进去的)。
        if ! "$REPO/scripts/bootstrap.sh" --check >/dev/null 2>&1; then
            echo "== work root incomplete; bootstrap --fetch on the host (the container runs with --network none) ==" >&2
            "$REPO/scripts/bootstrap.sh" --fetch || exit 1
        fi
        run make run ;;
    verify)
        # The verifier's own preflight decides which artifacts this run
        # needs; --stop-after=36 must work on a clean work root.
        run make -B -j"$(nproc)"
        run python3 tools/verify/verify_el2.py "$@" ;;
    selftest)
        build
        run make -B -j"$(nproc)"
        run python3 tools/verify/verify_el2.py --stop-after=36 ;;
    help|*)
        cat <<EOF
usage: scripts/container.sh <command>
  build                 build the pinned toolchain image
  make [args]           run make inside the container
  run                   interactive QEMU: make run inside the container
  verify [--stop-after=N]  in-container build + verify (preflight-driven)
  selftest              build image, make -B, verify --stop-after=36
  shell                 interactive shell in the container
environment: ESHINE_CONTAINER_ENGINE (docker/podman), ESHINE_WORK,
  ESHINE_CONTAINER_IMAGE, ESHINE_CONTAINER_NETWORK (default none),
  ESHINE_PROXY
EOF
        ;;
esac