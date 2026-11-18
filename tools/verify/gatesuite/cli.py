# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Julian Chu
"""Command-line interface for the fixture-based gate suite."""

from __future__ import annotations

import argparse
import json
import os
import sys

from . import artifacts, registry, selection as selection_mod
from .runner import RunOptions, run
from .session import install_signal_handlers
from .model import SUITES


def _parse_args(argv: list[str]) -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="fixture-based gate suite (independent QEMU fixtures)")
    p.add_argument("--suite", choices=SUITES, default="all",
                   help="smoke | green | heavy | all (default all)")
    p.add_argument("--gates", help="gate IDs, e.g. 1-18,20,27-29")
    p.add_argument("--fixture", action="append", default=None,
                   help="only gates on this fixture (repeatable)")
    p.add_argument("--stop-after", type=int, default=None,
                   help="legacy: select gates 1..N and mark PARTIAL-PASS")
    p.add_argument("--retries", type=int, default=1,
                   help="gate retries (passes on retry are reported FLAKY)")
    p.add_argument("--boot-retries", type=int, default=3,
                   help="QEMU zero-byte boot restarts (default 3)")
    p.add_argument("--jobs", type=int, default=1,
                   help="parallel workers (currently serial; reserved)")
    p.add_argument("--gate-cap", type=float, default=300.0,
                   help="per-gate wall-clock cap in seconds (default 300)")
    p.add_argument("--scenario-cap", type=float, default=1800.0,
                   help="per-scenario wall-clock cap in seconds (default 1800)")
    p.add_argument("--forensics", action="store_true",
                   help="print buffer tails on failures")
    p.add_argument("--keep-on-fail", action="store_true",
                   help="keep the failing QEMU alive for manual gdb")
    p.add_argument("--strict-artifacts", action="store_true",
                   help="missing artifact groups are BLOCKED (CI), not SKIP")
    p.add_argument("--list", action="store_true", help="list gates and exit")
    p.add_argument("--count", action="store_true",
                   help="print active/namespace/retired counts and exit")
    p.add_argument("--validate", action="store_true",
                   help="validate the registry and exit")
    p.add_argument("--json", metavar="PATH", default=None,
                   help="write the result table as JSON (use - for stdout)")
    p.add_argument("--shard", metavar="K/N", default=None,
                   help="run every N-th scenario/batch/gate group (K=1..N)")
    p.add_argument("--include-retired", action="store_true",
                   help="include retired gates in --gates/--list")
    return p.parse_args(argv)


def _list(include_retired: bool) -> int:
    gates = registry.collect()
    retired = registry.RETIRED
    quarantined = selection_mod.load_quarantine()
    print("namespace: 1..183 (new gates start at 184)")
    for gid, g in gates.items():
        if g.status == "retired" and not include_retired:
            continue
        fix = g.fixture or "static"
        if g.scenario:
            sc = registry.SCENARIOS.get(g.scenario)
            fix = "scenario:" + (sc.fixture if sc else "?")
        status = "quarantined" if gid in quarantined else g.status
        note = ("  # quarantined %s" % quarantined[gid].split()[0]
                if gid in quarantined else
                ("  # " + g.reason) if g.reason else "")
        print("[%d] %-11s %-18s %s%s"
              % (gid, status, fix, g.name, note))
    if include_retired:
        for gid, reason in sorted(retired.items()):
            print("[%d] retired     %s" % (gid, reason))
    return 0


def _count() -> int:
    gates = registry.collect()
    active = [g for g in gates.values() if g.status == "active"]
    green = selection_mod.select(suite="green").gates
    print("active=%d green=%d namespace=183 retired=%d total=%d"
          % (len(active), len(green), len(registry.RETIRED), len(gates)))
    return 0


def _validate() -> int:
    registry.collect()
    errors = registry.validate()
    if errors:
        for e in errors:
            print("invalid: " + e, file=sys.stderr)
        return 2
    print("registry: OK (%d active gates, %d retired, %d scenarios)"
          % (sum(1 for g in registry.GATE.values() if g.status == "active"),
             len(registry.RETIRED), len(registry.SCENARIOS)))
    return 0


def main(argv: list[str] | None = None) -> int:
    install_signal_handlers()
    args = _parse_args(sys.argv[1:] if argv is None else argv)
    if args.validate:
        return _validate()
    if args.count:
        return _count()
    if args.list:
        return _list(args.include_retired)

    if args.jobs > 1:
        print("note: --jobs > 1 is reserved; running serially", file=sys.stderr)

    shard = None
    if args.shard:
        try:
            k_s, n_s = args.shard.split("/", 1)
            shard = (int(k_s), int(n_s))
        except ValueError:
            print("bad --shard %r (want K/N)" % args.shard, file=sys.stderr)
            return 2
        if shard[1] < 1 or not (1 <= shard[0] <= shard[1]):
            print("bad --shard %r (want 1 <= K <= N)" % args.shard,
                  file=sys.stderr)
            return 2

    sel = selection_mod.select(
        suite=args.suite, gates=args.gates, fixtures=args.fixture,
        stop_after=args.stop_after,
        include_retired=args.include_retired)
    if sel.unknown:
        print("unknown gate IDs: %s" % ",".join(str(x) for x in sel.unknown),
              file=sys.stderr)
        return 2
    if not sel.gates:
        print("no gates selected", file=sys.stderr)
        return 2
    print("verify: suite=%s gates=%d partial=%s"
          % (sel.suite, len(sel.gates), sel.partial))
    env = artifacts.load_env()
    report = run(sel, env, RunOptions(
        retries=max(1, args.retries),
        boot_retries=max(1, args.boot_retries),
        shard=shard,
        forensics=args.forensics,
        keep_on_fail=args.keep_on_fail,
        strict_artifacts=args.strict_artifacts,
        jobs=max(1, args.jobs),
        gate_cap_s=args.gate_cap,
        scenario_cap_s=args.scenario_cap))
    if args.json:
        payload = {
            "suite": report.suite, "partial": report.partial,
            "started": report.started_utc,
            "duration_s": report.duration_s,
            "env": report.env,
            "counts": report.counts(),
            "red": [r.gate_id for r in report.red()],
            "results": [r.as_dict() for r in report.results],
        }
        text = json.dumps(payload, indent=2)
        if args.json == "-":
            print(text)
        else:
            os.makedirs(os.path.dirname(os.path.abspath(args.json)),
                        exist_ok=True)
            open(args.json, "w", encoding="utf-8").write(text + "\n")
    return report.exit_code(allow_skip=not args.strict_artifacts)