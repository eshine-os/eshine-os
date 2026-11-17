# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Julian Chu
"""Observability, logging, health, lifecycle and forensics gates 127..183.

Ported from the legacy serial verifier tools/verify/verify_el2.py.
Out of scope here (owned by the heavy module): [133] Yocto build,
[177]/[178] image aggregate + run.sh, [180] Docker static.

The legacy ran one long-lived world; this suite gives every gate (or
stateful scenario) a fresh world.  Batch context that the legacy
accumulated is re-created inside the owning scenario:

- [128] health "crashes 2": the legacy VM0 boot had recorded the
  signal-path crash (sigqdemo [95]) plus the wild-VA crash (crashdemo
  [125]); scenario/plain gate 128 replays both demos first.
- [146] audit of the boot governor chain: the fixture VM0 boot buffer
  carries the same "svc: marker start"/"svc: gated gate open" lines.
- [155..157] ACL state machine: revoke -> fault death -> re-grant ->
  kill/revoke/grant, one scenario on the dual fixture.
- [181]/[182] share the crash pid from one crashdemo run.

Anchors are legacy-verbatim.  Three of them are stale against the
current tree and are expected red until a dedupe/fix pass (kept
faithful per porting rule, flagged here for the reviewer):

- [135] "logdemo trc line 5" after `loglevel elog dbg`: the call-site
  macro ES_LOG_EMIT checks `mod.level >= lvl` (dbg=4 filters trc=5),
  see common/eshine_log.h:106.
- [140] "log core up, mods=2" from VM0 /var/log/slog: the RTOS side now
  registers boot+elog+ham (mods=3), see rtos/rt_ham.c:14 and the VM0
  boot line in any g19 log.
- [182] NT_PRSTATUS descsz=376 and cursig read at desc+14: the
  serializer writes sizeof(ce_prstatus)=368 (rtos/rt_crash.c; 600-byte
  core per docs/devlog/2026-11-03-station64-coredump.md) and cursig
  lives at desc+12 (pad0 at +14).

Timeout values follow the legacy read_until calls; gate timeout_s is
advisory in the runner.
"""

from __future__ import annotations

import os
import re
import subprocess

from .. import artifacts, recipes, serial
from ..registry import gate, scenario


def _line(s, text: str) -> int:
    """Send one shell line; return the mark that anchors its output.

    Writes longer than the PL011 RX FIFO (16-byte history) are chunked
    the way the legacy send_guest did.
    """
    data = text if text.endswith("\n") else text + "\n"
    p = len(s.buf)
    if len(data) > 16:
        s.send_guest(data)
    else:
        s.send(data)
    return p


# ---------------------------------------------------------------- plain


@gate(127, "xvm cross-VM: safeshm window, boot producer + vm1 consumer "
           "via host-owner window", fixture="dual", timeout_s=300.0)
def gate_127(ctx):
    s = ctx.s
    # dual: vm0 running (xvmsrv boot producer), vm1 booted, console host.
    recipes.hotkey(s, "1", timeout_s=300.0)
    p = ctx.mark()
    s.send("\n")
    s.expect("vm1> ", 45.0, start=p)
    p = ctx.mark()
    _line(s, "xvm")
    s.expect("xvm: waiting for vm0", 45.0, start=p)
    s.expect("xvm: got 'ping from vm0'", 90.0, start=p)
    s.expect("xvm: acked", 45.0, start=p)
    p = ctx.mark()
    _line(s, "host")
    s.expect("console -> host", 45.0, start=p)
    # The VM0-side "xvm: round-trip seq=N ok" print only fires on the
    # producer's slow path: the consumer must ack while the producer is
    # in its 150-tick wait.  A fast consumer acks during the producer's
    # 50-tick sleep, so the next loop sees ack==seq and bumps silently
    # (rt_xvm.c xvm_srv).  The consumer face above ("got 'ping from
    # vm0'" + "acked") is the round-trip proof; the producer print was
    # state-dependent in the legacy serial world and is not a stable
    # assertion for a fresh fixture.


@gate(128, "health: batch roll-up panel (uptime/tasks/procs/fp/parts/"
           "crash/xvm)", fixture="vm0", timeout_s=420.0)
def gate_128(ctx):
    s = ctx.s
    # Legacy batch context: sigqdemo [95] left the signal-path crash,
    # crashdemo [125] the wild-VA crash => health reports "crashes 2".
    p = ctx.mark()
    _line(s, "sigqdemo")
    s.expect("sigq: default terminate 140 ok", 45.0, start=p)
    p = ctx.mark()
    _line(s, "crashdemo")
    s.expect("crashdemo: done", 45.0, start=p)
    p = ctx.mark()
    _line(s, "health")
    s.expect("health: uptime ", 60.0, start=p)
    s.expect("tick, tasks ", 30.0, start=p)
    s.expect(", procs ", 30.0, start=p)
    s.expect("fp-saves ", 30.0, start=p)
    s.expect("cparts 1", 30.0, start=p)
    s.expect("mparts 0", 30.0, start=p)
    s.expect("crashes 2", 30.0, start=p)
    s.expect("xvm seq ", 30.0, start=p)


@gate(129, "console hotkeys: ctrl-^ 0/1/2 switch + h host (hb alive via "
           "hotkey)", fixture="multi", timeout_s=400.0)
def gate_129(ctx):
    s = ctx.s
    # multi ends attached to VM2's busybox shell; the legacy window had
    # all three VMs RUNNING with owner=vm2 and hotkey-switched from there.
    recipes.hotkey(s, "0", timeout_s=300.0)
    p = ctx.mark()
    _line(s, "hb")
    s.expect("rtos: alive", 30.0, start=p)
    recipes.hotkey(s, "1", timeout_s=300.0)
    recipes.hotkey(s, "2", timeout_s=300.0)
    recipes.hotkey(s, "h", expect_vm=False, timeout_s=30.0)


@gate(130, "rtos exit -> VM_DEAD (HVC#1 face, RX auto-reclaimed)",
      fixture="vm0", timeout_s=90.0)
def gate_130(ctx):
    p = _line(ctx.s, "exit")
    ctx.s.expect("vm0: VM_DEAD", 45.0, start=p)


@gate(131, "overflow -> panic names overflower (tick-scan face); vm0 "
           "reaped", fixture="vm0", timeout_s=180.0)
def gate_131(ctx):
    s = ctx.s
    # [35]-style command-face secondary readback (the one-shot boot line
    # is lost forever under the chardev byte-loss family; smpstat replays).
    p = ctx.mark()
    _line(s, "smpstat")
    s.expect("smp: cpu1 gicr1_en=", 30.0, start=p)
    m = re.search(rb"smp: cpu1 gicr1_en=(\d+)", s.buf[p:])
    if not m or int(m.group(1)) != 0x08000001:
        raise AssertionError("reboot secondary readback bad: %r"
                             % s.buf[p:][-200:])
    p = ctx.mark()
    _line(s, "overflow")
    s.expect("rtos panic: stack overflow", 60.0, start=p)
    s.expect("context: tick scan", 5.0, start=p)
    if b"task: overflower" not in s.buf[p:]:
        raise AssertionError("panic did not name overflower: %r"
                             % s.buf[p:][-200:])
    recipes.escape_pump(s, "console -> host (escape)", timeout_s=30.0)
    p = ctx.mark()
    _line(s, "vmkill 0")
    s.expect("killed (host authority)", 45.0, start=p)


@gate(132, "shell VM killed (authority); host intact (tick %d->%d, token "
           "echo)", fixture="multi", timeout_s=240.0)
def gate_132(ctx):
    s = ctx.s
    # multi ends attached to VM2; the legacy window was already on the
    # host prompt, so take the console back first.
    recipes.hotkey(s, "h", expect_vm=False, timeout_s=30.0)
    p = ctx.mark()
    _line(s, "vmkill 2")
    s.expect("killed (host authority)", 45.0, start=p)
    serial.tick_pair(s, "post-linux tick")
    serial.token_echo(s, "ESHINEOK%d" % (os.getpid() + 3))


@gate(143, "vm0 /proc/meminfo: page pool accounting via deep-path prefix "
           "routing", fixture="vm0", artifacts=("elf", "busybox"),
      timeout_s=240.0)
def gate_143(ctx):
    s = ctx.s
    p = _line(s, "run /bin/busybox cat /proc/meminfo")
    s.expect("MemTotal:", 120.0, start=p)


@gate(144, "vm0 /proc/cpuinfo: both vCPUs enumerated", fixture="vm0",
      artifacts=("elf", "busybox"), timeout_s=240.0)
def gate_144(ctx):
    s = ctx.s
    p = _line(s, "run /bin/busybox cat /proc/cpuinfo")
    s.expect("processor\t: 0", 120.0, start=p)
    s.expect("processor\t: 1", 60.0, start=p)


@gate(145, "vm0 /proc/interrupts: per-intid counters (vtimer/uart/ipi)",
      fixture="vm0", artifacts=("elf", "busybox"), timeout_s=240.0)
def gate_145(ctx):
    s = ctx.s
    p = _line(s, "run /bin/busybox cat /proc/interrupts")
    s.expect(" 27: ", 120.0, start=p)
    s.expect(" 33: ", 60.0, start=p)
    s.expect(" 0: ", 60.0, start=p)


@gate(152, "vm0 dbgdemo: platform probe (mdscr quiescent + pmu id "
           "readable; real-hw variant: trapped/raz)", fixture="vm0",
      timeout_s=120.0)
def gate_152(ctx):
    s = ctx.s
    p = _line(s, "dbgdemo")
    s.expect("dbg: mdscr quiescent (tcg stub, no trap)", 300.0, start=p)
    s.expect("dbg: pmu id readable (tcg, counters inert)", 300.0,
             start=p)
    s.expect("dbg: probe mdscr=", 300.0, start=p)


@gate(153, "host vmdbg: per-VM mdscr slot dump (frame inspection face; "
           "all-zero on tcg = trap-inert)", fixture="vm0ready",
      timeout_s=120.0)
def gate_153(ctx):
    s = ctx.s
    # vm0ready: VM0 running, console on the host (the legacy vmdbg ran
    # after escaping the VM0 console).
    p = _line(s, "vmdbg")
    s.expect("vm0.v0: mdscr=0x", 300.0, start=p)


@gate(154, "vm1: mdscr probe (second guest view, per-vm isolation by "
           "platform stub)", fixture="vm1", timeout_s=120.0)
def gate_154(ctx):
    # The bare-metal guest self-reports the probe during its boot; the
    # fixture prepare already consumed that stream into ctx.s.buf.
    if b"dbg: mdscr probe 0x0000000000000000" not in ctx.s.buf:
        raise AssertionError("vm1 mdscr probe missing from boot stream: %r"
                             % ctx.s.buf[-400:])


@gate(162, "host vmstat: per-VM preempt + quanta accounting (both faces)",
      fixture="host", timeout_s=90.0)
def gate_162(ctx):
    s = ctx.s
    p = _line(s, "vmstat")
    s.expect("vm-preempts: vm0=", 300.0, start=p)
    s.expect("quanta: vm0=", 300.0, start=p)


@gate(163, "host vmtrail <N> cpu3: filtered query face (cpu3-only, no "
           "cpu0 leak)", fixture="host", timeout_s=90.0)
def gate_163(ctx):
    s = ctx.s
    p = _line(s, "vmtrail 16 cpu3")
    s.expect("cpu3", 300.0, start=p)
    if b"cpu0" in s.buf[p:]:
        raise AssertionError("[163] vmtrail cpu3 filter leaks cpu0 lines")


@gate(164, "host vmtrail <N> cpu0: filtered query face (cpu0-only) — "
           "batch B complete", fixture="host", timeout_s=90.0)
def gate_164(ctx):
    s = ctx.s
    p = _line(s, "vmtrail 16 cpu0")
    s.expect("cpu0", 300.0, start=p)
    if b"cpu3" in s.buf[p:]:
        raise AssertionError("[164] vmtrail cpu0 filter leaks cpu3 lines")


@gate(165, "vm0 sockdemo: AF_UNIX 八门真号往返 (socket/bind/listen/"
           "connect/accept/sendto/recvfrom/shutdown — musl 直连零 patch)",
      fixture="vm0", timeout_s=1000.0)
def gate_165(ctx):
    s = ctx.s
    p = _line(s, "run /bin/sockdemo")
    s.expect("sockdemo: echo ok <ESHINE57>", 120.0, start=p)


@gate(166, "vm0 sockdemo rerun: socket 对象回收+复用 (SOCK_MAX 池无泄漏)",
      fixture="vm0", timeout_s=1000.0)
def gate_166(ctx):
    s = ctx.s
    p = _line(s, "run /bin/sockdemo")
    s.expect("sockdemo: echo ok <ESHINE57>", 120.0, start=p)


@gate(167, "vm0 hello co-run: fd 层共存面无回归 — batch C first station",
      fixture="vm0", timeout_s=400.0)
def gate_167(ctx):
    s = ctx.s
    p = _line(s, "run /bin/hello")
    s.expect("musl: hello from /bin/hello", 300.0, start=p)


@gate(172, "vm0 jsontest: cJSON parse of services.json (governance "
           "manifest as real consumer data)", fixture="vm0",
      timeout_s=1000.0)
def gate_172(ctx):
    s = ctx.s
    p = _line(s, "run /bin/jsontest")
    s.expect("jsontest: services=3", 120.0, start=p)
    s.expect("jsontest: beater mode=respawn max=2", 300.0, start=p)


@gate(173, "vm0 ztest: slog deflate->disk->readback->inflate byte-exact "
           "(compress-to-disk round trip)", fixture="vm0",
      timeout_s=1000.0)
def gate_173(ctx):
    s = ctx.s
    p = _line(s, "logctrl flush")
    s.expect("logctrl: flushed, kcursor ", 120.0, start=p)
    p = _line(s, "run /bin/ztest")
    s.expect("ztest: inflate match", 120.0, start=p)


@gate(174, "host 3rd ledger integrity: LEDGER.md lists 4 components + "
           "in-repo tarballs present — batch C third station",
      fixture=None, artifacts=("none",), timeout_s=60.0)
def gate_174(ctx):
    with open(os.path.join(artifacts.REPO, "3rd/LEDGER.md"),
              encoding="utf-8") as lf:
        ledger = lf.read()
    for comp in ("musl", "busybox", "cJSON", "zlib"):
        if comp not in ledger:
            raise AssertionError("[174] 3rd/LEDGER.md missing %s" % comp)
    for tb in ("musl-1.2.5.tar.gz", "cjson-1.7.18.tar.gz",
               "zlib-1.3.1.tar.gz"):
        if not os.path.exists(os.path.join(artifacts.REPO, "3rd", tb)):
            raise AssertionError("[174] 3rd/ missing tarball %s" % tb)


@gate(175, "vm0 cyclictest: wake-latency histogram (one-tick bucket "
           "dominant, max within 3-tick bound)", fixture="vm0",
      timeout_s=1000.0)
def gate_175(ctx):
    s = ctx.s
    p = _line(s, "run /bin/cyclictest")
    s.expect("cyclictest: 200 loops period=10ms", 120.0, start=p)
    s.expect("cyclictest: 9-10ms: ", 300.0, start=p)
    m = re.search(rb"cyclictest: 9-10ms: (\d+)", s.buf[p:])
    if not m or int(m.group(1)) == 0:
        raise AssertionError("[175] wake histogram one-tick bucket empty: %r"
                             % s.buf[p:][-300:])
    s.expect("cyclictest: max=", 300.0, start=p)
    m = re.search(rb"cyclictest: max=(\d+)us", s.buf[p:])
    if not m or int(m.group(1)) > 30000:
        raise AssertionError("[175] wake max beyond 3-tick sanity bound: %r"
                             % s.buf[p:][-200:])


@gate(176, "host baseline ledger: docs/benchmark/wake-latency-"
           "baseline.md recorded (numbers non-gate, trend face) — batch D "
           "first station", fixture=None, artifacts=("none",),
      timeout_s=30.0)
def gate_176(ctx):
    path = os.path.join(artifacts.REPO, "docs/benchmark/",
                        "wake-latency-baseline.md")
    with open(path, encoding="utf-8") as bf:
        if "9-10ms" not in bf.read():
            raise AssertionError("[176] baseline doc missing bucket")


@gate(179, "host SBOM integrity: manifest.yaml 8 components, version pin "
           "+ GPL isolation + NOTICE aggregation verified — batch D third "
           "station", fixture=None, artifacts=("none",), timeout_s=150.0)
def gate_179(ctx):
    sc = subprocess.run(["python3", "tools/sbom-check.py"],
                        capture_output=True, text=True,
                        cwd=artifacts.REPO, timeout=120)
    if sc.returncode != 0:
        raise AssertionError("[179] sbom-check failed: %r / %r"
                             % (sc.stdout[-300:], sc.stderr[-300:]))
    if "ALL GREEN" not in sc.stdout:
        raise AssertionError("[179] sbom-check output wrong: %r"
                             % sc.stdout[-200:])


@gate(183, "bsh: busybox ash subshell + line REPL (musl fork contract + "
           "tpidr inheritance; sh -c rows with echo/var expansion + root "
           "dir listing; empty line exits)", fixture="vm0",
      artifacts=("elf", "busybox"), timeout_s=400.0)
def gate_183(ctx):
    s = ctx.s
    p = _line(s, "bsh echo bsh65-ok")
    s.expect("bsh65-ok", 90.0, start=p)
    s.expect("rtos> ", 60.0, start=p)
    p = _line(s, "bsh V=eshine-sub; echo $V")
    s.expect("eshine-sub", 90.0, start=p)
    s.expect("rtos> ", 60.0, start=p)
    p = _line(s, "bsh")
    s.expect("line REPL (empty line = exit)", 30.0, start=p)

    # REPL lines go out as one write: the bsh parent reads a line per
    # stdin read, and the 8-byte send_guest chunking can split a line
    # into partial reads.  Per-line marks + "\n<output>" anchors keep
    # the typed echo (which contains the value) from satisfying the
    # assertion before the command actually runs.
    def repl(line: str, needle: str, timeout_s: float = 90.0):
        p = ctx.mark()
        s.send(line + "\n")
        s.expect(needle, timeout_s, start=p)
        s.expect("ash> ", 30.0, start=p)

    repl("echo repl66-ok", b"\nrepl66-ok")
    repl("W=repl-world; echo $W", b"\nrepl-world")
    p = ctx.mark()
    s.send("ls /\n")
    s.expect("bin", 90.0, start=p)
    s.expect("etc", 30.0, start=p)
    s.expect("ash> ", 30.0, start=p)
    p = ctx.mark()
    s.send("\n")
    s.expect("bsh: exit REPL", 30.0, start=p)
    s.expect("rtos> ", 30.0, start=p)
    s.expect("rtos> ", 30.0, start=p)


# ------------------------------------------------------------ scenarios


@gate(134, "host logdemo: err/wrn/inf + filter (dbg/trc absent) + "
           "ratelimit 3/7", fixture=None, scenario="log-host")
def gate_134(ctx):
    """Executed by scenario_log_host."""


@gate(135, "host loglevel elog dbg -> dbg/trc now emitted (runtime level "
           "control)", fixture=None, scenario="log-host")
def gate_135(ctx):
    """Executed by scenario_log_host."""


@gate(136, "host logmods: module registry (boot+elog via linker section)",
      fixture=None, scenario="log-host")
def gate_136(ctx):
    """Executed by scenario_log_host."""


@gate(137, "host logdump: ring dump incl. boot self-proof line (ring "
           "store audit)", fixture=None, scenario="log-host")
def gate_137(ctx):
    """Executed by scenario_log_host."""


@scenario("log-host", "host", (134, 135, 136, 137))
def scenario_log_host(ctx):
    s = ctx.s
    p134 = ctx.mark()
    _line(s, "logdemo")

    def check_134():
        s.expect("logdemo err line 1", 300.0, start=p134)
        s.expect("logdemo wrn line 2", 300.0, start=p134)
        s.expect("logdemo inf line 3", 300.0, start=p134)
        s.expect("ratelimit pass=3 suppressed=7", 300.0, start=p134)
        if b"logdemo dbg line" in s.buf[p134:]:
            raise AssertionError("[134] logdemo: dbg visible at default "
                                 "INF: %r" % s.buf[p134:][-300:])

    ctx.check(134, gate_134_name(), check_134)

    # Runtime level control is a world-state action: [136] logmods must
    # observe elog at dbg.
    p135 = ctx.mark()
    _line(s, "loglevel elog dbg")
    s.expect("loglevel: elog dbg", 300.0, start=p135)
    _line(s, "logdemo")

    def check_136():
        p = ctx.mark()
        _line(s, "logmods")
        s.expect("elog: dbg", 300.0, start=p)
        s.expect("boot: inf", 300.0, start=p)

    ctx.check(136, gate_136_name(), check_136)

    def check_137():
        p = ctx.mark()
        _line(s, "logdump")
        s.expect("logdump: --- ring start ---", 300.0, start=p)
        s.expect("log core up, mods=2", 300.0, start=p)
        s.expect("logdump: --- ring end ---", 300.0, start=p)

    ctx.check(137, gate_137_name(), check_137)

    # [135] is the known-stale anchor (trc filtered at module dbg); check
    # it last so its red does not block 136/137, which share the session.
    def check_135():
        s.expect("loglevel: elog dbg", 300.0, start=p135)
        s.expect("logdemo dbg line 4", 300.0, start=p135)
        # Legacy-verbatim stale anchor (see module notes): the call-site
        # macro filters trc (5) at module level dbg (4).
        s.expect("logdemo trc line 5", 10.0, start=p135)

    ctx.check(135, gate_135_name(), check_135)


def gate_134_name():
    return ("host logdemo: err/wrn/inf + filter (dbg/trc absent) + "
            "ratelimit 3/7")


def gate_135_name():
    return ("host loglevel elog dbg -> dbg/trc now emitted (runtime level "
            "control)")


def gate_136_name():
    return "host logmods: module registry (boot+elog via linker section)"


def gate_137_name():
    return ("host logdump: ring dump incl. boot self-proof line (ring "
            "store audit)")


@gate(138, "vm0 logdemo (via demo op trampoline): contract levels + "
           "filter + ratelimit on rtos side", fixture=None,
      scenario="log-vm0")
def gate_138(ctx):
    """Executed by scenario_log_vm0."""


@gate(139, "vm0 loglevel/logmods: runtime level control + module registry "
           "on rtos side", fixture=None, scenario="log-vm0")
def gate_139(ctx):
    """Executed by scenario_log_vm0."""


@scenario("log-vm0", "vm0", (138, 139))
def scenario_log_vm0(ctx):
    s = ctx.s
    p = ctx.mark()
    _line(s, "logdemo")

    def check_138():
        s.expect("logdemo err line 1", 120.0, start=p)
        s.expect("ratelimit pass=3 suppressed=7", 300.0, start=p)
        if b"logdemo dbg line" in s.buf[p:]:
            raise AssertionError("[138] vm0 logdemo: dbg visible at "
                                 "default INF: %r" % s.buf[p:][-300:])

    ctx.check(138, gate_138_name(), check_138)

    def check_139():
        p2 = ctx.mark()
        _line(s, "loglevel elog dbg")
        s.expect("loglevel: elog dbg", 300.0, start=p2)
        _line(s, "logmods")
        s.expect("elog: dbg", 300.0, start=p2)

    ctx.check(139, gate_139_name(), check_139)


def gate_138_name():
    return ("vm0 logdemo (via demo op trampoline): contract levels + "
            "filter + ratelimit on rtos side")


def gate_139_name():
    return ("vm0 loglevel/logmods: runtime level control + module registry "
            "on rtos side")


@gate(140, "vm0 klog persistence: elog ring -> /var/log/slog (flush + "
           "busybox cat reads kernel lines back)", fixture=None,
      scenario="slog", artifacts=("elf", "busybox"))
def gate_140(ctx):
    """Executed by scenario_slog."""


@gate(141, "vm0 slog dual source: user WRITE face persists 'S [X]' lines "
           "(system source)", fixture=None, scenario="slog",
      artifacts=("elf", "busybox"))
def gate_141(ctx):
    """Executed by scenario_slog."""


@gate(142, "vm0 slog rotation: logdemo 16 -> both generations in service "
           "(slog + slog.1)", fixture=None, scenario="slog")
def gate_142(ctx):
    """Executed by scenario_slog."""


@scenario("slog", "vm0", (140, 141, 142))
def scenario_slog(ctx):
    s = ctx.s

    def check_141():
        p = ctx.mark()
        _line(s, "slogdemo")
        s.expect("slog: dumped 3 lines", 120.0, start=p)
        _line(s, "logctrl flush")
        s.expect("logctrl: flushed, kcursor ", 300.0, start=p)
        _line(s, "run /bin/busybox cat /var/log/slog")
        s.expect("S [E] slog-three-err", 120.0, start=p)

    ctx.check(141, gate_141_name(), check_141)

    def check_142():
        p = ctx.mark()
        _line(s, "logdemo 16")
        s.expect("ratelimit pass=3 suppressed=7", 120.0, start=p)
        _line(s, "logctrl flush")
        s.expect("logctrl: flushed, kcursor ", 300.0, start=p)
        _line(s, "logctrl stat")
        s.expect("/var/log/slog.1", 300.0, start=p)

    ctx.check(142, gate_142_name(), check_142)

    # [140] carries the known-stale mods=2 anchor; check it last so its
    # red does not block 141/142 sharing the slog session.
    def check_140():
        p = ctx.mark()
        _line(s, "logdemo")
        s.expect("logdemo err line 1", 120.0, start=p)
        _line(s, "logctrl flush")
        s.expect("logctrl: flushed, kcursor ", 300.0, start=p)
        _line(s, "run /bin/busybox cat /var/log/slog")
        s.expect("logdemo err line 1", 120.0, start=p)
        # Legacy-verbatim stale anchor (module notes): VM0 now has
        # boot+elog+ham modules => mods=3, not 2.
        s.expect("log core up, mods=2", 60.0, start=p)

    ctx.check(140, gate_140_name(), check_140)


def gate_140_name():
    return ("vm0 klog persistence: elog ring -> /var/log/slog (flush + "
            "busybox cat reads kernel lines back)")


def gate_141_name():
    return ("vm0 slog dual source: user WRITE face persists 'S [X]' lines "
            "(system source)")


def gate_142_name():
    return ("vm0 slog rotation: logdemo 16 -> both generations in service "
            "(slog + slog.1)")


@gate(146, "vm0 svcdemo: manifest parse + ordered start + ready gate "
           "checkpoint (boot chain audited in full buf)", fixture=None,
      scenario="svc", artifacts=("elf",))
def gate_146(ctx):
    """Executed by scenario_svc."""


@gate(147, "vm0 svcdemo: waitpid monitor + respawn restart + "
           "max_restarts cap", fixture=None, scenario="svc",
      artifacts=("elf",))
def gate_147(ctx):
    """Executed by scenario_svc."""


@gate(148, "vm0 svcdemo: governance chain complete", fixture=None,
      scenario="svc", artifacts=("elf",))
def gate_148(ctx):
    """Executed by scenario_svc."""


@scenario("svc", "vm0", (146, 147, 148))
def scenario_svc(ctx):
    s = ctx.s
    p = ctx.mark()
    _line(s, "svcdemo")

    def check_146():
        # Legacy audits the full session buffer (boot governor chain).
        if b"svc: marker start" not in s.buf or \
                b"svc: gated gate open" not in s.buf:
            raise AssertionError("[146] boot governor chain absent")
        s.expect("svc: marker start", 120.0, start=p)
        s.expect("svc: gated gate /tmp/init40", 60.0, start=p)
        s.expect("svc: gated gate open", 60.0, start=p)

    ctx.check(146, gate_146_name(), check_146)

    def check_147():
        s.expect("svc: gated exited", 120.0, start=p)
        s.expect("svc: beater restart", 120.0, start=p)
        s.expect("svc: beater giving up", 120.0, start=p)

    ctx.check(147, gate_147_name(), check_147)

    def check_148():
        s.expect("svc: governed", 120.0, start=p)

    ctx.check(148, gate_148_name(), check_148)


def gate_146_name():
    return ("vm0 svcdemo: manifest parse + ordered start + ready gate "
            "checkpoint (boot chain audited in full buf)")


def gate_147_name():
    return ("vm0 svcdemo: waitpid monitor + respawn restart + "
            "max_restarts cap")


def gate_148_name():
    return "vm0 svcdemo: governance chain complete"


@gate(149, "vm0 ham: entity registered + beat + HEALTHY state",
      fixture=None, scenario="ham", artifacts=("elf",))
def gate_149(ctx):
    """Executed by scenario_ham."""


@gate(150, "vm0 ham: 2-period no-beat -> MISSED -> recovery beat -> "
           "HEALTHY (condition-action lifecycle)", fixture=None,
      scenario="ham", artifacts=("elf",))
def gate_150(ctx):
    """Executed by scenario_ham."""


@gate(151, "vm0 ham: warn action persisted (elog ring -> /var/log/slog "
           "audit)", fixture=None, scenario="ham",
      artifacts=("elf", "busybox"))
def gate_151(ctx):
    """Executed by scenario_ham."""


@scenario("ham", "vm0", (149, 150, 151))
def scenario_ham(ctx):
    s = ctx.s
    # Register + beat + first list (state needed by the MISSED cycle).
    p149 = ctx.mark()
    _line(s, "ham add hb1 100")
    _line(s, "hambeat hb1")
    _line(s, "ham ls")

    def check_149():
        s.expect("ham: added", 300.0, start=p149)
        s.expect("ham: beat", 300.0, start=p149)
        s.expect("hb1: HEALTHY", 300.0, start=p149)

    ctx.check(149, gate_149_name(), check_149)

    # 2-period no-beat => MISSED; recovery beat => HEALTHY.
    p150 = ctx.mark()
    _line(s, "sleep 4")
    _line(s, "ham ls")
    _line(s, "hambeat hb1")
    _line(s, "ham ls")

    def check_150():
        s.expect("ham: hb1 MISSED", 120.0, start=p150)
        s.expect("hb1: MISSED", 300.0, start=p150)
        s.expect("ham: hb1 recovered", 300.0, start=p150)
        s.expect("hb1: HEALTHY", 300.0, start=p150)

    ctx.check(150, gate_150_name(), check_150)

    def check_151():
        # The warn action went to the elog ring during the MISSED cycle;
        # flush + busybox cat reads it back from /var/log/slog.
        p = ctx.mark()
        _line(s, "logctrl flush")
        s.expect("logctrl: flushed", 300.0, start=p)
        _line(s, "run /bin/busybox cat /var/log/slog")
        s.expect("ham hb1 missed", 120.0, start=p)

    ctx.check(151, gate_151_name(), check_151)


def gate_149_name():
    return "vm0 ham: entity registered + beat + HEALTHY state"


def gate_150_name():
    return ("vm0 ham: 2-period no-beat -> MISSED -> recovery beat -> "
            "HEALTHY (condition-action lifecycle)")


def gate_151_name():
    return ("vm0 ham: warn action persisted (elog ring -> /var/log/slog "
            "audit)")


@gate(155, "xvmacl revoke: runtime stage-2 entry clear + TLBI -> vm1 "
           "window access faults -> isolation death", fixture=None,
      scenario="acl", artifacts=("elf",))
def gate_155(ctx):
    """Executed by scenario_acl."""


@gate(156, "xvmacl re-grant: ACL durable across create -> vm1 revived "
           "(boot self-report)", fixture=None, scenario="acl",
      artifacts=("elf",))
def gate_156(ctx):
    """Executed by scenario_acl."""


@gate(157, "xvmacl state machine: kill->revoke->grant (ACL face complete, "
           "vm1 parked clean)", fixture=None, scenario="acl",
      artifacts=("elf",))
def gate_157(ctx):
    """Executed by scenario_acl."""


@scenario("acl", "dual", (155, 156, 157))
def scenario_acl(ctx):
    s = ctx.s
    # [155] revoke the safeshm window, then vm1's xvm access must fault.
    p_rev = ctx.mark()
    _line(s, "xvmacl 1 off")
    s.expect("xvm: window revoked", 300.0, start=p_rev)
    recipes.attach_vm(s, 1, "\nvm1> ", wake_retries=6, timeout_s=25.0)
    p_fault = ctx.mark()
    _line(s, "xvm")
    s.expect("fault:", 120.0, start=p_fault)
    recipes.wake_host(s, timeout_s=25.0)
    p_ls = ctx.mark()
    _line(s, "vmls")
    s.expect("VM_DEAD", 300.0, start=p_ls)

    def check_155():
        s.expect("xvm: window revoked", 300.0, start=p_rev)
        s.expect("fault:", 120.0, start=p_fault)
        s.expect("VM_DEAD", 300.0, start=p_ls)

    ctx.check(155, gate_155_name(), check_155)

    # [156] re-grant is durable across vmcreate: vm1 revives.
    p_on = ctx.mark()
    _line(s, "xvmacl 1 on")
    s.expect("xvm: window granted", 300.0, start=p_on)
    p_cr = ctx.mark()
    _line(s, "vmcreate 1")
    s.expect("vm1: CREATED", 300.0, start=p_cr)
    p_run = ctx.mark()
    _line(s, "vmrun 1")
    s.expect("dbg: mdscr probe 0x0000000000000000", 120.0, start=p_run)

    def check_156():
        s.expect("xvm: window granted", 300.0, start=p_on)
        s.expect("vm1: CREATED", 300.0, start=p_cr)
        s.expect("dbg: mdscr probe 0x0000000000000000", 120.0,
                 start=p_run)

    ctx.check(156, gate_156_name(), check_156)

    # [157] kill -> revoke -> grant (ACL face complete).
    p_k = ctx.mark()
    _line(s, "vmkill 1")
    s.expect("killed", 300.0, start=p_k)
    p_off = ctx.mark()
    _line(s, "xvmacl 1 off")
    s.expect("xvm: window revoked", 300.0, start=p_off)
    p_up = ctx.mark()
    _line(s, "xvmacl 1 on")
    s.expect("xvm: window granted", 300.0, start=p_up)

    def check_157():
        s.expect("killed", 300.0, start=p_k)
        s.expect("xvm: window revoked", 300.0, start=p_off)
        s.expect("xvm: window granted", 300.0, start=p_up)

    ctx.check(157, gate_157_name(), check_157)


def gate_155_name():
    return ("xvmacl revoke: runtime stage-2 entry clear + TLBI -> vm1 "
            "window access faults -> isolation death")


def gate_156_name():
    return ("xvmacl re-grant: ACL durable across create -> vm1 revived "
            "(boot self-report)")


def gate_157_name():
    return ("xvmacl state machine: kill->revoke->grant (ACL face complete, "
            "vm1 parked clean)")


@gate(158, "vm0 pscidemo: AFFINITY_INFO (0=ON both vcpus, DENIED "
           "invalid)", fixture=None, scenario="psci-dfx",
      artifacts=("elf",))
def gate_158(ctx):
    """Executed by scenario_psci_dfx."""


@gate(159, "host vmstat: per-VM pump quanta accounting (实时配额调度 "
           "BUDGET/USED form)", fixture=None, scenario="psci-dfx",
      artifacts=("elf",))
def gate_159(ctx):
    """Executed by scenario_psci_dfx."""


@gate(160, "vm0 pscireset: SYSTEM_RESET -> teardown -> rebuild -> re-boot "
           "(pump-exit reset chain)", fixture=None, scenario="psci-dfx",
      artifacts=("elf",))
def gate_160(ctx):
    """Executed by scenario_psci_dfx."""


@gate(161, "vm0 pscioff: SYSTEM_OFF -> clean VM_DEAD (no fault lines) -> "
           "revive chain ready", fixture=None, scenario="psci-dfx",
      artifacts=("elf",))
def gate_161(ctx):
    """Executed by scenario_psci_dfx."""


@scenario("psci-dfx", "vm0", (158, 159, 160, 161))
def scenario_psci_dfx(ctx):
    s = ctx.s

    def check_158():
        p = ctx.mark()
        _line(s, "pscidemo")
        s.expect("psci: aff vcpu0=0 vcpu1=0 invalid=4294967293", 300.0,
                 start=p)

    ctx.check(158, gate_158_name(), check_158)

    # [159] host-side per-VM pump quanta.
    recipes.escape_pump(s, "eshine> ", timeout_s=25.0)

    def check_159():
        p = ctx.mark()
        _line(s, "vmstat")
        s.expect("quanta: vm0=", 300.0, start=p)

    ctx.check(159, gate_159_name(), check_159)

    # [160] SYSTEM_RESET: teardown + rebuild + re-boot.  The re-boot
    # anchors are world state for [161], so wait for them as actions.
    recipes.attach_vm(s, 0, "\r\nrtos> ", wake_retries=6, timeout_s=25.0)
    p_reset = ctx.mark()
    _line(s, "pscireset")
    s.expect("vm0: system reset (psci)", 120.0, start=p_reset)
    s.expect("cpu1: online (rtos psci)", 120.0, start=p_reset)

    def check_160():
        s.expect("psci: resetting", 300.0, start=p_reset)
        s.expect("vm0: system reset (psci)", 120.0, start=p_reset)
        s.expect("cpu1: online (rtos psci)", 120.0, start=p_reset)

    ctx.check(160, gate_160_name(), check_160)

    # [161] SYSTEM_OFF -> clean VM_DEAD.
    def check_161():
        recipes.attach_vm(s, 0, "\r\nrtos> ", wake_retries=6,
                          timeout_s=25.0)
        p = ctx.mark()
        _line(s, "pscioff")
        s.expect("psci: powering off", 300.0, start=p)
        s.expect("vm0: system off (psci)", 120.0, start=p)
        recipes.wake_host(s, timeout_s=25.0)
        p3 = ctx.mark()
        _line(s, "vmls")
        s.expect("VM_DEAD", 300.0, start=p3)

    ctx.check(161, gate_161_name(), check_161)


def gate_158_name():
    return "vm0 pscidemo: AFFINITY_INFO (0=ON both vcpus, DENIED invalid)"


def gate_159_name():
    return ("host vmstat: per-VM pump quanta accounting (实时配额调度 "
            "BUDGET/USED form)")


def gate_160_name():
    return ("vm0 pscireset: SYSTEM_RESET -> teardown -> rebuild -> re-boot "
            "(pump-exit reset chain)")


def gate_161_name():
    return ("vm0 pscioff: SYSTEM_OFF -> clean VM_DEAD (no fault lines) -> "
            "revive chain ready")


@gate(168, "vm2 virtio_net bind: eth0 registered (Linux driver via "
           "second virtio-mmio window)", fixture=None, scenario="vm2-net")
def gate_168(ctx):
    """Executed by scenario_vm2_net."""


@gate(169, "vm2 F_MAC negotiation: config MAC read (feature bit + config "
           "space face)", fixture=None, scenario="vm2-net")
def gate_169(ctx):
    """Executed by scenario_vm2_net."""


@gate(170, "vm2 device-level TX: 42 bytes / 1 packet (xmit->kick->desc->"
           "used->netdev counter; RX=0=ARP-echo-drop boundary documented)",
      fixture=None, scenario="vm2-net")
def gate_170(ctx):
    """Executed by scenario_vm2_net."""


@gate(171, "host gdump forensics: guest RAM hexdump (arm64 Image magic "
           "'ARMd') — batch C second station", fixture=None,
      scenario="vm2-net")
def gate_171(ctx):
    """Executed by scenario_vm2_net."""


@scenario("vm2-net", "vm2", (168, 169, 170, 171))
def scenario_vm2_net(ctx):
    s = ctx.s

    def check_168():
        p = ctx.mark()
        _line(s, "ls /sys/class/net")
        s.expect("eth0", 90.0, start=p)

    ctx.check(168, gate_168_name(), check_168)

    def check_169():
        p = ctx.mark()
        _line(s, "cat /sys/class/net/eth0/address")
        s.expect("52:55:0a:00:02:58", 90.0, start=p)

    ctx.check(169, gate_169_name(), check_169)

    # Device-level TX accounting needs the interface configured and one
    # ping transmitted (world state for [170]).
    p = ctx.mark()
    _line(s, "ifconfig eth0 10.0.0.2 netmask 255.255.255.0 up")
    s.expect("~ # ", 90.0, start=p)
    p = ctx.mark()
    _line(s, "ping -c 1 -W 3 10.0.0.1")
    s.expect("~ # ", 120.0, start=p)

    def check_170():
        p = ctx.mark()
        _line(s, "cat /proc/net/dev")
        s.expect("eth0:", 90.0, start=p)
        m = re.search(rb"eth0:\s+(\d+)\s+(\d+).*?\s(\d+)\s+(\d+)\s+0\s+0"
                      rb"\s+0\s+0\s+0\s+0\s*$", s.buf[p:], re.M)
        if not m or int(m.group(3)) != 42 or int(m.group(4)) != 1:
            raise AssertionError("[170] eth0 TX counters wrong: %r"
                                 % s.buf[p:][-260:])

    ctx.check(170, gate_170_name(), check_170)

    def check_171():
        # Escape to the host, then dump the guest image bytes: the arm64
        # Image magic 'ARMd' at 0x58080038 (Linux slot).
        recipes.escape_pump(s, "console -> host (escape)", timeout_s=30.0)
        p = ctx.mark()
        _line(s, "gdump 58080038 4")
        s.expect("0000000000000041 0000000000000052", 90.0, start=p)
        if b"000000000000004d 0000000000000064" not in s.buf[p:]:
            raise AssertionError("[171] arm64 Image magic 'ARMd' wrong: %r"
                                 % s.buf[p:][-200:])

    ctx.check(171, gate_171_name(), check_171)


def gate_168_name():
    return ("vm2 virtio_net bind: eth0 registered (Linux driver via "
            "second virtio-mmio window)")


def gate_169_name():
    return ("vm2 F_MAC negotiation: config MAC read (feature bit + config "
            "space face)")


def gate_170_name():
    return ("vm2 device-level TX: 42 bytes / 1 packet (xmit->kick->desc->"
            "used->netdev counter; RX=0=ARP-echo-drop boundary documented)")


def gate_171_name():
    return ("host gdump forensics: guest RAM hexdump (arm64 Image magic "
            "'ARMd') — batch C second station")


@gate(181, "coredump: wild VA -> ELF core file -> EL0 coreparse "
           "(pid/cursig/pc/esr/far/frames from NT notes)", fixture=None,
      scenario="coredump")
def gate_181(ctx):
    """Executed by scenario_coredump."""


@gate(182, "coredump: gcore hex reassembly -> host-side independent ELF "
           "parse (ET_CORE/EM_AARCH64/PT_NOTE/NT_PRSTATUS, pid match) — "
           "batch E deep-water station", fixture=None, scenario="coredump")
def gate_182(ctx):
    """Executed by scenario_coredump."""


@scenario("coredump", "vm0", (181, 182))
def scenario_coredump(ctx):
    s = ctx.s
    # One crash produces the core file both gates consume; the pid is
    # dynamic (legacy parsed it from the dump line).
    state = {"pid": ""}
    p_crash = ctx.mark()
    _line(s, "crashdemo")
    s.expect("crash: core dumped /var/core/core.", 120.0, start=p_crash)
    m = re.search(rb"core dumped /var/core/core\.(\d+)", s.buf[p_crash:])
    if m:
        state["pid"] = m.group(1).decode()
    s.expect("crash: reaped code=139 ok", 60.0, start=p_crash)
    s.expect("crashdemo: done", 45.0, start=p_crash)

    def check_181():
        m2 = re.search(rb"core dumped /var/core/core\.(\d+)",
                       s.buf[p_crash:])
        if not m2:
            raise AssertionError("[181] core dump line unparseable")
        core_pid = m2.group(1).decode()
        s.expect("crash: reaped code=139 ok", 60.0, start=p_crash)
        s.expect("crashdemo: done", 45.0, start=p_crash)
        p = ctx.mark()
        _line(s, "run /bin/coreparse /var/core/core.%s" % core_pid)
        s.expect(("coredump: pid=%s cursig=11 pc=0x"
                  % core_pid).encode(), 60.0, start=p)
        s.expect("far=0x4f000000", 45.0, start=p)
        s.expect("coredump: frames=1 0x", 45.0, start=p)

    ctx.check(181, gate_181_name(), check_181)

    def check_182():
        core_pid = state["pid"]
        if not core_pid:
            raise AssertionError("[182] core pid unavailable")
        p = ctx.mark()
        _line(s, "gcore /var/core/core.%s" % core_pid)
        s.expect("gcore: done ", 90.0, start=p)
        core = bytearray()
        for ln in s.buf[p:].split(b"\n"):
            ln = ln.strip(b"\r")
            if not ln.startswith(b"gcore: "):
                continue
            body = ln[len(b"gcore: "):]
            parts = body.split(b":", 1)
            if len(parts) != 2:
                continue                       # "gcore: done N bytes"
            try:
                off = int(parts[0])
            except ValueError:
                continue
            if off != len(core):
                continue                       # 段序对齐(防串行错位)
            for h in parts[1].split():
                core.append(int(h, 16))
        if core[0:4] != b"\x7fELF":
            raise AssertionError("[182] reassembled core bad magic (%r)"
                                 % bytes(core[0:6]))
        et = core[16] | (core[17] << 8)
        em = core[18] | (core[19] << 8)
        if et != 4 or em != 183:
            raise AssertionError("[182] e_type/e_machine %d/%d != 4/183"
                                 % (et, em))
        phoff = int.from_bytes(core[24:32], "little")
        phentsize = core[54] | (core[55] << 8)
        phnum = core[56] | (core[57] << 8)
        if phoff != 64 or phentsize != 56 or phnum != 1:
            raise AssertionError("[182] phdr header wrong")
        if int.from_bytes(core[64:68], "little") != 4:
            raise AssertionError("[182] phdr[0] not PT_NOTE")
        npos = 120
        namesz = int.from_bytes(core[npos:npos + 4], "little")
        descsz = int.from_bytes(core[npos + 4:npos + 8], "little")
        ntype = int.from_bytes(core[npos + 8:npos + 12], "little")
        # Legacy-verbatim stale constant (module notes): the serializer
        # writes sizeof(ce_prstatus)=368, not 376.
        if ntype != 1 or namesz != 5 or descsz != 376:
            raise AssertionError("[182] NT_PRSTATUS header wrong "
                                 "(%d/%d/%d)" % (ntype, namesz, descsz))
        dpos = npos + 12 + 8
        si = int.from_bytes(core[dpos:dpos + 4], "little")
        # Legacy-verbatim offset (module notes): cursig is at desc+12;
        # desc+14 is pad0.
        cursig = int.from_bytes(core[dpos + 14:dpos + 16], "little")
        prpid = int.from_bytes(core[dpos + 16:dpos + 20], "little")
        if si != 11 or cursig != 11 or str(prpid) != core_pid:
            raise AssertionError("[182] prstatus sig/pid wrong "
                                 "(%d/%d/%d vs %s)"
                                 % (si, cursig, prpid, core_pid))

    ctx.check(182, gate_182_name(), check_182)


def gate_181_name():
    return ("coredump: wild VA -> ELF core file -> EL0 coreparse "
            "(pid/cursig/pc/esr/far/frames from NT notes)")


def gate_182_name():
    return ("coredump: gcore hex reassembly -> host-side independent ELF "
            "parse (ET_CORE/EM_AARCH64/PT_NOTE/NT_PRSTATUS, pid match) — "
            "batch E deep-water station")