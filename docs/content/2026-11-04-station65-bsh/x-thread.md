# X 8 推(站 65 bsh 子壳,musl fork 考古)

1/8
eshine-os station 65: `bsh` — a busybox ash subshell beside the rtos> shell. But the real story is what the first musl program to ever call fork() dug up: a syscall-contract gap that had been hiding for 14 stations. #buildinpublic #musl #busybox

2/8
The archaeology: aarch64 has no __NR_fork. Not "our kernel doesn't have it" — the arch's syscall table doesn't define it. musl implements fork() and vfork() both as clone(SIGCHLD, 0). Zero thread flags. If your clone() only accepts the thread-flag set, you've rejected every musl fork that was ever attempted.

3/8
Why did 14 stations of musl programs never trip it? hello, beat, initmark, jsontest, ztest, coreparse — none of them ever calls fork. busybox ash was the first musl process in the repo to fork. The gap was real the whole time; it just had no customers.

4/8
Fix one: sys_clone forwards flagless-thread calls to sys_fork (process-copy semantics). CLONE_VFORK degrades to fork — which is exactly musl's own vfork shape.

5/8
Fix two, and my favorite part: ash's grandchild died with far=-0xA4. The station-64 ELF core file — built yesterday — delivered the dead task's esr/elr/far to my desk. Disassembled elr: `mrs x0, tpidr_el0; sub x0, x0, #0xA4`. That's musl's __pthread_self inline.

6/8
The grandchild runs busybox code without exec'ing — and fork didn't inherit the parent's tpidr. Task switch loads the zero slot into hardware, __pthread_self computes 0-0xA4, wild access. The forensics tool caught its own first real bug, one day after it was built.

7/8
Honest edges, all on the record: applet-type builtins (printf) hit execve ENOSYS — exec stays in the user-space loader per our red line. The argument-less interactive mode hangs the whole VM (tick dies, pump alive — gdb froze at pump_run_vcpu); filed as a deep-water arc.

8/8
Verdict: `bsh echo hi`, `bsh V=hi; echo $V` — all green, back to rtos> clean. Gates 182→183. The subshell wears the same door key as everything else: real syscalls, real contracts. #buildinpublic
