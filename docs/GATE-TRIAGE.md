<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# Gate triage — fixture-suite migration (2026-11-17)

The new fixture suite (`tools/verify/gate_suite.py`) runs every ordinary
gate in its own QEMU session. The first triage pass over gates 1..183
left five quarantined entries; the cloud runs added 173, 103 and 141,
and the scheduler atomic-switch fix (below) resolved that whole
fd file-read family (103/141/172/173). Four entries remain quarantined
(excluded from the green suite, still run by `--suite all` and visible
in `--list`). Each entry has the failing evidence from `.work/logs/`
and the options for the one-by-one review.

Format: `id | gate | evidence | classification | options`. The
authoritative machine-readable list is `tools/verify/quarantine.txt`.

## Quarantined (red) gates

| ID | Gate | Evidence (2026-11-17) | Classification | Options |
|---|---|---|---|---|
| 122 | apsdemo: partition enforcement + steal ledger | Stalls after `aps: ph3 tasks ok`; the two prio-3 busy tasks starve the shell, no `ph4 window ok`; 300s gate cap hit. Legacy `[122]` was the known APS hang. | REAL-DEFECT | (a) fix the APS demo/priority window; (b) keep quarantined until fixed; (c) retire if the partition face is covered elsewhere (123/124 remain green). |
| 135 | host `loglevel elog dbg` → dbg/trc emitted | `dbg` line is emitted; `trc` line is not. `ES_LOG_EMIT` filters `trc` (5) at module level `dbg` (4). 134/136/137 are green. | OBSOLETE (stale anchor) | (a) change the anchor to "trc absent after dbg" (matches the macro contract); (b) drop the trc expectation; (c) retire 135 (dbg emission is already proved by the same scenario). |
| 140 | vm0 klog persistence (elog ring → /var/log/slog) | Gate's own dump shows `log core up, mods=3`; the anchor expects `mods=2`. VM0 now registers boot+elog+ham. 141/142 are green. | OBSOLETE (stale anchor) | (a) update the anchor to `mods=3`; (b) drop the mods line from the check (persistence is proved by the surrounding lines). |
| 182 | coredump: gcore reassembly, host-side ELF parse | Gate's own dump reports NT_PRSTATUS descsz **368**, not the legacy 376; `cursig` lives at desc+12 (pad0 at +14). 181 is green. | OBSOLETE (stale constants) | (a) update descsz to 368 and the cursig offset to +12 (the parser face is worth keeping); (b) retire 182 (181 already proves the core format). |

## Migration bugs fixed during the port (not quarantined)

| ID | Symptom | Fix |
|---|---|---|
| 127 | VM0-side `xvm: round-trip seq=N ok` print is timing-racy: it only fires when the consumer acks while the producer is in its 150-tick wait; a fast consumer skips it. | Dropped the redundant producer print; the consumer face (`got 'ping from vm0'` + `acked`) is the round-trip proof. |
| 134–137 | `logdemo` was a no-op on the host shell: `shell.c` carried a duplicated empty `else if (strncmp(line, "logdemo", 64) == 0)` branch since station 58. | Removed the empty branch (real SUT defect; 134/136/137 green, 135 separately stale). |
| 155–157, 161 | After a guest fault/off, `sched_console_owner` is already NULL; `0x1d` is not an escape and no prompt is printed. The legacy matched a stale prompt. | `wake_host()` pumps newlines until `eshine> ` appears. |
| 183 | The bsh REPL reads one line per stdin read; the 8-byte `send_guest` chunking split REPL lines, and shared position marks matched the typed echo. | Single-write REPL lines, per-line marks, `\n<output>` anchors. Now green (FLAKY on first retry). |

## Resolved: fd file-read spin family (2026-11-17)

Gates **103, 141, 172 and 173** shared one silent-wedge signature: a
`run /bin/...` that reads a file prints nothing and the gate times out
(`cat /tmp/init40`, `cat /var/log/slog`, cJSON reading
`/etc/services.json`, ztest slurping the slog). A local probe
(`run /bin/busybox` echo/cat cycles in one boot) reproduced it within
3-5 cycles and the QEMU gdbstub showed four vCPUs in the EL2 pump while
the guest sat parked in WFI. A physical task-pool dump
(`monitor xp` at `pool` VA 0x4018aa20 + 0x10000000) caught the loader
child in `state=RUN` with an expired `pend_deadline`, absent from every
queue and from every CPU's `rt_cur`.

Root cause: `rt_pick_ready` marks the picked task RUN and unlinks it
under the queue lock, but the cooperative path then returned to
`rt_sched` with IRQs enabled; a tick landing between pick and
`rt_switch_to` let the IRQ exit switch to a different task, leaving the
picked one stranded (the failure mode `vec.S` itself calls "RUN 标记
永失 = 无声楔"). If the interrupted switch belonged to a blocking task,
the stranded task stayed lost until that task woke — which for the fd
read path could be never.

Fix: `rt_sched` and `rt_task_exit` mask DAIF across the whole
pick→switch window and pass the caller's DAIF into `rt_switch_to` as a
second argument; `vec.S` builds the resumption psr from that value
instead of `mrs daif`, so the switch semantics are unchanged. A
60-cycle × 4-boot stress probe that hung on every boot before now runs
clean with zero panics, and `make verify-fast` (smoke) is 18/18.

## CI run findings (2026-11-17)

Run **44043674022** (first cloud green run) came back `gates=172 pass=158
fail=9 skip=1 blocked=4 flaky=24` with
`red=[103,104,106,141,143,144,145,158,159,160,161,173,183]`. Two causes,
one deterministic and one environmental:

- **prepare order (fixed).** `scripts/ci-gates.sh prepare` ran `make`
  before `scripts/bootstrap.sh --fetch`, so the kernel embedded a
  `rtos/ramfs.cpio` built before busybox existed
  (`gen_ramfs: WARN /bin/busybox missing`). Every busybox gate then
  failed instantly with `run: open failed` (103/104/106/141/143/144/145,
  and 183 through its `/bin/busybox ash` row, plus one 106 scenario
  attempt). Fixed by fetching before make, and a hard assertion now
  refuses to run the suite if `cpio -it rtos/ramfs.cpio` has no
  `bin/busybox`.
- **loader/fd spin (open, 173 quarantined).** 173 spun 3/3 in CI and
  passes 5/5 locally; 158-161 blocked 4/4 as BLOCKED on VM0 boot attach
  (`wake prompt '\r\nrtos> ' never appeared`) in the same window, then
  pass 3/3 locally. Both are the loader/fd spin family already tracked by
  172, exposed by the cloud runner. 158-161 stay in green for now (the
  gate logic is green; a fixture BLOCKED is retried on a fresh boot) and
  move to quarantine if they recur.

Run **44060155659** (after the prepare fix) came back `gates=171
pass=170 fail=1 flaky=30 red=[103]` in 1440s. 158-161 were green this
time (confirming the BLOCKED block was environmental), and the only red
was 103 on its second `run` — the fd file-read spin above, quarantined.

Run **44064626618** (103 quarantined) came back `gates=170 pass=168
fail=1 skip=1 flaky=39 red=[141]` in 1904s: the same fd file-read spin
moved to 141's `cat /var/log/slog`; 142 was SKIP as its scenario
neighbour. 141 is now quarantined too.

Run **44071218150** (141 quarantined) came back `gates=169 pass=168
fail=0 blocked=1 flaky=32 red=[92]` in 1982s. 92 BLOCKED on the known
all-space flood family at console attach, four fixture attempts in a
row (9.5s total). No gate logic failed. CI now uses six boot attempts
(`CI_BOOT_RETRIES=6`); a flood retry is immediate, so the tail is cheap.

## Fixture-level flake families (FLAKY, not red)

These pass on a retry and are reported as `FLAKY`; they are SUT boot
robustness issues, not gate logic:

- zero-byte chardev boots (fixture restarts QEMU, 3 attempts);
- the all-space flood (`wvtrip: all-space chunk`);
- RX wake lost byte at console attach (pump every ~1.5s, 25s window);
- VM0 init stalls after `srv: pm bound stdio` (no `init: inittab processed`);
- post-death console wake (now `wake_host`);
- service/init respawn storms after attach (inittab re-runs, `beater
  restart` loop) can bury the shell prompt. `[81]` failed the full green
  run this way (it passed in the isolated block run); CI now uses three
  gate retries and four boot attempts.
- user-space loader/fd spin: `run /bin/...` prints nothing and the guest
  spins (~400% CPU). Seen on `[172]` (quarantined) and `[141]` (slog
  persistence, FAIL/SKIP in the green run); `[103]`/`[104]`/`[143]` hit
  it once each but passed on retry. Scenarios now retry a failed check
  like ordinary gates.

Observed on a load-12 host: nine FLAKY gates in the 1..77 block, and
several in the 78..183 blocks. A quiet window or the cloud runner should
reproduce far less; `[128]` hit three init stalls in a row today and is
not quarantined — its logic is green when the fixture boots.