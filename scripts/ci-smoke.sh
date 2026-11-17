#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Julian Chu
#
# Minimal CI battery shared by GitHub Actions, Gitee Go, and local sanity
# runs: build -> static checks -> gates 1..18 (hypervisor boot + VM0 RTOS
# up). The full 183-gate suite remains the local/release gate
# (`make verify`, two consecutive full runs to close a station).
#
# Env:
#   CI_SMOKE_GATES   last gate to run (default 18)
#   CI_NO_BUILD=1    skip the build (caller already built, e.g. make ci-smoke)
#
# Gates above 18 cover guest services and are load-sensitive on shared CI
# runners (the first GitHub container run timed out at gate 19, the RX
# wake chain, with the guest alive and services running).
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO"

if [ "${CI_NO_BUILD:-0}" != 1 ]; then
    make -j"$(nproc)"
fi

# static checks: shell syntax + python byte-compile
bash -n scripts/*.sh tools/*.sh
python3 -m py_compile tools/verify/*.py tools/*.py scripts/*.py

# smoke tier: host boot + VM0 RTOS up
python3 tools/verify/verify_el2.py --stop-after="${CI_SMOKE_GATES:-18}"