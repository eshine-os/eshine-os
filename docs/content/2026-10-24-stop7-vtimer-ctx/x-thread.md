<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# X / Twitter thread (EN, 8 tweets) — Stop 7: Per-VM vtimer Context

**1/8**
Two VMs, one physical virtual-timer, both want to sleep. Stop 7 of my
from-scratch ARM64 Type-1 hypervisor: per-VM timer context + multi-waiter.
The debt from stop 4's "single-waiter assumption" comes due. 🧵👇

**2/8**
Demo: vm0 sleeps 4s (arms first), vm1 sleeps 2s (arms second). vm1 wakes
first, vm0 second — wake ORDER asserted by stream position in the gate.
The stop-4 bug ("later armer overwrites, first sleeper never wakes") is
now a regression criterion.

**3/8**
The catch: cortex-a57 is ARMv8.0 — there is NO trap bit for CNTV_CTL/
CVAL accesses (those CNTHCTL bits came in later ARM extensions). Guests
write the physical timer directly. KVM on newer cores can trap+emulate;
we can't. So the host aligns at context switch (参考实现's form).

**4/8**
Switch pair: on VM entry, write its saved ctl/cval to the physical timer
(the physical vtimer always belongs to whoever is running — an invariant
with zero ambiguity). On ANY exit (yield/exit/fault/wait/preempt), read
the physical regs back into its frame tail. Hand back as-is on re-entry.

**5/8**
But if the physical timer always holds the *running* guest's, vm0's
4-second deadline can't fire while vm1 runs. Host-side per-tick sweep of
the saved ctx table: any WAITING+armed VM whose cval has passed gets
WOKE'd. Sound coarse at 1s cadence? It isn't:

**6/8**
A WOKE only takes effect when the pump next schedules that VM anyway.
Instant detection and tick-cadence detection are THE SAME timetable on a
single core. That's not a precision compromise — it's the essential
granularity of single-core scheduling.

**7/8**
First run woke them in the WRONG order (4s sleeper woke first!). Stop 5's
sched_waiter() scanned "first waiter in table order" — fine with one
waiter, instantly wrong with two: it handed vm1's interrupt to vm0,
whose deadline hadn't arrived. False wake. Fix: assign by EXPIRY, not
table order — the same predicate as the sweep. The old lesson
("beneficiary ≠ current holder") generalizes to "assign by expiry".

**8/8**
Also: the WAIT-reason check must read the sleeper's OWN saved ctx, not
the physical regs — the physical might hold someone else's view (that's
the switch-pair's whole point). Gate: 27 steps ×2 green.
github.com/eshine-os/eshine-os · www.eshine-os.org

## 附图清单

- T1: 双睡醒序终端输出(封面)
- T4: 切换对代码(泵切入/切出)
- T6: 表序假唤醒反例输出
- T8: make verify 27 步 PASS
