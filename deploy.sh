#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Julian Chu
# Deploy the website/ directory from this branch to /var/www/eshine-os.
set -euo pipefail

SRC="$(cd "$(dirname "$0")/website" && pwd)"
DST="${1:-/var/www/eshine-os}"

mkdir -p "$DST"
if command -v rsync >/dev/null 2>&1; then
    rsync -a --delete --exclude='.well-known' "$SRC/" "$DST/"
else
    find "$DST" -mindepth 1 -maxdepth 1 ! -name '.well-known' -exec rm -rf {} +
    cp -a "$SRC/." "$DST/"
fi

find "$DST" -type d -exec chmod 755 {} +
find "$DST" -type f -exec chmod 644 {} +
echo "deployed $SRC -> $DST"
