#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Julian Chu
#
# Fixture-based gate suite entry for cloud CI (GitHub Actions / Gitee Go).
# Modes:
#   prepare   build the tree + external artifacts (Linux Image, initramfs,
#             musl busybox, cJSON/zlib); static checks
#   run       run the selected suite with strict artifact gating
#   all       prepare + run (default)
#
# Env:
#   CI_SUITE       smoke | green | heavy | all   (default green)
#   CI_RETRIES     gate retries                  (default 3)
#   CI_BOOT_RETRIES fixture boot retries          (default 6)
#   CI_SKIP_FETCH  1 = artifacts already restored from cache (default 0)
#   CI_SHARD       K/N = run every N-th gate group (default: whole suite)
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO"
MODE="${1:-all}"

if [ "$MODE" = "prepare" ] || [ "$MODE" = "all" ]; then
    # Order matters: the kernel embeds rtos/ramfs.cpio at link time.  The
    # external artifacts (busybox in tools/ramfs/bin, musl demos) must
    # exist BEFORE make packs the ramfs, otherwise every `run /bin/...`
    # gate fails with "run: open failed" against a busybox-less image.
    if [ "${CI_SKIP_FETCH:-0}" != "1" ]; then
        scripts/bootstrap.sh --fetch
    fi
    make -j"$(nproc)"

    # Hard artifact assertion: never run the suite against a ramfs that
    # lacks busybox.  A cache/order regression here is a silent fixture
    # defect that would fail ~9 gates with a misleading "open failed".
    if ! cpio -it --quiet < rtos/ramfs.cpio | grep -qx 'bin/busybox'; then
        echo "FATAL: rtos/ramfs.cpio has no bin/busybox;" >&2
        echo "       run tools/build-busybox.sh, then make, before the suite" >&2
        exit 1
    fi

    bash -n scripts/*.sh tools/*.sh
    python3 -m py_compile \
        tools/verify/*.py \
        tools/verify/gatesuite/*.py \
        tools/verify/gatesuite/gates/*.py \
        tools/verify/tests/*.py \
        tools/*.py scripts/*.py
fi

if [ "$MODE" = "run" ] || [ "$MODE" = "all" ]; then
    SHARD=()
    if [ -n "${CI_SHARD:-}" ]; then
        SHARD=(--shard "${CI_SHARD}")
    fi
    python3 tools/verify/gate_suite.py \
        --suite "${CI_SUITE:-green}" \
        "${SHARD[@]}" \
        --retries "${CI_RETRIES:-3}" \
        --boot-retries "${CI_BOOT_RETRIES:-6}" \
        --strict-artifacts \
        --json "${CI_RESULT_JSON:-$PWD/.work/logs/gates-summary.json}"
fi