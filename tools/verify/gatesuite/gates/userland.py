# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Julian Chu
"""VM0 EL0 userland gates 78..126 (legacy stations 28..46).

Every gate gets a fresh `vm0` fixture (VM0 booted, attached, rtos> prompt)
except the stateful families:

* [78] needs only VM0.  The legacy session happened to own the console
  from VM2 and hot-keyed back first; that console choreography belongs
  to the hotkey gate [129], not to the utest assertion, so the port
  starts from a fresh `vm0` fixture and drops the prefix (dedupe).
* [105]/[106]: `init-dualworld` scenario on `multi` (the only VM0+VM2
  world).  105 owns the inittab sysinit/respawn evidence; 106 owns the
  two-world md5sum comparison against the host's busybox bytes.
* [125]/[126]: `crash-ring` scenario on `vm0`; 126 consumes the crash
  ring record that 125's crashdemo leaves behind.

Artifact gap: the musl userland programs exercised here (/bin/hello,
/bin/pt, /bin/beat, /bin/initmark) are packed into rtos.bin by the ramfs
build, so only the "elf" tag can preflight them; the "busybox" tag is
declared where tools/ramfs/bin/busybox is the payload under test.
"""

from __future__ import annotations

import hashlib
import re
import time

from .. import artifacts
from ..registry import gate, scenario, scenario_gate

NAMES = {
    78: "utest: EL0 syscall face (getpid/ENOSYS/sysinfo;SVC channel "
        "itself = EL0 proof)",
    79: "forkdemo: COPY_UCONTEXT full lifecycle (child+exit42+wait4 reap)",
    80: "mmapdemo: demand paging (VMA lazy zero-fill + rw + munmap)",
    81: "exedemo: EXEC_SETUP frame rewrite (pc/sp/x0; no image read by "
        "kernel)",
    82: "ipcdemo: 3-phase msg + priority propagation (reply 42)",
    83: "pulsedemo: async pulses (5 fired/drained)",
    84: "abortdemo: sender timeout + UNBLOCK pulse (QNX mirror)",
    85: "fddemo: fd=channel triple (open/read/write/close via resmgr)",
    86: "futexdemo: WAIT/WAKE faces + real pair + timeout block",
    87: "sigdemo: cross-proc kill -> ERESTARTSYS restart (elr-4 rewind, "
        "x0 kept); handler + sigreturn",
    88: "vm0: musl hello via user-space loader (dual-world left wing)",
    89: "vm0: pthread 4-thread join via clone(220) (musl TLS+CLEAR_TID "
        "contract)",
    90: "shmdemo: kernel shm segment, fixed xspace VA, fork shared not "
        "copied",
    91: "msgseg: single-copy window (1KB via shm, 8B pulse carries the "
        "reference)",
    92: "mqdemo: named mqueue, priority order 5-3-1, no-wait face",
    93: "pipedemo: fd pipe pair via resmgr, bytes + EOF after writer close",
    94: "longdemo: 1024B long message + MsgRead mirror "
        "(read-held-by-offset)",
    95: "sigqdemo: sigqueue value+tid targeting, SA_RESTORER, "
        "default-terminate",
    96: "wrdemo: ramfs COW write + append + SEEK_END + unlink lifecycle",
    97: "lsdemo: READDIR live-node enumeration via /dev/ramfs meta-name",
    98: "fblkdemo: virtio-blk sector RMW via hypervisor disk backend",
    99: "rngdemo: /dev/random 16B x2 differ + getrandom same mixer",
    100: "clockdemo: clock_gettime REAL/MONO + BOOT_EPOCH wall face",
    101: "procdemo: /proc task table (READDIR second consumer)",
    102: "slogdemo: slogd ring + warn console policy",
    103: "busybox echo + cat inittab marker (musl static, argc/argv "
         "loader)",
    104: "busybox ls (getdents64 one-per-call) + date (clock face)",
    105: "init inittab: sysinit marker + respawn gate-shot",
    106: "dual-world: same busybox bytes on vm0+vm2 (md5 equal)",
    107: "capdemo: CAP_VFS fail-closed (open+getdents64) + self-inc-out",
    108: "capdemo2: CAP_SPAWN fail-closed (fork denied)",
    109: "rlimitdemo: max_fd self-lower -> 4th open EMFILE",
    110: "uiddemo: uuid identities differ, nonzero (fork=new)",
    111: "fail-closed regression: shell keeps CAP_ALL (whole battery = "
         "implicit face)",
    112: "scheddemo: setscheduler/getparam/yield + rr_get_interval "
         "(sched mgmt face)",
    113: "uname: utsname six fields (160 true no.)",
    114: "pgiddemo: setsid self-session + pgid inherit",
    115: "sigdemo2: sigpending/sigaltstack/sigsuspend (mask restore via "
         "sigreturn)",
    116: "timerdemo: posix timer SIGEV_THREAD_ID periodic + delete + "
         "itimer",
    117: "robustdemo: exit-path robust chain walk wakes futex waiter "
         "(holder died)",
    118: "cloexecdemo: F_SETFD/F_GETFD roundtrip + parent fd survives",
    119: "calltrace: FP chain walk (panic hook + demo face)",
    120: "polldemo: ppoll timeout/POLLIN/drain (pipe POLL op)",
    121: "ps blk column: wait-source attribution live",
    122: "apsdemo: partition enforcement fires (throttle ledger) + steal "
         "ledger; share printed for humans",
    123: "mprtdemo: memory partition cap (mmap/fork denied, munmap "
         "uncharge, exit zero)",
    124: "parts: partition tables (k_aps_show mirror)",
    125: "crashdemo: wild user VA -> proc exit 139 (was system panic), "
         "parent still alive",
    126: "crashinfo: crash ring record (hw triple + regs + user frame "
         "chain)",
}

MOTD_MD5 = b"f6f062e1b6748d300d9e6550b7e3ba53"   # tools/ramfs/etc/motd


def _line(ctx, text: str) -> int:
    """Send one shell line; return the mark that anchors its output."""
    p = ctx.mark()
    ctx.s.send(text + "\n")
    return p


@gate(78, NAMES[78], fixture="vm0", timeout_s=150.0)
def gate_78(ctx):
    s = ctx.s
    # VM0 fixture is already attached (rtos> prompt); the utest assertion
    # is the whole gate (the legacy hotkey prefix was console state, not
    # part of this face - [129] owns hotkey switching).
    p = _line(ctx, "utest")
    s.expect("utest: pid=", 30.0, start=p)
    s.expect("EL0 syscall OK", 30.0, start=p)


@gate(79, NAMES[79], fixture="vm0", timeout_s=150.0)
def gate_79(ctx):
    s = ctx.s
    p = _line(ctx, "forkdemo")
    s.expect("fork: child alive", 45.0, start=p)
    s.expect("fork: reaped pid=", 45.0, start=p)
    s.expect("code=42", 30.0, start=p)


@gate(80, NAMES[80], fixture="vm0", timeout_s=140.0)
def gate_80(ctx):
    s = ctx.s
    p = _line(ctx, "mmapdemo")
    s.expect("mmap: lazy zero+rw ok", 45.0, start=p)
    s.expect("mmapdemo: unmapped", 30.0, start=p)
    s.expect("rtos> ", 30.0, start=p)


@gate(81, NAMES[81], fixture="vm0", timeout_s=110.0)
def gate_81(ctx):
    s = ctx.s
    p = _line(ctx, "exedemo")
    s.expect("exec: phase2 entered arg=42", 45.0, start=p)
    s.expect("rtos> ", 30.0, start=p)


@gate(82, NAMES[82], fixture="vm0", timeout_s=220.0)
def gate_82(ctx):
    s = ctx.s
    p = _line(ctx, "ipcdemo")
    s.expect("ipcdemo: reply 42", 120.0, start=p)
    s.expect("dt=", 30.0, start=p)
    s.expect("rtos> ", 30.0, start=p)


@gate(83, NAMES[83], fixture="vm0", timeout_s=220.0)
def gate_83(ctx):
    s = ctx.s
    p = _line(ctx, "pulsedemo")
    s.expect("pulsedemo: fired 5", 120.0, start=p)
    s.expect("pulse: drained 5 sum=10", 60.0, start=p)


@gate(84, NAMES[84], fixture="vm0", timeout_s=220.0)
def gate_84(ctx):
    s = ctx.s
    p = _line(ctx, "abortdemo")
    s.expect("abortdemo: send timed out (-110)", 120.0, start=p)
    s.expect("abort: unblock pulse value=0x", 60.0, start=p)


@gate(85, NAMES[85], fixture="vm0", timeout_s=190.0)
def gate_85(ctx):
    s = ctx.s
    p = _line(ctx, "fddemo")
    s.expect("fddemo: via fd tty", 60.0, start=p)
    s.expect("fd=channel triple live", 30.0, start=p)
    s.expect("fddemo: motd 67 bytes", 30.0, start=p)
    s.expect("fddemo: version 5 bytes", 30.0, start=p)


@gate(86, NAMES[86], fixture="vm0", timeout_s=260.0)
def gate_86(ctx):
    s = ctx.s
    p = _line(ctx, "futexdemo")
    s.expect("futexdemo: mismatch -> -EAGAIN ok", 60.0, start=p)
    s.expect("futexdemo: wake no-waiters -> 0 ok", 30.0, start=p)
    s.expect("futexdemo: poke pair r=0 dt=", 60.0, start=p)
    s.expect("futexdemo: timeout r=-110 dt=", 60.0, start=p)


@gate(87, NAMES[87], fixture="vm0", timeout_s=220.0)
def gate_87(ctx):
    s = ctx.s
    p = _line(ctx, "sigdemo")
    s.expect("sigdemo: child waiting", 45.0, start=p)
    s.expect("sig: handler ran sig=10", 60.0, start=p)
    s.expect("sig: futex restarted -> -EAGAIN ok", 30.0, start=p)
    s.expect("sigdemo: reaped code=7 ok", 45.0, start=p)


@gate(88, NAMES[88], fixture="vm0", timeout_s=240.0)
def gate_88(ctx):
    s = ctx.s
    p = _line(ctx, "run /bin/hello")
    # Observed ~2-4s; the legacy 900s was a serial-suite budget, not
    # this gate's scale (a hung loader must fail fast in CI).
    s.expect("musl: hello from /bin/hello", 120.0, start=p)
    s.expect("musl: fmt proof 6*7=42", 30.0, start=p)
    s.expect("run: exited code=0", 60.0, start=p)


@gate(89, NAMES[89], fixture="vm0", timeout_s=300.0)
def gate_89(ctx):
    s = ctx.s
    p = _line(ctx, "run /bin/pt")
    s.expect("pthread: 4 threads joined, counter=10 (want 10)",
             180.0, start=p)
    s.expect("pthread: self=", 60.0, start=p)
    s.expect("run: exited code=0", 60.0, start=p)


@gate(90, NAMES[90], fixture="vm0", timeout_s=160.0)
def gate_90(ctx):
    s = ctx.s
    p = _line(ctx, "shmdemo")
    s.expect("shmdemo: va=0x41000000", 45.0, start=p)
    s.expect("shmdemo: fork shared ok, p2=305419896", 45.0, start=p)
    s.expect("shmdemo: destroyed ok", 30.0, start=p)


@gate(91, NAMES[91], fixture="vm0", timeout_s=110.0)
def gate_91(ctx):
    s = ctx.s
    p = _line(ctx, "msgseg")
    s.expect("msgseg: 1024B via window, wire=8B ok", 45.0, start=p)
    s.expect("msgseg: destroyed ok", 30.0, start=p)


@gate(92, NAMES[92], fixture="vm0", timeout_s=160.0)
def gate_92(ctx):
    s = ctx.s
    p = _line(ctx, "mqdemo")
    s.expect("mqdemo: empty no-wait -110 ok", 45.0, start=p)
    s.expect("mqdemo: order 5 3 1 ok", 45.0, start=p)
    s.expect("mqdemo: unlinked ok", 30.0, start=p)


@gate(93, NAMES[93], fixture="vm0", timeout_s=140.0)
def gate_93(ctx):
    s = ctx.s
    p = _line(ctx, "pipedemo")
    s.expect("pipedemo: got 'through the pipe' ok", 45.0, start=p)
    s.expect("pipedemo: eof ok", 30.0, start=p)
    s.expect("pipedemo: closed ok", 30.0, start=p)


@gate(94, NAMES[94], fixture="vm0", timeout_s=160.0)
def gate_94(ctx):
    s = ctx.s
    p = _line(ctx, "longdemo")
    s.expect("long: 1024B via long slot ok", 45.0, start=p)
    s.expect("long: msg_read mirror ok", 30.0, start=p)
    s.expect("longdemo: reply 42 ok", 45.0, start=p)


@gate(95, NAMES[95], fixture="vm0", timeout_s=240.0)
def gate_95(ctx):
    s = ctx.s
    p = _line(ctx, "sigqdemo")
    s.expect("sigq: queued val=48879", 45.0, start=p)
    s.expect("sigq: handler sig=10 val=48879", 45.0, start=p)
    s.expect("sigq: resumed", 30.0, start=p)
    s.expect("sigq: default terminate 140 ok", 45.0, start=p)
    s.expect("sigq: empty poll -11 ok", 30.0, start=p)


@gate(96, NAMES[96], fixture="vm0", timeout_s=210.0)
def gate_96(ctx):
    s = ctx.s
    p = _line(ctx, "wrdemo")
    s.expect("wrdemo: wrote 9 ok", 45.0, start=p)
    s.expect("wrdemo: append 7 ok", 30.0, start=p)
    s.expect("wrdemo: read 'ESHINE39-APPEND' ok", 30.0, start=p)
    s.expect("wrdemo: eof after END ok", 30.0, start=p)
    s.expect("wrdemo: unlinked, reopen ENOENT ok", 30.0, start=p)


@gate(97, NAMES[97], fixture="vm0", timeout_s=90.0)
def gate_97(ctx):
    s = ctx.s
    p = _line(ctx, "lsdemo")
    s.expect("lsdemo: entries ok, /etc/motd in list", 45.0, start=p)


@gate(98, NAMES[98], fixture="vm0", timeout_s=90.0)
def gate_98(ctx):
    s = ctx.s
    p = _line(ctx, "fblkdemo")
    s.expect("fblkdemo: sector rw ok", 45.0, start=p)


@gate(99, NAMES[99], fixture="vm0", timeout_s=110.0)
def gate_99(ctx):
    s = ctx.s
    p = _line(ctx, "rngdemo")
    s.expect("rng: two reads differ", 45.0, start=p)
    s.expect("rng: getrandom 32B", 30.0, start=p)


@gate(100, NAMES[100], fixture="vm0", timeout_s=110.0)
def gate_100(ctx):
    s = ctx.s
    p = _line(ctx, "clockdemo")
    s.expect("clock: mono advance ok", 45.0, start=p)
    s.expect("clock: wall sec=1793491", 30.0, start=p)


@gate(101, NAMES[101], fixture="vm0", timeout_s=90.0)
def gate_101(ctx):
    s = ctx.s
    p = _line(ctx, "procdemo")
    s.expect("shell-seen", 45.0, start=p)


@gate(102, NAMES[102], fixture="vm0", timeout_s=110.0)
def gate_102(ctx):
    s = ctx.s
    p = _line(ctx, "slogdemo")
    s.expect("slog: dumped 3 lines", 45.0, start=p)
    s.expect("slog: [W] slog-two-warn", 30.0, start=p)


@gate(103, NAMES[103], fixture="vm0", timeout_s=240.0,
      artifacts=("elf", "busybox"))
def gate_103(ctx):
    s = ctx.s
    p_run = ctx.mark()
    s.send("run /bin/busybox echo ESHINE40-BB\n")
    # Drain the typed echo line first: it contains the token, so the real
    # output wait must start past it (legacy r48 anchor shift).
    s.expect("echo ESHINE40-BB\r\n", 60.0, start=p_run)
    p_out = ctx.mark()
    s.expect("ESHINE40-BB", 120.0, start=p_out)
    s.expect("run: exited code=0", 60.0, start=p_out)
    p_cat = ctx.mark()
    s.send("run /bin/busybox cat /tmp/init40\n")
    s.expect("ESHINE40-INIT", 120.0, start=p_cat)


@gate(104, NAMES[104], fixture="vm0", timeout_s=240.0,
      artifacts=("elf", "busybox"))
def gate_104(ctx):
    s = ctx.s
    p = ctx.mark()
    s.send("run /bin/busybox ls -1 /etc\n")
    s.expect("inittab", 120.0, start=p)
    s.expect("motd", 60.0, start=p)
    p = ctx.mark()
    s.send("run /bin/busybox date -u\n")
    s.expect("UTC 2026", 120.0, start=p)


# [105]/[106] are executed by the `init-dualworld` scenario.  Direct
# @gate registration (instead of scenario_gate) is needed to declare the
# busybox artifact for 105 and the busybox+linux artifacts for 106.
@gate(105, NAMES[105], fixture=None, scenario="init-dualworld",
      artifacts=("elf", "busybox"), timeout_s=400.0)
def gate_105(ctx):
    """Executed by scenario_init_dualworld (inittab sysinit/respawn)."""


@gate(106, NAMES[106], fixture=None, scenario="init-dualworld",
      artifacts=("elf", "linux", "busybox"), timeout_s=300.0)
def gate_106(ctx):
    """Executed by scenario_init_dualworld (vm0 vs vm2 md5sum)."""


@scenario("init-dualworld", "multi", (105, 106))
def scenario_init_dualworld(ctx):
    s = ctx.s
    # multi ends attached to VM2; take the console back to VM0 (same
    # hotkey prefix the legacy session used before this battery).
    p = ctx.mark()
    s.send(b"\x1e0")
    s.expect("console -> vm", 30.0, start=p)
    p = ctx.mark()
    s.send("\n")
    s.expect("rtos> ", 30.0, start=p)

    if 105 in ctx.selected:
        def check_105():
            # The sysinit marker is a boot-time event; scan the whole
            # buffer (legacy start=p3 dead-anchor fix from station 40).
            for needle in (b"init: sysinit /bin/initmark",
                           b"init: inittab processed",
                           b"initmark: wrote /tmp/init40"):
                if needle not in s.buf:
                    raise AssertionError(
                        "inittab sysinit marker missing: %r" % needle)
            p_r = ctx.mark()
            s.send("initrespawn\n")
            s.expect("init: respawn /bin/beat", 45.0, start=p_r)
            s.expect("respawn beat", 30.0, start=p_r)
            s.expect("exited code=42", 30.0, start=p_r)

        ctx.check(105, NAMES[105], check_105)

    if 106 in ctx.selected:
        def check_106():
            # vm0 side: busybox md5sum /etc/motd via the fd window.
            # Idempotent resend (legacy r26): one dirty transient on a
            # loaded boot is tolerated; the value itself is judged below.
            m0 = None
            for attempt in range(2):
                p_m = ctx.mark()
                s.send("run /bin/busybox md5sum /etc/motd\n")
                s.expect("  /etc/motd", 120.0, start=p_m)
                m0 = re.search(rb"([0-9a-f]{32})  /etc/motd", s.buf[p_m:])
                if m0 and m0.group(1) == MOTD_MD5:
                    break
                if attempt == 0:
                    time.sleep(0.3)
            if m0 is None:
                raise AssertionError("vm0 md5sum output missing")

            # vm2 side: the same busybox bytes under the real Linux
            # kernel (hotkey 0x1e2, console owner VM0 -> VM2).
            p_hk = ctx.mark()
            s.send(b"\x1e2")
            s.expect("console -> vm", 30.0, start=p_hk)
            time.sleep(1.0)
            p_bb = ctx.mark()
            s.send_guest("md5sum /bin/busybox\n")
            s.expect("  /bin/busybox", 90.0, start=p_bb)
            mb2 = re.search(rb"([0-9a-f]{32})  /bin/busybox",
                            s.buf[p_bb:])
            p_motd = ctx.mark()
            s.send_guest("md5sum /etc/motd\n")
            s.expect("  /etc/motd", 45.0, start=p_motd)
            m2h = re.search(rb"([0-9a-f]{32})  /etc/motd",
                            s.buf[p_motd:])
            p_back = ctx.mark()
            s.send(b"\x1e0")
            s.expect("console -> vm", 30.0, start=p_back)
            time.sleep(1.0)

            if mb2 is None or m2h is None:
                raise AssertionError(
                    "vm2 md5sum output missing (busybox=%r motd=%r)"
                    % (mb2 and mb2.group(1), m2h and m2h.group(1)))
            try:
                with open(artifacts.BB, "rb") as bf:
                    bb_hash = hashlib.md5(bf.read()).hexdigest().encode()
            except OSError as e:
                raise AssertionError(
                    "host busybox unreadable: %s" % e) from e
            if mb2.group(1) != bb_hash or m0.group(1) != m2h.group(1):
                raise AssertionError(
                    "dual-world hash mismatch (vm2=%s want=%s motd vm0=%s "
                    "vm2=%s)" % (mb2.group(1).decode(), bb_hash.decode(),
                                 m0.group(1).decode(), m2h.group(1).decode()))

        ctx.check(106, NAMES[106], check_106)


@gate(107, NAMES[107], fixture="vm0", timeout_s=180.0)
def gate_107(ctx):
    s = ctx.s
    p = _line(ctx, "capdemo")
    s.expect("cap: vfs dropped, open -EPERM ok", 45.0, start=p)
    s.expect("cap: getdents64 -EPERM ok", 30.0, start=p)
    s.expect("cap: self-inc-out (no add path) ok", 30.0, start=p)
    s.expect("capdemo: done", 30.0, start=p)


@gate(108, NAMES[108], fixture="vm0", timeout_s=110.0)
def gate_108(ctx):
    s = ctx.s
    p = _line(ctx, "capdemo2")
    s.expect("cap: spawn dropped, fork -EPERM ok", 45.0, start=p)
    s.expect("capdemo2: done", 30.0, start=p)


@gate(109, NAMES[109], fixture="vm0", timeout_s=110.0)
def gate_109(ctx):
    s = ctx.s
    p = _line(ctx, "rlimitdemo")
    s.expect("rlimit: 4th open -EMFILE ok", 45.0, start=p)
    s.expect("rlimitdemo: done", 30.0, start=p)


@gate(110, NAMES[110], fixture="vm0", timeout_s=150.0)
def gate_110(ctx):
    s = ctx.s
    p = _line(ctx, "uiddemo")
    s.expect("uuid: child differs nonzero ok", 45.0, start=p)
    s.expect("uuid: parent nonzero ok", 30.0, start=p)
    s.expect("uiddemo: done", 30.0, start=p)


@gate(111, NAMES[111], fixture="vm0", timeout_s=110.0)
def gate_111(ctx):
    s = ctx.s
    # Rerun of capdemo: the shell must still hold CAP_ALL (fail-closed
    # regression testimonial from the legacy battery).
    p = _line(ctx, "capdemo")
    s.expect("cap: shell CAP_ALL regression ok", 45.0, start=p)
    s.expect("capdemo: done", 30.0, start=p)


@gate(112, NAMES[112], fixture="vm0", timeout_s=110.0)
def gate_112(ctx):
    s = ctx.s
    p = _line(ctx, "scheddemo")
    s.expect("sched: set+get prio 7 ok", 45.0, start=p)
    s.expect("sched: rr slice 40ms ok", 30.0, start=p)


@gate(113, NAMES[113], fixture="vm0", timeout_s=90.0)
def gate_113(ctx):
    s = ctx.s
    p = _line(ctx, "unamedemo")
    s.expect("uname: eshine-os 27.04 aarch64 ok", 45.0, start=p)


@gate(114, NAMES[114], fixture="vm0", timeout_s=110.0)
def gate_114(ctx):
    s = ctx.s
    p = _line(ctx, "pgiddemo")
    s.expect("pgid: child inherits group ok", 45.0, start=p)
    s.expect("pgid: setsid self-leader ok", 30.0, start=p)


@gate(115, NAMES[115], fixture="vm0", timeout_s=180.0)
def gate_115(ctx):
    s = ctx.s
    p = _line(ctx, "sigdemo2")
    s.expect("sig2: pending bit ok", 45.0, start=p)
    s.expect("sig2: altstack reg ok", 30.0, start=p)
    s.expect("sig2: handler in own ctx ok", 30.0, start=p)
    s.expect("sig2: resumed -EINTR ok", 30.0, start=p)


@gate(116, NAMES[116], fixture="vm0", timeout_s=240.0)
def gate_116(ctx):
    s = ctx.s
    p = _line(ctx, "timerdemo")
    s.expect("timer: periodic fired ok", 90.0, start=p)
    s.expect("timer: delete ok", 30.0, start=p)
    s.expect("itimer: fired ok", 75.0, start=p)


@gate(117, NAMES[117], fixture="vm0", timeout_s=130.0)
def gate_117(ctx):
    s = ctx.s
    p = _line(ctx, "robustdemo")
    s.expect("robust: waiter woke after death ok", 90.0, start=p)


@gate(118, NAMES[118], fixture="vm0", timeout_s=150.0)
def gate_118(ctx):
    s = ctx.s
    p = _line(ctx, "cloexecdemo")
    s.expect("cloexec: fd clean ok", 45.0, start=p)
    s.expect("cloexec: bit roundtrip ok", 30.0, start=p)
    s.expect("cloexec: parent fd survives ok", 30.0, start=p)


@gate(119, NAMES[119], fixture="vm0", timeout_s=110.0)
def gate_119(ctx):
    s = ctx.s
    p = _line(ctx, "calltrace")
    s.expect("calltrace:", 45.0, start=p)
    s.expect("frames=", 30.0, start=p)


@gate(120, NAMES[120], fixture="vm0", timeout_s=520.0)
def gate_120(ctx):
    s = ctx.s
    p = _line(ctx, "polldemo")
    s.expect("poll: empty pipe times out ok", 180.0, start=p)
    s.expect("poll: POLLIN after write ok", 120.0, start=p)
    s.expect("poll: drained back to empty ok", 120.0, start=p)
    s.expect("polldemo: done", 60.0, start=p)


@gate(121, NAMES[121], fixture="vm0", timeout_s=160.0)
def gate_121(ctx):
    s = ctx.s
    p = _line(ctx, "ps")
    s.expect("blk sem", 120.0, start=p)


@gate(122, NAMES[122], fixture="vm0", timeout_s=1080.0)
def gate_122(ctx):
    s = ctx.s
    p = _line(ctx, "apsdemo")
    s.expect("aps: contend a=", 360.0, start=p)
    s.expect("A-throttled=", 120.0, start=p)
    s.expect("x ok", 60.0, start=p)
    s.expect("aps: solo a=", 270.0, start=p)
    s.expect("A-steals=", 90.0, start=p)
    s.expect("x ok", 60.0, start=p)
    s.expect("cpart demoA budget 20%", 60.0, start=p)


@gate(123, NAMES[123], fixture="vm0", timeout_s=540.0)
def gate_123(ctx):
    s = ctx.s
    p = _line(ctx, "mprtdemo")
    s.expect("mprt: mmap 16p ok", 180.0, start=p)
    s.expect("mprt: mmap -12 budget ok", 60.0, start=p)
    s.expect("mprt: fork denied by budget ok", 60.0, start=p)
    s.expect("mprt: mmap after munmap ok", 60.0, start=p)
    s.expect("mprtdemo: reaped code=0 ok", 60.0, start=p)
    s.expect("mpart mpdemo cur 0/16p", 60.0, start=p)


@gate(124, NAMES[124], fixture="vm0", timeout_s=130.0)
def gate_124(ctx):
    s = ctx.s
    # parts is a pure table dump; a fresh boot already holds the system
    # partition (apsdemo/mprtdemo free their partitions at exit).
    p = _line(ctx, "parts")
    s.expect("cpart system budget 100%", 90.0, start=p)


scenario_gate(125, NAMES[125], "crash-ring", suites=("green",))
scenario_gate(126, NAMES[126], "crash-ring", suites=("green",))


@scenario("crash-ring", "vm0", (125, 126))
def scenario_crash_ring(ctx):
    s = ctx.s
    p = ctx.mark()
    s.send("crashdemo\n")

    def crash_face():
        s.expect("crashdemo: child running", 120.0, start=p)
        s.expect("crash: reaped code=139 ok", 90.0, start=p)
        s.expect("crashdemo: done", 45.0, start=p)

    # crashdemo is an unconditional action: 126 needs the ring record
    # even when 125 is not selected.
    if 125 in ctx.selected:
        ctx.check(125, NAMES[125], crash_face)
    else:
        crash_face()

    if 126 in ctx.selected:
        p2 = ctx.mark()
        s.send("crashinfo\n")

        def check_126():
            s.expect("crash: ", 45.0, start=p2)
            s.expect("far 0x4f000000", 60.0, start=p2)
            s.expect("pc=0x", 45.0, start=p2)

        ctx.check(126, NAMES[126], check_126)