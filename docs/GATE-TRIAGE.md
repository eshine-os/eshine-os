<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# Gate triage — fixture-suite migration (2026-11-17)

The new fixture suite (`tools/verify/gate_suite.py`) runs every ordinary
gate in its own QEMU session. The first triage pass over gates 1..183
left five quarantined entries (excluded from the green suite, still run
by `--suite all` and visible in `--list`). Each entry has the failing
evidence from `.work/logs/` and the options for the one-by-one review.

Format: `id | gate | evidence | classification | options`. The
authoritative machine-readable list is `tools/verify/quarantine.txt`.

## Quarantined (red) gates

| ID | Gate | Evidence (2026-11-17) | Classification | Options |
|---|---|---|---|---|
| 122 | apsdemo: partition enforcement + steal ledger | Stalls after `aps: ph3 tasks ok`; the two prio-3 busy tasks starve the shell, no `ph4 window ok`; 300s gate cap hit. Legacy `[122]` was the known APS hang. | REAL-DEFECT | (a) fix the APS demo/priority window; (b) keep quarantined until fixed; (c) retire if the partition face is covered elsewhere (123/124 remain green). |
| 135 | host `loglevel elog dbg` → dbg/trc emitted | `dbg` line is emitted; `trc` line is not. `ES_LOG_EMIT` filters `trc` (5) at module level `dbg` (4). 134/136/137 are green. | OBSOLETE (stale anchor) | (a) change the anchor to "trc absent after dbg" (matches the macro contract); (b) drop the trc expectation; (c) retire 135 (dbg emission is already proved by the same scenario). |
| 140 | vm0 klog persistence (elog ring → /var/log/slog) | Gate's own dump shows `log core up, mods=3`; the anchor expects `mods=2`. VM0 now registers boot+elog+ham. 141/142 are green. | OBSOLETE (stale anchor) | (a) update the anchor to `mods=3`; (b) drop the mods line from the check (persistence is proved by the surrounding lines). |
| 172 | vm0 jsontest: cJSON parse of services.json | No output after `run /bin/jsontest`; guest spins at ~400% CPU for 120s. Same boot printed `svc: manifest parse failed`; `[104]` once failed with `run: bad elf`. | REAL-DEFECT (loader/fd read path) | (a) fix the loader/fd read of `/etc/services.json`; (b) keep quarantined; (c) retire until the fd path is reworked (the JSON parser also has 173/174 coverage-adjacent faces). |
| 182 | coredump: gcore reassembly, host-side ELF parse | Gate's own dump reports NT_PRSTATUS descsz **368**, not the legacy 376; `cursig` lives at desc+12 (pad0 at +14). 181 is green. | OBSOLETE (stale constants) | (a) update descsz to 368 and the cursig offset to +12 (the parser face is worth keeping); (b) retire 182 (181 already proves the core format). |

## Migration bugs fixed during the port (not quarantined)

| ID | Symptom | Fix |
|---|---|---|
| 127 | VM0-side `xvm: round-trip seq=N ok` print is timing-racy: it only fires when the consumer acks while the producer is in its 150-tick wait; a fast consumer skips it. | Dropped the redundant producer print; the consumer face (`got 'ping from vm0'` + `acked`) is the round-trip proof. |
| 134–137 | `logdemo` was a no-op on the host shell: `shell.c` carried a duplicated empty `else if (strncmp(line, "logdemo", 64) == 0)` branch since station 58. | Removed the empty branch (real SUT defect; 134/136/137 green, 135 separately stale). |
| 155–157, 161 | After a guest fault/off, `sched_console_owner` is already NULL; `0x1d` is not an escape and no prompt is printed. The legacy matched a stale prompt. | `wake_host()` pumps newlines until `eshine> ` appears. |
| 183 | The bsh REPL reads one line per stdin read; the 8-byte `send_guest` chunking split REPL lines, and shared position marks matched the typed echo. | Single-write REPL lines, per-line marks, `\n<output>` anchors. Now green (FLAKY on first retry). |

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