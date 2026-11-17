# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Julian Chu
"""Data model for the fixture-based gate suite."""

from __future__ import annotations

import dataclasses
import time
from typing import Callable, Iterable

PASS = "PASS"
FAIL = "FAIL"
SKIP = "SKIP"
BLOCKED = "BLOCKED"
FLAKY = "FLAKY"

SUITES = ("smoke", "green", "heavy", "all")
TIMING_SUITE = "timing"


@dataclasses.dataclass(frozen=True)
class Gate:
    """One gate definition; the callable receives a GateContext."""

    id: int
    name: str
    fixture: str | None
    fn: Callable[["GateContext"], None]
    suites: frozenset[str] = frozenset({"green"})
    artifacts: frozenset[str] = frozenset({"elf"})
    timeout_s: float = 120.0
    timing: bool = False
    scenario: str | None = None
    status: str = "active"          # active | retired | quarantined
    reason: str = ""

    def selected_by(self, suite: str) -> bool:
        if self.status == "retired":
            return False
        if suite == "all":
            return True
        if suite == TIMING_SUITE:
            return self.timing
        return suite in self.suites


@dataclasses.dataclass
class GateResult:
    gate_id: int
    name: str
    fixture: str
    status: str
    duration_s: float = 0.0
    detail: str = ""
    log_path: str = ""
    scenario: str | None = None
    attempts: int = 1
    streamed: bool = False

    def as_dict(self) -> dict:
        return dataclasses.asdict(self)


@dataclasses.dataclass
class RunReport:
    results: list[GateResult] = dataclasses.field(default_factory=list)
    suite: str = "all"
    started_utc: str = ""
    duration_s: float = 0.0
    env: dict = dataclasses.field(default_factory=dict)
    partial: bool = False

    def counts(self) -> dict[str, int]:
        out = {PASS: 0, FAIL: 0, SKIP: 0, BLOCKED: 0, FLAKY: 0}
        for r in self.results:
            out[r.status] = out.get(r.status, 0) + 1
        return out

    def red(self) -> list[GateResult]:
        return [r for r in self.results
                if r.status in (FAIL, BLOCKED)]

    def exit_code(self, *, allow_skip: bool = True) -> int:
        if self.red():
            return 1
        if not allow_skip and any(r.status == SKIP for r in self.results):
            return 1
        return 0

    def summary_line(self) -> str:
        c = self.counts()
        red = "[" + ",".join(str(r.gate_id) for r in self.red()) + "]"
        kind = "PARTIAL-PASS" if self.partial else (
            "PASS" if not self.red() else "FAIL")
        return ("RESULT %s gates=%d pass=%d fail=%d skip=%d blocked=%d "
                "flaky=%d red=%s duration=%.1fs"
                % (kind, len(self.results), c.get(PASS, 0) + c.get(FLAKY, 0),
                   c.get(FAIL, 0), c.get(SKIP, 0), c.get(BLOCKED, 0),
                   c.get(FLAKY, 0), red, self.duration_s))


class ScenarioFailure(Exception):
    """A selected scenario gate's assertion failed; later steps are SKIP."""

    def __init__(self, gate_id: int, name: str, cause: BaseException):
        super().__init__("%d: %s" % (gate_id, cause))
        self.gate_id = gate_id
        self.name = name
        self.cause = cause


class GateContext:
    """Per-gate execution context passed to a gate function."""

    def __init__(self, session, gate: Gate, fixture_name: str, env=None):
        self.s = session
        self.gate = gate
        self.fixture_name = fixture_name
        self.env = env
        self.state: dict = {}

    def mark(self) -> int:
        return len(self.s.buf)

    def fail(self, message: str):
        raise AssertionError(message)


class ScenarioContext(GateContext):
    """Scenario context: actions always run, checks emit selected results."""

    def __init__(self, session, gate: Gate, fixture_name: str,
                 selected: set[int], results_out: list[GateResult]):
        super().__init__(session, gate, fixture_name)
        self.selected = selected
        self.results = results_out
        self._t0 = time.time()

    def check(self, gate_id: int, name: str, fn: Callable[[], None]):
        """Run an assertion for gate_id when selected; record PASS."""
        if gate_id not in self.selected:
            return
        t0 = time.time()
        try:
            fn()
        except BaseException as e:
            raise ScenarioFailure(gate_id, name, e) from e
        result = GateResult(
            gate_id=gate_id, name=name, fixture=self.fixture_name,
            status=PASS, duration_s=time.time() - t0,
            scenario=self.gate.scenario)
        self.results.append(result)
        self._stream(result)

    def emit(self, gate_id: int, name: str):
        if gate_id in self.selected:
            result = GateResult(
                gate_id=gate_id, name=name, fixture=self.fixture_name,
                status=PASS, duration_s=0.0, scenario=self.gate.scenario)
            self.results.append(result)
            self._stream(result)

    @staticmethod
    def _stream(result: GateResult) -> None:
        from .report import print_result
        result.streamed = True
        print_result(result)


def mark_selected(gates: Iterable[Gate], selected: set[int]) -> None:
    """Default selection helper used by gate modules' scenario functions."""
    selected.update(g.id for g in gates)