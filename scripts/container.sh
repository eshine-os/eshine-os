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
    "$ENGINE" run --rm --network "${ESHINE_CONTAINER_NETWORK:-none}" \
        --user "$(id -u):$(id -g)" \
        -v "$REPO:/src" -v "$ESHINE_WORK:/work" \
        -e ESHINE_WORK=/work -e HOME=/work/home -e TMPDIR=/work/tmp \
        -e ESHINE_PROXY="${ESHINE_PROXY:-}" \
        -w /src "$IMAGE" "$@"
}

case "$cmd" in
    build) build ;;
    make) run make "$@" ;;
    shell) run bash ;;
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
  verify [--stop-after=N]  in-container build + verify (preflight-driven)
  selftest              build image, make -B, verify --stop-after=36
  shell                 interactive shell in the container
environment: ESHINE_CONTAINER_ENGINE (docker/podman), ESHINE_WORK,
  ESHINE_CONTAINER_IMAGE, ESHINE_CONTAINER_NETWORK (default none),
  ESHINE_PROXY
EOF
        ;;
esac