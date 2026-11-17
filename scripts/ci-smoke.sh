#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Julian Chu
#
# Local/container smoke: host boot + VM0 boot (gates 1..18), no external
# artifacts.  The full green suite is scripts/ci-gates.sh.
#
# Env:
#   CI_SMOKE_GATES  legacy compatibility: run gates 1..N as PARTIAL-PASS
#   CI_NO_BUILD=1   skip the build (caller already built)
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO"

if [ "${CI_NO_BUILD:-0}" != "1" ]; then
    make -j"$(nproc)"
fi

bash -n scripts/*.sh tools/*.sh
python3 -m py_compile \
    tools/verify/*.py \
    tools/verify/gatesuite/*.py \
    tools/verify/gatesuite/gates/*.py \
    tools/verify/tests/*.py \
    tools/*.py scripts/*.py

if [ -n "${CI_SMOKE_GATES:-}" ]; then
    python3 tools/verify/gate_suite.py --stop-after "${CI_SMOKE_GATES}"
else
    python3 tools/verify/gate_suite.py --suite smoke
fi