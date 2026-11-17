# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Julian Chu
"""Gate runner: fresh fixture per gate, scenario fixtures for families."""

from __future__ import annotations

import dataclasses
import datetime
import os
import time

from . import artifacts, fixtures, registry
from .model import (BLOCKED, FAIL, FLAKY, PASS, SKIP, Gate, GateContext,
                    GateResult, RunReport, ScenarioContext, ScenarioFailure)
from .report import print_result, print_summary
from .session import FixtureError, FloodError, QemuSession


@dataclasses.dataclass
class RunOptions:
    retries: int = 1
    boot_retries: int = 3
    forensics: bool = False
    keep_on_fail: bool = False
    strict_artifacts: bool = False
    jobs: int = 1
    verbose: bool = False


def _missing_artifacts(gate: Gate, env: artifacts.Env) -> list[str]:
    tags = set(gate.artifacts)
    fix_key = gate.fixture
    if fix_key is None and gate.scenario:
        sc = registry.SCENARIOS.get(gate.scenario)
        fix_key = sc.fixture if sc else None
    tags.update(fixtures.fixture_artifacts(fix_key))
    problems: list[str] = []
    for tag in sorted(tags):
        problems.extend(artifacts.artifact_problems(tag, env))
    return problems


def _session_paths(env: artifacts.Env, tag: str, attempt: int):
    base = "%s-%d-%d" % (tag, os.getpid(), attempt)
    return (os.path.join(env.run_dir, base + ".sock"),
            os.path.join(env.log_dir, base + ".log"))


def _run_fresh(gate: Gate, env: artifacts.Env,
               opts: RunOptions) -> GateResult:
    fixture = fixtures.get(gate.fixture)
    if fixture is None:
        raise RuntimeError("gate %d has unknown fixture %r"
                           % (gate.id, gate.fixture))
    last: Exception | None = None
    log = ""
    duration = 0.0
    # Floods are a boot defect, not a gate assertion: retry the whole
    # fixture.  Assertion failures retry only up to --retries.
    max_attempts = max(opts.retries, opts.boot_retries)
    for attempt in range(1, max_attempts + 1):
        sock, log = _session_paths(env, "g%d" % gate.id, attempt)
        session = QemuSession(fixture.qemu_args(env, sock), sock, log,
                              name="gate%d" % gate.id)
        t0 = time.time()
        try:
            session.start(env.banner, retries=opts.boot_retries)
            try:
                fixture.prepare(session, env)
            except FloodError:
                raise
            except Exception as e:
                raise FixtureError("fixture %s: %s"
                                   % (gate.fixture, str(e)[:1500])) from e
            ctx = GateContext(session, gate, gate.fixture)
            gate.fn(ctx)
            duration = time.time() - t0
            status = PASS if attempt == 1 else FLAKY
            session.close()
            return GateResult(gate.id, gate.name, gate.fixture, status,
                              duration, log_path=log, attempts=attempt)
        except (FloodError, FixtureError) as e:
            last = e
            duration = time.time() - t0
            if opts.forensics:
                print("  gate %d attempt %d fixture: %s"
                      % (gate.id, attempt, e))
            session.close()
            continue
        except Exception as e:
            last = e
            duration = time.time() - t0
            tail = session.tail(4000).decode(errors="replace")
            if opts.forensics:
                print("  gate %d attempt %d failed: %s\n--- output tail ---\n%s"
                      % (gate.id, attempt, e, tail))
            if attempt < opts.retries:
                session.close()
                continue
            session.close(keep=opts.keep_on_fail)
            return GateResult(gate.id, gate.name, gate.fixture, FAIL,
                              duration, detail=str(e)[:2000], log_path=log,
                              attempts=attempt)
    return GateResult(gate.id, gate.name, gate.fixture, FAIL,
                      duration, detail=str(last)[:2000], log_path=log,
                      attempts=max_attempts)


def _run_scenario(key: str, gates: list[Gate], env: artifacts.Env,
                  opts: RunOptions) -> list[GateResult]:
    sc = registry.SCENARIOS[key]
    fixture = fixtures.get(sc.fixture)
    if fixture is None:
        return [GateResult(g.id, g.name, sc.fixture, BLOCKED,
                           detail="unknown fixture %r" % sc.fixture)
                for g in gates]
    selected = {g.id for g in gates}
    rep = gates[0]
    last: Exception | None = None
    for attempt in range(1, max(1, opts.boot_retries) + 1):
        sock, log = _session_paths(env, "sc-%s-a%d" % (key, attempt), 1)
        session = QemuSession(fixture.qemu_args(env, sock), sock, log,
                              name="scenario-" + key)
        results: list[GateResult] = []
        t0 = time.time()
        try:
            session.start(env.banner, retries=opts.boot_retries)
            try:
                fixture.prepare(session, env)
            except FloodError:
                raise
            except Exception as e:
                raise FixtureError("fixture %s: %s"
                                   % (sc.fixture, str(e)[:1500])) from e
            ctx = ScenarioContext(session, rep, sc.fixture, selected, results)
            sc.fn(ctx)
            for r in results:
                r.attempts = attempt
            return results
        except (FloodError, FixtureError) as e:
            last = e
            if opts.forensics:
                print("  scenario %s attempt %d fixture: %s"
                      % (key, attempt, e))
            session.close()
            continue
        except ScenarioFailure as f:
            results.append(GateResult(
                f.gate_id, f.name, sc.fixture, FAIL,
                time.time() - t0, detail=str(f.cause)[:2000],
                log_path=log, scenario=key))
            done = {r.gate_id for r in results}
            for g in gates:
                if g.id not in done:
                    results.append(GateResult(
                        g.id, g.name, sc.fixture, SKIP, 0.0,
                        detail="blocked_by=%d" % f.gate_id, log_path=log,
                        scenario=key))
            return results
        except Exception as e:
            if opts.forensics:
                print("  scenario %s failed: %s\n--- output tail ---\n%s"
                      % (key, e, session.tail(4000).decode(errors="replace")))
            done = {r.gate_id for r in results}
            for g in gates:
                if g.id not in done:
                    results.append(GateResult(
                        g.id, g.name, sc.fixture, BLOCKED,
                        time.time() - t0,
                        detail="fixture: %s" % str(e)[:1500],
                        log_path=log, scenario=key))
            return results
        finally:
            session.close()
    return [GateResult(g.id, g.name, sc.fixture, BLOCKED,
                       detail="flood: %s" % str(last)[:1500],
                       scenario=key, attempts=max(1, opts.boot_retries))
            for g in gates]


def _run_static(gate: Gate, env: artifacts.Env,
                opts: RunOptions) -> GateResult:
    t0 = time.time()
    try:
        ctx = GateContext(None, gate, "static")
        gate.fn(ctx)
        return GateResult(gate.id, gate.name, "static", PASS,
                          time.time() - t0)
    except Exception as e:
        return GateResult(gate.id, gate.name, "static", FAIL,
                          time.time() - t0, detail=str(e)[:2000])


def run(selection, env: artifacts.Env | None = None,
        opts: RunOptions | None = None) -> RunReport:
    env = env or artifacts.load_env()
    opts = opts or RunOptions()
    started = datetime.datetime.now(datetime.timezone.utc)
    t0 = time.time()
    report = RunReport(suite=selection.suite, partial=selection.partial,
                       started_utc=started.strftime("%Y-%m-%dT%H:%M:%SZ"),
                       env={"version": env.version, "work": env.work,
                            "qemu": env.qemu})

    # Artifact gate: missing groups skip their gates, never the whole run.
    runnable: list[Gate] = []
    for gate in selection.gates:
        problems = _missing_artifacts(gate, env)
        if problems:
            status = BLOCKED if opts.strict_artifacts else SKIP
            report.results.append(GateResult(
                gate.id, gate.name, gate.fixture or "static", status,
                detail="; ".join(problems)))
        else:
            runnable.append(gate)

    # Group scenario gates; everything else runs on its own fixture.
    groups: list[tuple[int, str, object]] = []
    scen: dict[str, list[Gate]] = {}
    for gate in runnable:
        if gate.scenario:
            scen.setdefault(gate.scenario, []).append(gate)
        else:
            groups.append((gate.id, "gate", gate))
    for key, gates in scen.items():
        groups.append((min(g.id for g in gates), "scenario", (key, gates)))
    groups.sort(key=lambda x: x[0])

    for _, kind, payload in groups:
        if kind == "scenario":
            key, gates = payload
            rs = _run_scenario(key, gates, env, opts)
            report.results.extend(rs)
            for r in rs:
                if not r.streamed:
                    print_result(r)
        else:
            gate = payload
            if gate.fixture is None:
                r = _run_static(gate, env, opts)
            else:
                r = _run_fresh(gate, env, opts)
            report.results.append(r)
            print_result(r)

    report.results.sort(key=lambda r: r.gate_id)
    report.duration_s = time.time() - t0
    print_summary(report)
    return report