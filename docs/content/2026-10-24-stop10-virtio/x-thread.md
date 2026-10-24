<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# X / Twitter thread (EN, 8 tweets) — Stop 10: virtio Proper

**1/8**
Stop 10 of my from-scratch ARM64 Type-1 hypervisor: I retired my own
homemade block protocol and replaced it with REAL virtio — virtio-mmio
legacy + split virtqueue + virtio-blk. Best part: the migration moved
ZERO data. The gate criteria needles from stop 9 run UNCHANGED. 🧵👇

**2/8**
Why retire your own protocol? It runs in 5 minutes, but its only user
is you. My vblk was a draft for virtio all along — "memory is the
transport, registers are the doorbell" was always the virtqueue
philosophy. Same static disk array carried over byte-for-byte; stop 9's
data survives the protocol swap.

**3/8**
virtio-mmio legacy register set: probe triple-verify (magic
0x74726976, version 2, DeviceID 2) — device identity is HANDSHAKED,
not assumed. Status steps ACK→DRIVER→DRIVER_OK. QueuePFN delivers the
queue's physical frame number. And DRIVER_OK doubles as the interrupt
enable — virtio-mmio has no IMSC; the semantics are cleaner than a
real PL011.

**4/8**
Split virtqueue legacy layout is a formula: desc table @+0 (16B×N),
avail ring @+16N, used ring @ next 4096 boundary. desc = free-list of
chains; each request borrows 3: header(type/sector) → data(512B) →
status(device writes OK/ERR). avail = guest's submit queue, used =
device's completion queue. Host and guest both derive from the same
formula — one builds chains, the other unbuilds them.

**5/8**
Then the linker trap. First version used three separate static
variables, each aligned(4096). But `aligned` guarantees ALIGNMENT, not
ADJACENCY — the linker is free to stuff other .bss objects between
them, and the layout formula breaks silently. Fix: ONE 8KB region,
members placed BY THE FORMULA at +0/+128/+4096.

**6/8**
Generalize that: don't hand structure layout to the linker. Any
formula about "where things sit relative to each other" must live on
one physically-contiguous region. Same class of bug as the trap-frame
offset contracts in earlier stops — the assembler/linker doesn't know
your intent.

**7/8**
Second battle: `#define g_avail ((struct vq_avail *)(g_vq+128))` is a
POINTER, and I wrote `.idx` after it. Compile error — pointer macros
take `->`. The compiler caught it at build time for zero runtime cost.
Two battles, both at BUILD time. Zero runtime debugging this stop.

**8/8**
Two consecutive first-test-green stops. The pattern: device stations
become "construction by blueprint" because stops 6 & 8 already paid
full price for the hard problems. Gate 31 steps ×2, criteria migrated
at equal strength. Skepticism compounds. github.com/eshine-os/eshine-os
· www.eshine-os.org

## 附图清单

- T1: virtio blk on → dwrite/dread with unchanged needles (hook)
- T2: vblk→virtio mapping table (封面图)
- T4: split virtqueue layout formula diagram
- T5: aligned-but-not-adjacent .bss layout illustration
- T8: gate 31 PASS ×2, criteria-migration diff
