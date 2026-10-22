# X / Twitter thread (English, 8 tweets) — Stop 3: full vCPU context switch

1/ Stop 3 of my from-scratch ARM64 Type-1 hypervisor: the vCPU context
switch. Three features, three real bugs, one lesson about stacks that
leak. As always: 1-2h/day, human + AI pair programming, Apache-2.0,
everything on GitHub.

2/ A trap frame now lives inside the vcpu: an asm stub at the
lower-EL sync slot saves x0-x30 + SP_EL1 + ELR + SPSR before any C
code runs. Bootstrapping: you can't scratch x18-x28 (they're the
guest's), so the stub parks x0 on the exception stack first. From
this point, the EL2 handler can use ANY register. The
compiler-coincidence debt is retired.

3/ HVC #2 — yield. The guest hands the CPU back, the VM goes PAUSED,
and a later vmrun resumes it at the breakpoint. hvc_yield() returns
like a plain function call. The PAUSED state has been in my state
machine since week one; tonight it finally earned its keep. Zero
new transitions needed.

4/ Fault isolation: a guest touching an unmapped address used to
halt the whole hypervisor. Now it prints the frame's diagnostics —
ESR, FAR, and the saved registers, so you literally see the wild
pointer's original value in x0 — and the VM dies clean. The host
keeps typing. Guest crashes are data; host crashes are incidents.

5/ Bug #1: I marked vcpu_enter_guest noreturn. But the retire path
DOES return through it. GCC trusted me and emitted no continuation
— the first guest exit hung in the dead zone. noreturn belongs to
the ERET-out path, not the returns-through path.

6/ Bug #2, the big one: trap_resume ERETs out without unwinding, so
nobody gives SP_EL2 back. Every HVC leaked ~110 bytes of handler
frame. Trap #79 walked the stack through the 4KB bottom and stomped
cur_vcpu — 16 bytes below the stack. Next trap loaded a zeroed
frame, guest went to PC 0, host followed.

7/ Why didn't stop 2 catch this? Its full gate is ~84 traps × 32
bytes ≈ 2.7KB. Under the 4KB margin. Not "no bug" — "bug not yet
due." Fix: the trap entry resets SP_EL2 to the top itself. Entry
normalizes; exits may abandon frames.

8/ Bug #3: GCC -O2 folded my register-integrity selftest (sum
0..99) into the constant 4950 — the loop compiled with NO
accumulator. One empty asm "+r"(sum) later, the sum lives in x30
and the counter in x19, both crossing every trap. objdump-verified.
Gate: 17 true-green checks ×2. github.com/eshine-os/eshine-os
