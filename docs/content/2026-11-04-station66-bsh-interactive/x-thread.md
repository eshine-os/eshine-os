# X 8 推(站 66 bsh 交互深水)

1/8
eshine-os station 66: the no-arg bsh deep dive. The "frozen" interactive shell turned out to be 90% design (a heartbeat-silence gate from station 24A) — and the real break was a 10-character condition in the pump's RX injection gate. #buildinpublic #debugging

2/8
Tooling first: we added the fourth lens — gdb-multiarch with source symbols, reading live structs straight: vm_table[0].vcpus[0].wait_state, task pool slots, scheduler queues, guest RAM. Everything below was decided by direct reads, not by theory.

3/8
The freeze autopsy: vm0's tick runs at 100Hz (rt_ticks and IRQ-27 counters both climb). The heartbeat's wake_tick advances in lockstep with rt_ticks. The task pool shows ash in BLOCK on the RX wait bit — the textbook posture of a shell waiting for input. Nothing is dead.

4/8
Then why the silent screen? Prime suspect: station 24A's design gate — the heartbeat only speaks when the shell task sleeps. In bsh mode the shell parent sleeps in wait4, not SLEEP; the heartbeat stays politely quiet. Design, not death.

5/8
The real break, once narrowed: the pump's device-event gate injects the UART IRQ only when (wait_reason==TIMER && owner==vcpu). ash sleeps on stdin with vm0 parked on TIMER; when the owner face is absent, the RX interrupt never enters. Station 24B's exclusion, built to stop idle-background CPUs from self-spinning, silently starved the subshell.

6/8
The fix is one clause wide: a non-owner TIMER park with genuinely consumable RX data (passing the same enable gating) also wakes. Empty ring stays asleep — the station-20 self-spin lesson is preserved verbatim.

7/8
After the fix, the interactive chain lit up for the first time: one boot printed "sh: selftest: not found" — ash ate the probe's line, parsed it, executed it. Proof of life. But the success rate is 1 in 4: a nondeterministic race tangled with station 58's still-unsolved boot hang (20-75% of boots).

8/8
Epilogue: the nondeterministic race got benched, not fixed. Station 66 ships a line REPL: the shell parent reads lines itself (the same sys_read path the main loop has used for sixty gates) and forks one sh -c per line — every component is an already-proven deterministic surface, just composed for the first time. 3/4 boots green; the remaining 1/4 is the station-58 boot-hang clan, a global problem. Busybox applets (echo/ls/cat/var expansion) all live inline. Honest ledger: the deterministic sh -c criteria stay as the gate (11/11 green); the no-arg interactive mode ships for hands-on trial (exit returns to rtos>). Reliable interactivity needs the station-58 hang arc closed first — that's the next expedition's map. #buildinpublic
