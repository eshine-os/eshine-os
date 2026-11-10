#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Julian Chu
# Refresh the generated block in docs/state/PROJECT-STATE.md.
set -e
REPO="$(cd "$(dirname "$0")/.." && pwd)"
STATE="$REPO/docs/state/PROJECT-STATE.md"
[ -f "$STATE" ] || { echo "missing $STATE" >&2; exit 1; }

HEAD=$(git -C "$REPO" rev-parse --short HEAD)
SUBJECT=$(git -C "$REPO" log -1 --format=%s)
VER=$(cat "$REPO/VERSION")
NOW=$(date -u +%Y-%m-%dT%H:%M:%SZ)
# Gate lines are "[N/183]"; other "N/M" text occurs in comments. Take the
# most frequent denominator rather than the largest number.
GATES=$(grep -oE '\[[0-9]+/[0-9]+\]' "$REPO/tools/verify/verify_el2.py" | \
        cut -d/ -f2 | tr -d ']' | sort | uniq -c | sort -nr | \
        head -1 | awk '{print $2}')

python3 - "$STATE" "$NOW" "$HEAD" "$SUBJECT" "$VER" "$GATES" <<'PY'
import sys
path, now, head, subject, ver, gates = sys.argv[1:]
text = open(path, encoding='utf-8').read()
begin = '<!-- state:begin -->'
end = '<!-- state:end -->'
a = text.index(begin) + len(begin)
b = text.index(end)
block = (
    '\n- Updated: %s\n'
    '- HEAD: %s (%s)\n'
    '- Version: %s\n'
    '- Gates: %s (tools/verify/verify_el2.py)\n'
    '- Dev tags: 26.08-1w .. 26.11-2w; release v26.10\n' % (
        now, head, subject, ver, gates))
open(path, 'w', encoding='utf-8').write(text[:a] + block + text[b:])
PY
echo "updated $STATE: $HEAD ($VER, $GATES gates)"