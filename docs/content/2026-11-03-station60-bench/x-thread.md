# X 8 推(站 60 延迟基线,一拍超睡+号面事故)

1/8
eshine-os station 60: the first performance station — a cyclictest-shaped wake-latency histogram. The numbers are a baseline, not a gate: gates assert shape and bounds, values land in docs/benchmark/. #buildinpublic #RTOS #benchmark

2/8
The instrument: a task wakes every 10ms (one tick at 100Hz), measures actual-minus-nominal overshoot, 200 loops into 1ms buckets. Result: 9-10ms bucket dominant (149 loops), >10ms tail (50), one stray at 5-6ms.

3/8
The 9-10ms main cluster is exactly one tick of overshoot minus ~30µs phase: POSIX "sleep at least" on a tick-quantum scheduler needs ceil+1 tick boundaries. Phase stays locked (wake→sleep immediately), so the cluster is tight.

4/8
The 25% tail: phase walk. The loop period and the tick period differ by drift; occasionally the wake crosses a boundary and eats a second tick (≤20ms). And the 0-1ms bucket is always zero — the tick scheduler has no sub-tick wakeup, full stop. hrtimer is a future-arc line item, not a pretense.

5/8
The number incident: my first version assigned nanosleep syscall 35 — the x86_64 number. The smoke's histogram all-zero + rc=-1 + errno=38 (ENOSYS): three signals, one fact, nobody home on that number.

6/8
aarch64 uses asm-generic numbering: real nanosleep = 101. This is station 42's discipline re-violated: syscall numbers come from musl's installed headers, never memory. Along the way, objdump symbol-annotation counting proved unreliable as evidence (sendto counted 0 while demonstrably alive) — headers won.

7/8
Baseline discipline: docs/benchmark/wake-latency-baseline.md records the distribution + decomposition. Future stations (real board, user-space interrupt takeover, priority work) re-run and compare against main-cluster drift. Main cluster moving right = scheduling regression flag.

8/8
Gates 174→176, smoke 11/11, max=10054µs. Next: yocto image aggregation. #buildinpublic
