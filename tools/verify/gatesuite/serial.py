# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Julian Chu
"""Serial assertion helpers shared by every gate."""

from __future__ import annotations

import re
import time

_HB_RE = re.compile(rb"rtos: alive tick=\d+\r?\n")


def token_echo(s, tok: str, deadline_s: float = 10.0) -> None:
    """Send `echo TOKEN`; require the token twice (typed echo + output).

    Heartbeat lines are stripped before counting, so an interleaved
    `rtos: alive tick=N` cannot split the token.
    """
    line = ("echo %s\n" % tok).encode()
    s.send(line)
    deadline = time.time() + deadline_s
    start = s.epoch
    while True:
        cleaned = _HB_RE.sub(b"", s.buf[start:])
        if cleaned.count(tok.encode()) >= 2:
            return
        if time.time() > deadline:
            raise TimeoutError(
                "guest shell token echo incomplete for %r; output so far:\n%s"
                % (tok, s.buf[start:].decode(errors="replace")))
        s.recv(timeout=1.0)


def tick_pair(s, label: str, timeout_s: float = 30.0) -> tuple[int, int]:
    """Two `tick` reads with a 2.5s gap; the count must strictly grow."""
    p = s.mark()
    s.send("tick\n")
    s.expect("ticks: ", timeout_s, start=p)
    m = re.search(rb"ticks: (\d+)", s.buf[p:])
    if not m:
        raise AssertionError("%s: tick output missing" % label)
    before = int(m.group(1))
    time.sleep(2.5)
    p2 = s.mark()
    s.send("tick\n")
    s.expect("ticks: ", timeout_s, start=p2)
    m2 = re.search(rb"ticks: (\d+)", s.buf[p2:])
    if not m2:
        raise AssertionError("%s: second tick output missing" % label)
    after = int(m2.group(1))
    if after <= before:
        raise AssertionError("%s: tick did not grow (%d -> %d)"
                             % (label, before, after))
    return before, after