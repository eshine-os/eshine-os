# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Julian Chu
"""QEMU session: spawn, serial socket, position-scoped reads, cleanup."""

from __future__ import annotations

import atexit
import os
import signal
import socket
import subprocess
import time

_LIVE: set["QemuSession"] = set()


def install_signal_handlers() -> None:
    """Close live QEMU sessions on SIGINT/SIGTERM (no orphan QEMU)."""
    def handler(signum, _frame):
        for s in list(_LIVE):
            try:
                s.close()
            except Exception:
                pass
        raise SystemExit(128 + signum)

    signal.signal(signal.SIGINT, handler)
    signal.signal(signal.SIGTERM, handler)


class FloodError(RuntimeError):
    """The known all-space flood poisoned this boot; retry the fixture."""


class FixtureError(RuntimeError):
    """A fixture could not be established; retry with a fresh boot."""


@atexit.register
def _close_live() -> None:
    for s in list(_LIVE):
        try:
            s.close()
        except Exception:
            pass


class QemuSession:
    def __init__(self, args: list[str], sock_path: str, log_path: str,
                 name: str = "session"):
        self.args = args
        self.sock_path = sock_path
        self.log_path = log_path
        self.name = name
        self.proc: subprocess.Popen | None = None
        self.sock: socket.socket | None = None
        self.log = None
        self.buf = b""
        self.epoch = 0
        self.gate_deadline: float | None = None
        self._closed = False

    # -- lifecycle -----------------------------------------------------
    def _spawn(self) -> None:
        if os.path.exists(self.sock_path):
            os.unlink(self.sock_path)
        self.log = open(self.log_path, "ab")
        self.log.write(b"\n==== %s %s ====\n"
                       % (self.name.encode(), time.strftime(
                           "%Y-%m-%dT%H:%M:%S").encode()))
        self.log.flush()
        self.proc = subprocess.Popen(
            self.args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            start_new_session=True)
        _LIVE.add(self)

    def _connect(self, timeout_s: float) -> None:
        deadline = time.time() + timeout_s
        last_error: Exception | None = None
        while time.time() < deadline:
            if os.path.exists(self.sock_path):
                s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                try:
                    s.connect(self.sock_path)
                    self.sock = s
                    return
                except OSError as e:
                    last_error = e
                    s.close()
            time.sleep(0.05)
        raise TimeoutError("serial socket never appeared: %s (%r)"
                           % (self.sock_path, last_error))

    def start(self, banner: bytes, *, connect_timeout_s: float = 90.0,
              banner_timeout_s: float = 36.0, retries: int = 3,
              retry_sleep_s: float = 45.0) -> None:
        last_error: Exception | None = None
        for attempt in range(1, retries + 1):
            self.buf = b""
            try:
                self._spawn()
                self._connect(connect_timeout_s)
            except (TimeoutError, ConnectionError, OSError) as e:
                last_error = e
                self.close(keep=False)
                if attempt < retries:
                    time.sleep(retry_sleep_s)
                continue
            saw_bytes, ok = self._wait_banner(banner, banner_timeout_s)
            if ok:
                return
            # Bytes but no banner: a real boot failure, do not mask it.
            if saw_bytes:
                raise TimeoutError(
                    "boot banner absent in %.0fs; output so far:\n%s"
                    % (banner_timeout_s,
                       self.buf.decode(errors="replace")))
            last_error = TimeoutError(
                "zero-byte boot (chardev family), attempt %d" % attempt)
            self.close(keep=False)
            if attempt < retries:
                time.sleep(retry_sleep_s)
        raise TimeoutError("QEMU boot failed after %d attempts: %r"
                           % (retries, last_error))

    def _wait_banner(self, banner: bytes,
                     timeout_s: float) -> tuple[bool, bool]:
        deadline = time.time() + timeout_s
        while time.time() < deadline:
            if banner in self.buf:
                return True, True
            try:
                self.recv(timeout=0.3)
            except socket.timeout:
                continue
            except (ConnectionError, OSError):
                break
        return bool(self.buf), banner in self.buf

    # -- io ------------------------------------------------------------
    def recv(self, timeout: float = 1.0) -> bytes:
        assert self.sock is not None
        self.sock.settimeout(timeout)
        chunk = self.sock.recv(4096)
        if not chunk:
            raise ConnectionError("guest closed the serial socket")
        self.buf += chunk
        if self.log is not None:
            self.log.write(chunk)
            self.log.flush()
        return chunk

    def read_until(self, needle: bytes | str, timeout_s: float, *,
                   start: int | None = 0, drain: bool = True) -> int:
        needle = needle.encode() if isinstance(needle, str) else needle
        start = 0 if start is None else start
        deadline = time.time() + timeout_s
        if self.gate_deadline is not None:
            deadline = min(deadline, self.gate_deadline)
        while needle not in self.buf[start:]:
            if time.time() > deadline:
                if (self.gate_deadline is not None
                        and time.time() > self.gate_deadline):
                    raise TimeoutError(
                        "gate deadline exceeded while waiting for %r"
                        % needle)
                raise TimeoutError(
                    "timeout waiting for %r; output so far:\n%s"
                    % (needle,
                       self.buf[start:][-4000:].decode(errors="replace")))
            try:
                self.recv(timeout=1.0)
            except socket.timeout:
                continue
        if drain:
            time.sleep(0.12)
            self.sock.settimeout(0.05)
            cap = time.time() + 0.25
            try:
                while time.time() < cap:
                    self.recv(timeout=0.05)
            except (socket.timeout, BlockingIOError):
                pass
        return start

    expect = read_until

    def mark(self) -> int:
        return len(self.buf)

    def send(self, data: bytes | str) -> None:
        data = data.encode() if isinstance(data, str) else data
        self.epoch = len(self.buf)
        assert self.sock is not None
        self.sock.sendall(data)

    def send_guest(self, data: bytes | str) -> None:
        """Legacy 8-byte chunking (pl011 RX FIFO history)."""
        data = data.encode() if isinstance(data, str) else data
        self.epoch = len(self.buf)
        assert self.sock is not None
        for i in range(0, len(data), 8):
            self.sock.sendall(data[i:i + 8])
            time.sleep(0.05)

    def tail(self, n: int = 4000) -> bytes:
        return self.buf[-n:]

    def flooded(self, start: int = 0) -> bool:
        """Detect the known all-space flood (station 58 family)."""
        w = self.buf[start:]
        return (b"wvtrip: all-space chunk" in w
                or b" " * 512 in w)

    # -- teardown ------------------------------------------------------
    def close(self, keep: bool = False) -> None:
        if self._closed:
            return
        if self.sock is not None:
            try:
                self.sock.close()
            except OSError:
                pass
            self.sock = None
        if self.proc is not None and not keep:
            try:
                os.killpg(os.getpgid(self.proc.pid), signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(os.getpgid(self.proc.pid), signal.SIGKILL)
                except ProcessLookupError:
                    pass
                self.proc.wait(timeout=5)
            self.proc = None
        if not keep:
            if self.log is not None:
                self.log.close()
                self.log = None
            if os.path.exists(self.sock_path):
                try:
                    os.unlink(self.sock_path)
                except OSError:
                    pass
            _LIVE.discard(self)
            self._closed = True
        else:
            print("KEEP_SESSION: qemu pid=%s sock=%s log=%s"
                  % (self.proc.pid if self.proc else "?",
                     self.sock_path, self.log_path))

    def __enter__(self) -> "QemuSession":
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close(keep=False)