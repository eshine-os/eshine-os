# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Julian Chu
"""Result lines and summary (streaming; one line per gate)."""

from __future__ import annotations

from .model import BLOCKED, FAIL, FLAKY, GateResult, RunReport


def print_result(r: GateResult) -> None:
    suffix = ""
    if r.attempts > 1:
        suffix += " attempts=%d" % r.attempts
    if r.scenario:
        suffix += " scenario=%s" % r.scenario
    print("  [%d/183] %s ... %s (%.1fs)%s"
          % (r.gate_id, r.name, r.status, r.duration_s, suffix), flush=True)
    if r.status in (FAIL, BLOCKED) and r.detail:
        first = r.detail.splitlines()[0] if r.detail else ""
        print("      %s%s" % (first[:300],
                              ("  log=" + r.log_path) if r.log_path else ""),
              flush=True)


def print_summary(report: RunReport) -> None:
    c = report.counts()
    red = ",".join(str(r.gate_id) for r in report.red())
    kind = "PARTIAL-PASS" if report.partial else (
        "PASS" if not report.red() else "FAIL")
    print("RESULT %s gates=%d pass=%d fail=%d skip=%d blocked=%d flaky=%d "
          "red=[%s] duration=%.1fs"
          % (kind, len(report.results),
             c.get("PASS", 0) + c.get(FLAKY, 0), c.get(FAIL, 0),
             c.get("SKIP", 0), c.get(BLOCKED, 0), c.get(FLAKY, 0), red,
             report.duration_s), flush=True)