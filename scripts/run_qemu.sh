#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Julian Chu
set -e
D="$(cd "$(dirname "$0")/.." && pwd)"
exec qemu-system-aarch64 \
  -M virt,virtualization=on,gic-version=3 \
  -cpu cortex-a57 -m 512 -nographic \
  -kernel "$D/eshine.elf" \
  -device loader,file="$D/guest1.bin",addr=0x50000000,force-raw=on \
  -device loader,file="$D/guest2.bin",addr=0x51000000,force-raw=on \
  "$@"
