#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Julian Chu
set -e
D="$(cd "$(dirname "$0")/.." && pwd)"
# 第十四站:Linux guest 内核(槽 2;Image 存在才装载,几何:PA 0x58080000)
IMG="${ESHINE_LINUX_IMAGE:-$HOME/projects/eshine-linux/linux-6.6/arch/arm64/boot/Image}"
EXTRA=""
if [ -f "$IMG" ]; then
  EXTRA="-device loader,file=$IMG,addr=0x58080000,force-raw=on"
fi

exec qemu-system-aarch64 \
  -M virt,virtualization=on,gic-version=3 \
  -cpu cortex-a57 -m 512 -nographic \
  -kernel "$D/eshine.elf" \
  -device loader,file="$D/guest1.bin",addr=0x50000000,force-raw=on \
  -device loader,file="$D/guest2.bin",addr=0x51000000,force-raw=on \
  $EXTRA \
  "$@"
