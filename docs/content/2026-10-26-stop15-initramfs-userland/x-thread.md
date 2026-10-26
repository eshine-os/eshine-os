<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# X/Twitter thread — Station 15: first userspace (busybox PID 1)

1/ Station 15 of our from-scratch ARM64 Type-1 hypervisor: Linux runs
into USERSPACE. Station 14 ended on a VFS panic ("no rootfs"); today
`Run /init as init process` → busybox sh as PID 1 → interactive
`~ #` shell, straight from our management console. The project's
first userspace ever. 🧵

2/ The proof isn't just a prompt — it's `cat /proc/cpuinfo` from
userspace reporting `CPU part: 0xd07` (cortex-a57): the very CPU we
model. Every layer honest: QEMU → EL2 → stage-2 → Linux 6.6 →
initramfs → /init → sh → /proc.

3/ The rootfs is loaded the bootloader way: an 8MB fixed-slot
uncompressed cpio at guest-PA 0x44000000, announced via DTB
`linux,initrd-start/end` properties — constants, because the slot is
fixed. Zero kernel patches: 6.6 natively parses uncompressed cpio
(the RD_NONE option I thought we'd need doesn't even exist).

4/ Even the cpio packer is in-repo: 40 lines of Python. mknod needs
root, fakeroot isn't installed — so device nodes come from a spec
line (`--dev console:5:1`) instead of a privileged filesystem call.
From-scratch isn't always harder; sometimes it's just smaller.

5/ Humbling confession: in station 14 I "fixed" an FPEN bit-position
bug (3<<20 → 3<<22). This station proved my fix was a no-op all
along — the kernel's sysreg.h says FPEN is bits [21:20]. The boot
worked because the kernel wrote its own 3<<20 at __cpu_setup.

6/ But `msr` is a WHOLE-register write: our per-entry 3<<22 silently
zeroed the kernel's FPEN. First explicit SIMD — the userspace FP
restore in fpsimd_load_state — trapped EC=0x07 and the kernel
panicked with "Unhandled exception". Lesson: read the spec, don't
guess bit positions. The fix: delete the line. The guest owns its
own CPACR.

7/ Third time this project bites itself on "emulation layer
overreach" (SP_EL0, vtimer injection, now CPACR). The philosophy
that keeps emerging: the guest's context belongs to the guest — the
host emulates events, not policy.

8/ One more design fix: bare-metal guests used to auto-return the
console when sleeping on a timer — but a Linux shell waiting for
input is ALWAYS timer-armed (250Hz), so the console was taken away
every sleep. Console ownership is now explicit, with an escape
character (Ctrl-], the Xen/ssh convention) to reclaim the host. 48-step
gate, double green. Next: more things to do inside that shell.
