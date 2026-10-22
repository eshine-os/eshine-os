# X / Twitter thread (English, 8 tweets) — Stop 2: VM abstraction

1/ Last night my from-scratch ARM64 Type-1 hypervisor grew up: the VM
became a real object. A tiny manager shell — vmls / vmcreate / vmrun /
vmkill — now drives two guests through a lifecycle state machine.
Open source, Apache-2.0, built 1-2h/day pair-programming with AI.

2/ The design choice that matters: every VM slot embeds its own
complete stage-2 page tables (~20KB) plus its vCPU. No heap, no
allocator yet — static and deterministic. The VM struct *is* the
address space.

3/ The isolation proof: both guests link at the same "physical"
address 0x40000000 — but VM0's stage-2 maps it to PA 0x50000000 and
VM1's to PA 0x51000000. Non-overlapping windows. VM0 could write
wild pointers all day and never touch VM1's image. Isolation is a
property of the page tables, not of good behavior.

4/ Same source, two identities: -DGUEST_ID=0/1 builds guest1.bin and
guest2.bin from one tree. Banners and prompts differ; every other
byte is identical. The gate can tell the VMs apart, and so can you.

5/ Bug #1 (gdb-verified): after growing the VM struct, a leftover
stack-allocated instance was ~20KB against a 16KB host stack. The
frame smashed the boot stage-2 tables; the guest died at its first
fetch. Host shell kept working — stack overflow in bare metal
corrupts *someone else's* memory.

6/ Bug #2: my table descriptor is (addr & ~0xFFF) | 0x3 — the mask
chops the low 12 bits. One table wasn't 4KB-aligned, so the pointer
folded back onto the pgd page: pgd[0] pointed at pgd itself. For
descriptor tables, alignment isn't optimization. It's correctness.

7/ Bug #3: after ERET, every scratch register belongs to the guest.
When the run returned, my caller's callee-saved held EL2 handler
residue — the code printed a VM id as the HVC exception's ESR value.
Stop 1 survived by luck: its caller only used adrp-reloaded strings.

8/ Gate: 14 true-green checks, run 4x in a row — both VM round trips,
guest tokens double-echoed inside each guest, host integrity after
everything. Repo: github.com/eshine-os/eshine-os
Next stop: full vCPU context switch (the register debt comes due).
