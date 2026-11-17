# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Julian Chu
"""Suite / gate selection and quarantine handling."""

from __future__ import annotations

import dataclasses
import os

from . import registry
from .model import Gate

SUITE_NAMES = ("smoke", "green", "heavy", "all")
QUARANTINE = os.path.join(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))), "quarantine.txt")


@dataclasses.dataclass
class Selection:
    suite: str
    gates: tuple[Gate, ...]
    partial: bool = False
    unknown: tuple[int, ...] = ()
    quarantined: tuple[int, ...] = ()


def parse_gate_spec(spec: str) -> list[int]:
    out: list[int] = []
    for part in spec.split(","):
        part = part.strip()
        if not part:
            continue
        if "-" in part:
            a, b = part.split("-", 1)
            out.extend(range(int(a), int(b) + 1))
        else:
            out.append(int(part))
    return out


def load_quarantine(path: str = QUARANTINE) -> dict[int, str]:
    """Parse `id classification reason review-by` lines; comments ignored."""
    out: dict[int, str] = {}
    if not os.path.exists(path):
        return out
    for line in open(path, encoding="utf-8"):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split(None, 1)
        try:
            gid = int(parts[0])
        except ValueError:
            continue
        out[gid] = parts[1] if len(parts) > 1 else "quarantined"
    return out


def select(*, suite: str = "all", gates: str | None = None,
           fixtures: list[str] | None = None,
           stop_after: int | None = None,
           include_retired: bool = False,
           quarantine_path: str = QUARANTINE) -> Selection:
    all_gates = registry.collect()
    quarantined = load_quarantine(quarantine_path)
    if stop_after is not None:
        gates_spec = "1-%d" % stop_after
        suite = "all"
    else:
        gates_spec = gates

    qids: set[int] = set() if suite == "all" else set(quarantined)
    chosen: list[Gate] = []
    unknown: list[int] = []
    want_ids = set(parse_gate_spec(gates_spec)) if gates_spec else None
    for gid, g in all_gates.items():
        if g.status == "retired" and not include_retired:
            continue
        if want_ids is not None and gid not in want_ids:
            continue
        if want_ids is None and gid in qids:
            continue
        if want_ids is None:
            if suite == "all":
                ok = True
            elif suite == "green":
                ok = g.status == "active" and "green" in g.suites
            else:
                ok = g.selected_by(suite)
            if not ok:
                continue
        chosen.append(g)
    if want_ids is not None:
        unknown = sorted(want_ids - set(all_gates) - set(registry.RETIRED))
    if fixtures:
        keep = set(fixtures)
        chosen = [g for g in chosen
                  if (g.fixture in keep
                      or (g.scenario and registry.SCENARIOS.get(
                          g.scenario) and
                          registry.SCENARIOS[g.scenario].fixture in keep))]
    chosen.sort(key=lambda g: g.id)
    return Selection(suite=suite, gates=tuple(chosen),
                     partial=(stop_after is not None or gates_spec is not None),
                     unknown=tuple(unknown),
                     quarantined=tuple(sorted(quarantined)))