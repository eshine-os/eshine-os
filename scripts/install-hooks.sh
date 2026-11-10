#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Julian Chu
# Enable the repository's commit-msg hook (core.hooksPath is not cloned).
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
chmod +x "$ROOT/.githooks/commit-msg" "$ROOT/scripts/check-commit-msg.py"
git -C "$ROOT" config core.hooksPath .githooks
echo "hooks installed: core.hooksPath=.githooks"
echo "test: python3 scripts/check-commit-msg.py --file .git/COMMIT_EDITMSG"