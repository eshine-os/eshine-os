#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Julian Chu
"""Fixture-based gate suite entry point.

Gates run in independent fixtures (a fresh QEMU per gate, or one scenario
fixture for inherently stateful families).  Gate IDs are stable; the
historical namespace is 1..183 and new gates start at 184.

Examples:
    python3 tools/verify/gate_suite.py --suite smoke
    python3 tools/verify/gate_suite.py --suite green --retries 2
    python3 tools/verify/gate_suite.py --gates 1-18,20 --jobs 1
    python3 tools/verify/gate_suite.py --list
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from gatesuite.cli import main  # noqa: E402

if __name__ == "__main__":
    sys.exit(main())