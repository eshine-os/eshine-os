# X 8 推(站 55 PSCI 补面)

1/8
eshine-os station 55: PSCI completion. A VM that could only be executed (vmkill) can now leave on its own terms: CPU_OFF, AFFINITY_INFO, SYSTEM_OFF, SYSTEM_RESET. #hypervisor #buildinpublic

2/8
SYSTEM_OFF walks VM_DEAD without a fault line. Shutdown is a lifecycle event, not an incident — no ESR/FAR forensics, no "faulted" label. Clean death and accidental death are two different deaths.

3/8
SYSTEM_RESET is three brothers, not one jump: teardown (existing vm_finish machinery) -> rebuild (vm_create on the same slot) -> restart (vm_start). Implementation: a reset flag set in the PSCI handler, consumed at the pump exit after the lifecycle closes — reboot logic lives in the lifecycle machine, not the exception path.

4/8
CPU_OFF parks the CALLING vcpu (core 0 always denied — the primary never takes itself offline). v0 caveat honestly recorded: the park is wait-with-no-source, a stray SGI could re-wake it; hardening waits for a real consumer.

5/8
AFFINITY_INFO: guest-view MPIDR (0x80000000|idx) -> 0=ON / DENIED. Three faces proven live: vcpu0=0, vcpu1=0, invalid frame=0xFFFFFFFD.

6/8
Quota accounting: every pumped quantum counts per-VM (vm_quanta), one vmstat line. The 参考实现 实时配额调度 BUDGET/USED form. Accounting first, enforcement later — the consumer-side (RTOS APS partitioning, station 45) already governs the layer that matters.

7/8
Console choreography the smoke taught us: RESET releases the console owner (lifecycle close) — the revived VM needs a re-take (vmconsole) before the prompt is bare again. And always settle on "inittab processed" before attaching — the boot-window race family never sleeps.

8/8
Smoke 25/25: aff three faces -> quanta -> full reset arc (dying message -> rebuild -> self-report chain green -> console re-take) -> clean off -> revive chain. Gates 157→161. Batch B: one station left (DFX closeout: trail dump + vmstat + same-disease audits). #ARM
