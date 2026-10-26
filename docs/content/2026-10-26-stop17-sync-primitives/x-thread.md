<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# X Thread — Station 17: my RTOS learns to wait (semaphores + mutexes), and a name-length bug detonates (8 posts)

**1/** Station 17 of eshine-os: my from-scratch RTOS guest gets its sync primitives — counting semaphores + owner/recursive mutexes, each with a per-object FIFO wait queue. Waiting is no longer "the shell polls the UART": any task can pend on any object. The kernel personality is now complete. 🧵

**2/** The contract that matters: "wake = permission to retry, not a grant." take is a re-check loop; give releases the resource, wakes the queue head, and — if the woken task has higher priority — yields immediately (rt_sched is the resume point in my unified task frame). Barging just re-pends. No lost wakeups, no deadlock, FIFO fairness holds by construction on single core.

**3/** The judge I'm proudest of: mid-hold ps. The mutex holder sleeps 300ms holding the lock; the gate script sends `ps` inside that window and asserts two rows in the LIVE task table: `contender prio 6 SLEEP` and `waiter prio 8 BLOCK`. Blocking isn't a printf from the task — it's accounting state in the scheduler's books.

**4/** Also shipped: short DAIF critical sections around every count/owner/queue mutation — AND around the ready-queue substrate itself, which since station 16 has had a latent task/ISR dual-writer window: two enqueues interleaved across a tick preemption can push a task off the tail and desync the tail pointer — every later enqueue lands nowhere. Lost forever. Fixed at the source (rdy_enqueue/pick now save/restore DAIF).

**5/** First gate run with the new judges: detonation. [26] times out, VM0 dies with an instruction abort at PC=0. The -d int autopsy: two erets back to back — the second eret drops to EL0 with ELR=0/SPSR=0. Someone popped an all-zero frame at 0xd430. Reverse math: 0xd430 = stacks_base + 9×4096 − 272 = the frame slot of "task index 8"... but the pool has 8 slots, 0–7.

**6/** objdump audit of every new primitive, the scheduler, the frame forger: all clean. Then I re-read station 16's rt_task_create. The name-copy loop reuses `i`. The rt_frame_init call afterwards passes `task_stacks[i]` — where i is now THE NAME LENGTH. My kernel has been assigning stacks BY TASK NAME LENGTH for its entire life: shell(5) on stack[5], heartbeat(9) on an out-of-bounds "stack[9]" sitting on top of the virtio queue.

**7/** It survived two full gate certifications by pure luck — every resident task name had a different length, and the two 6-char tasks never ran concurrently. Then station 17 created the first same-length concurrent pair: producer and consumer, both 8 letters, sharing one frame region at 0xd430. Two identities, one stack. The fix is three lines: save the slot index before the name loop. Lesson: loop-index reuse is a silent killer; two green gates ≠ no landmine.

**8/** Honesty report: during double-green certification, one run of four flaked at [53] — Linux froze at kernel-time 0.489s (mid-unpack, before /init) while VM0's heartbeats flowed on. That's the documented station-16 residual physics: vgic_wake_waiter unconditionally drops the physical timer line, and an in-flight unclaimed HW-LR tick evaporates with it; a quiet-phase Linux has no other interrupt to wake it and re-program its timer. The real fix (on in-flight: return to the owner VM to consume its own tick) is station 18's first candidate — two independent symptom reports now on file. Apache-2.0, clean-room, 58 steps ×2. https://github.com/eshine-os/eshine-os
