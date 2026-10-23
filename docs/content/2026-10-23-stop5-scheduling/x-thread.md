<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# X / Twitter thread (EN, 8 tweets) — Stop 5: Scheduling

## Thread

**1/8**
Two VMs, one CPU, one keyboard. This week on my from-scratch ARM64
Type-1 hypervisor (pairing with AI, 1-2h/evening): the scheduling
station. The punchline up front: my *host* shell command got executed
by a *guest* VM, because two VMs were fighting over one UART data
register. 🧵👇

**2/8**
Design: the host shell IS the scheduler. Highest-priority task: while
you type, it serves you (host authority); while you don't, it pumps —
each RUNNING, non-waiting VM gets one 1s quantum. vmrun went async:
VMs are now real background processes, not demos that block the host.

**3/8**
Preemption: tick only sets a flag; the actual switch happens at trap
exits (参考实现's form, minimal). Two trigger points: guest-state IRQ exit
transcribes the exception frame into the vcpu frame and bails; HVC exits
check the quantum too — getc polling storms make ticks land in host
segments, so the host side debits the quantum. Together they close the
loop.

**4/8**
The proof: a guest spins 3G pure-compute iterations (~8s on TCG), gets
preempted SEVEN times at arbitrary instruction boundaries, and its
counter survives every migration. Gate criterion: preempts delta ≥2
before/after the spin — anchor behavior, not wall-clock. (TCG speed
is machine-dependent; quanta aren't.)

**5/8**
The nastiest bug of the station: tick delivered ONCE, then died.
git-bisect pointed at exceptions.c, but the source *looked* perfect.
objdump diff: the EOI call had vanished from one branch. A multi-line
comment patch had swallowed a single-statement if's body — the if
became empty, the next line (EOI) got conditionally bound to it, host
idle path skipped EOI, level-triggered INTID 26 never dropped, GIC
thought the interrupt was still active forever.

**6/8**
Lesson: "the source looks correct" ≠ "the assembly is correct".
git-bisect finds the file; objdump convicts. Single-statement if +
multi-line comment patch = a silent killer. Level-triggered IRQs
punish a missing EOI with total silence, not an error.

**7/8**
And one honest mystery, filed not fixed: a time-based spin reading
CNTVCT_EL0 advanced at exactly 62.5M/s (the counter frequency!) with
proven-lossless frame migration, yet cmp/b.lo never fell through
across preemption boundaries. Suspected QEMU mrs-helper × TB cache
interaction. Documented in devlog 05, switched to pure iteration
count. Don't open the emulator's chest when your evidence is already
closed.

**8/8**
Result: 22-step gate ×3 green (clean-rebuild included), spin crossed
7 quanta losslessly, WFI became real CPU yield (fairness = others get
your quanta), devlog 05 has all ten interaction-surface bugs.
Next: per-vCPU vtimer contexts, or a trapped vuart device model.
github.com/eshine-os/eshine-os · www.eshine-os.org

## 附图清单

- T1: 双 VM token 交错输出截图
- T3: ARCHITECTURE.md 调度节(泵循环代码)
- T4: `spun 3G iters (preempted ok)` + vmstat 前后差特写
- T5: 源码段 vs objdump diff 对比图(EOI 消失)——thread 封面图
- T8: make verify 22 步 PASS 截图
