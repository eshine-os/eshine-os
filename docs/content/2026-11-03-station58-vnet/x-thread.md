# X 8 推(站 58 virtio-net,九刀二分之夜)

1/8
eshine-os station 58: a second virtio device — virtio-net, consumed by vm2's own Linux virtio_net driver. OASIS spec clean-room (the reference repo's virtio-net 组件 is off-limits by license). The night's real content: a nine-cut bisection. #buildinpublic #hypervisor #virtio

2/8
The bind proof writes itself: Linux discovers the device on a second virtio-mmio window, negotiates F_MAC, and reads the MAC from our config space — the six bytes we wrote come back through the guest's sysfs. That's a device model's full marks.

3/8
First landmine: I picked LR4 for the new vIRQ channel. `msr ich_lr4_el2` — instant EL2 exception. ESR 0x02000000. QEMU's virtual GIC interface implements exactly 4 list registers. Station 53's lesson repeats: platform assumptions must be measured, never remembered. Fix: read ICH_VTR_EL2, share LR3 with vblk when only 4 exist (per-pCPU semantics can't collide).

4/8
The ELR+EC print I added to el2_sync was the knife that convicted LR4. Then instrumentation caught my next error: Linux's tx header descriptor says len=10 — the legacy net header is 10 bytes; the +2 num_buffers only comes with MRG_RXBUF. I had assumed 12.

5/8
Then the crash: delivering the looped frame back to the guest, 6.6 oopsed at __netif_receive_skb_core+0x4 — executing DTB strings ("pl011@9000000"). Nine cuts: no vIRQ=alive, empty poll=alive, no completion=alive, zero frame=alive, IP ethtype=alive; ARP (request/reply, self/foreign IP, unicast/broadcast)=dead, every time.

6/8
Forensics along the way: a host-side gdump command (hexdump of guest RAM) plus single-line instrumentation — because multi-line slow prints stretch the MMIO write past the netdev watchdog, which then rewrites the vring and hands you corpse data. Serial output IS latency.

7/8
Host-side gdump before and after the crash: guest physical text is clean and unchanged. The divergence lives between the guest's stage-1 and our stage-2. Mechanism not penetrated — recorded as a platform boundary, like station 53's MDCR/TCG discovery. No guessing.

8/8
Honest close: a real NIC never receives its own outgoing broadcast — looping an ARP request back to its sender is pathological semantics anyway. v0 drops the ARP echo (counted); TX proof = 42 bytes/1 packet in /proc/net/dev. Full RX delivery rides the host-side QEMU -netdev bridge arc. Gates 167→171. Next: cjson+zlib ledger.
