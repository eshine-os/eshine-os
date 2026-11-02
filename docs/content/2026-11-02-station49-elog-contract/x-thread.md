# X 8 推(站 49 日志契约)

1/8
eshine-os station 49: one logging contract, two operating systems (hypervisor EL2 + RTOS EL1), single shared core in common/ with per-side weak-symbol backend hooks. Clean-room from the 参考实现 mechanism spec, zero code copied. #buildinpublic

2/8
Module registry via linker sections: LOG_MODULE_REGISTER("sched") drops a descriptor into the "eshine_log_mods" section; GNU ld synthesizes __start_/__stop_ so the kernel can enumerate modules at runtime. No constructors — bare-metal EL2 never promised to run them. #systems

3/8
Zero-cost filtering: the level check is inlined in the macro — below threshold means zero argument evaluation, zero function call. When you have a 100Hz RTOS and a hypervisor on the same chip, logging discipline IS performance discipline.

4/8
The 参考实现 contract said "single-writer ring". We run 4 CPUs. So the ring-store hook is weak: default = raw store, each side overrides it to take a lock around the raw path. The contract's intent (repos bring their own ring) becomes the fix for its own assumption. #concurrency

5/8
Neither tree had a snprintf (hand-rolled lines only, house tradition). A logging contract NEEDS formatting — so the core ships an 80-line minimal vsnprintf (%d %u %x %s %c %p, bounds-checked per byte). Sometimes adopting a contract means writing the missing organ. #C

6/8
Station landmine #1: I placed two object rules BEFORE the all: target in the Makefile. First target = default goal. Bare `make` compiled exactly one file and declared "nothing to be done" — new file never compiled, stale objects, no relink, zero warnings. Silent build, loud lie.

7/8
Verify by tools, not vibes: objdump showed no .eshine_log_mods section → traced to the hijacked default goal → fixed → section present (0x20 = 2 modules), __start_ symbol synthesized, both ELFs. The mechanism exists when the section exists.

8/8
Smoke 24/24: three levels emitted, dbg/trc absent at default INF (forbidden-needle assertion!), runtime per-module level control, module table, 32KiB ring dump, ratelimit 3/7 — on both sides, the RTOS one through the syscall demo trampoline. Gates 133→139. Next: slog persistence + governance faces. #rtos
