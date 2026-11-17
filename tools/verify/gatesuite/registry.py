# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Julian Chu
"""Gate and scenario registry.

IDs are stable documentation anchors.  The historical namespace is
1..183; retired gates leave gaps and new gates start at 184.
"""

from __future__ import annotations

import dataclasses
import sys
from typing import Callable

from .model import Gate, GateContext, ScenarioContext

GATE: dict[int, Gate] = {}
RETIRED: dict[int, str] = {}
SCENARIO_IDS: dict[str, tuple[int, ...]] = {}


@dataclasses.dataclass
class ScenarioDef:
    key: str
    fixture: str
    gate_ids: tuple[int, ...]
    fn: Callable[[ScenarioContext], None]


SCENARIOS: dict[str, ScenarioDef] = {}


@dataclasses.dataclass(frozen=True)
class Step:
    """A declarative command/expect step (smoke.py schema, extended)."""

    send: str | None
    expect: str
    fail_if: str | None = None
    timeout_s: float = 60.0
    guest: bool = False


def _steps_fn(steps: tuple[Step, ...]):
    def run(ctx: GateContext) -> None:
        s = ctx.s
        epoch = 0
        for st in steps:
            pos = ctx.mark()
            if st.send is not None:
                epoch = pos
                line = st.send if st.send.endswith("\n") else st.send + "\n"
                if st.guest:
                    s.send_guest(line)
                else:
                    s.send(line)
            search = pos if st.send is not None else epoch
            s.expect(st.expect, st.timeout_s, start=search)
            if st.fail_if is not None:
                chunk = s.buf[search:]
                if st.fail_if.encode() in chunk:
                    raise AssertionError(
                        "forbidden needle %r in output" % st.fail_if)
    return run


def gate(gate_id: int, name: str, *,
         fixture: str | None = "host",
         suites: tuple[str, ...] = ("green",),
         artifacts: tuple[str, ...] = ("elf",),
         timeout_s: float = 120.0,
         timing: bool = False,
         scenario: str | None = None,
         status: str = "active",
         reason: str = "",
         steps: tuple[Step, ...] | None = None,
         fn: Callable[[GateContext], None] | None = None):
    """Register a gate.  Use as a decorator or with steps=/fn=."""
    def register(callable_fn):
        if gate_id in GATE:
            raise ValueError("duplicate gate id %d" % gate_id)
        if gate_id in RETIRED:
            raise ValueError("gate %d is retired" % gate_id)
        if status == "retired":
            RETIRED[gate_id] = reason
            return callable_fn
        GATE[gate_id] = Gate(
            id=gate_id, name=name, fixture=fixture,
            fn=callable_fn, suites=frozenset(suites),
            artifacts=frozenset(artifacts), timeout_s=timeout_s,
            timing=timing, scenario=scenario, status=status,
            reason=reason)
        return callable_fn

    if steps is not None or fn is not None:
        register(fn if fn is not None else _steps_fn(steps))
        return None
    return register


def scenario(key: str, fixture: str, gate_ids: tuple[int, ...]):
    """Register a stateful scenario fixture."""
    def deco(callable_fn):
        if key in SCENARIOS:
            raise ValueError("duplicate scenario %r" % key)
        SCENARIOS[key] = ScenarioDef(key, fixture, gate_ids, callable_fn)
        return callable_fn
    return deco


def scenario_gate(gate_id: int, name: str, key: str, *,
                  suites: tuple[str, ...] = ("green",),
                  timing: bool = False):
    """Register a gate that is executed by scenario `key`."""
    return gate(gate_id, name, fixture=None, suites=suites,
                timing=timing, scenario=key,
                fn=lambda ctx: None)


def validate() -> list[str]:
    errors: list[str] = []
    for gid, g in GATE.items():
        if g.scenario is None:
            continue
        sc = SCENARIOS.get(g.scenario)
        if sc is None:
            errors.append("gate %d references unknown scenario %r"
                          % (gid, g.scenario))
        elif gid not in sc.gate_ids:
            errors.append("gate %d missing from scenario %r ids"
                          % (gid, g.scenario))
    for key, sc in SCENARIOS.items():
        for gid in sc.gate_ids:
            g = GATE.get(gid)
            if g is None:
                if gid not in RETIRED:
                    errors.append("scenario %r references unknown gate %d"
                                  % (key, gid))
            elif g.scenario != key:
                errors.append("gate %d claims scenario %r, scenario says %r"
                              % (gid, g.scenario, key))
    return errors


def load_all() -> None:
    """Import all gate modules so decorators register themselves."""
    from . import gates  # noqa: F401  (import for side effects)


def collect() -> dict[int, Gate]:
    load_all()
    errors = validate()
    if errors:
        for e in errors:
            print("registry: " + e, file=sys.stderr)
        raise SystemExit("gate registry invalid")
    return dict(sorted(GATE.items()))


def name_of(gate_id: int) -> str:
    g = GATE.get(gate_id)
    if g is not None:
        return g.name
    return RETIRED.get(gate_id, "retired")