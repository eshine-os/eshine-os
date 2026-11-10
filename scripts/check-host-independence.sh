#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Julian Chu
# Fail if build/code files depend on host paths; prove make resolves under
# an isolated ESHINE_WORK with hostile HOME/TMPDIR.
set -e
REPO="$(cd "$(dirname "$0")/.." && pwd)"
. "$REPO/scripts/env.sh"

FORBIDDEN='(/home/|\$HOME/|~/|/tmp/(musl-build|cjson-build|zlib-build|busybox-build|eshine-))'
mapfile -t FILES < <(
    { git -C "$REPO" ls-files
      git -C "$REPO" ls-files --others --exclude-standard
    } | grep -E '^(Makefile|scripts/|tools/|meta-eshine/|docker/|\.github/)' | \
    grep -vE '^scripts/check-host-independence\.sh$' | sort -u)

bad=0
for f in "${FILES[@]}"; do
    if grep -I -nE "$FORBIDDEN" "$REPO/$f" 2>/dev/null; then
        echo "HOST DEP: $f" >&2
        bad=1
    fi
done
if [ "$bad" != 0 ]; then
    echo "host-independence: FAILED (see above)" >&2
    exit 1
fi

tmp=$(mktemp -d "${ESHINE_TMP_DIR}/iso-XXXXXX")
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/work" "$tmp/home" "$tmp/tmpdir"
if ! env -i PATH="$PATH" HOME="$tmp/home" TMPDIR="$tmp/tmpdir" \
        ESHINE_WORK="$tmp/work" make -C "$REPO" -n >/dev/null 2>"$tmp/err"; then
    echo "host-independence: make -n failed under isolated env" >&2
    cat "$tmp/err" >&2
    exit 1
fi
echo "host-independence: OK (static scan + isolated make -n)"