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
#   CI_BOOT_RETRIES fixture boot retries          (default 4)
#   CI_SKIP_FETCH  1 = artifacts already restored from cache (default 0)
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO"
MODE="${1:-all}"

if [ "$MODE" = "prepare" ] || [ "$MODE" = "all" ]; then
    make -j"$(nproc)"
    if [ "${CI_SKIP_FETCH:-0}" != "1" ]; then
        scripts/bootstrap.sh --fetch
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
    python3 tools/verify/gate_suite.py \
        --suite "${CI_SUITE:-green}" \
        --retries "${CI_RETRIES:-3}" \
        --boot-retries "${CI_BOOT_RETRIES:-4}" \
        --strict-artifacts \
        --json "${CI_RESULT_JSON:-$PWD/.work/logs/gates-summary.json}"
fi